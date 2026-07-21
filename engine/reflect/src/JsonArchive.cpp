// JsonArchive.cpp — 리플렉션 ↔ json::Value 대칭 아카이브 (04 §직렬화 JsonArchive)
//
// core json::Value 는 불변(생성 후 mutation API 없음)이므로:
//   - 쓰기: 자체 가변 노드 트리(WNode)를 스택으로 쌓고, Root() 최초 호출 시 json::Value 로 굳힌다.
//   - 읽기: 불변 json::Value 트리를 커서 스택으로 순회한다.
//
// 버전 필드: BeginObject 가 "__version" 키를 쓰고/읽는다. 읽기 시 version out 파라미터로
//   구버전을 알린다(필드 RenamedFrom 폴백은 Key/HasKey 단계에서 처리).
// AssetRef: reflect 는 asset 을 링크하지 않으므로(L1.5 경계) 전방선언 타입을 값 의미로만
//   다룬다 — 계약이 고정한 레이아웃(AssetGuid{hi,lo} + type:u64) 미러로 왕복.
#include "mye/ser/JsonArchive.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// asset::AssetRef 는 전방선언만 사용(링크 없음). 계약 고정 레이아웃 미러(값 의미 왕복 전용).
namespace mye::asset {
struct AssetRef;
namespace {
struct AssetGuidMirror { std::uint64_t hi = 0; std::uint64_t lo = 0; };
struct AssetRefMirror  { AssetGuidMirror guid; std::uint64_t type = 0; };
}
}

namespace mye::ser {

namespace {

constexpr const char* kVersionKey = "__version";

// UUID 왕복(AssetGuid.hi/lo ↔ 8-4-4-4-12 소문자 hex). reflect 는 asset 을 링크하지
// 않으므로 자체 최소 구현(계약 표기와 동일 형식).
std::string GuidToString(std::uint64_t hi, std::uint64_t lo) {
    static const char* d = "0123456789abcdef";
    char buf[36];
    std::uint64_t parts[16];
    for (int i = 0; i < 8; ++i) parts[i]      = (hi >> ((7 - i) * 8)) & 0xff;
    for (int i = 0; i < 8; ++i) parts[8 + i]  = (lo >> ((7 - i) * 8)) & 0xff;
    int pos = 0;
    auto emit = [&](int byteCount, int startByte) {
        for (int b = 0; b < byteCount; ++b) {
            std::uint64_t v = parts[startByte + b];
            buf[pos++] = d[(v >> 4) & 0xf];
            buf[pos++] = d[v & 0xf];
        }
    };
    emit(4, 0);  buf[pos++] = '-';
    emit(2, 4);  buf[pos++] = '-';
    emit(2, 6);  buf[pos++] = '-';
    emit(2, 8);  buf[pos++] = '-';
    emit(6, 10);
    return std::string(buf, buf + 36);
}

bool GuidFromString(std::string_view s, std::uint64_t& hi, std::uint64_t& lo) {
    std::uint8_t bytes[16];
    int bi = 0;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < s.size() && bi < 16;) {
        if (s[i] == '-') { ++i; continue; }
        if (i + 1 >= s.size()) return false;
        int a = hexVal(s[i]), b = hexVal(s[i + 1]);
        if (a < 0 || b < 0) return false;
        bytes[bi++] = static_cast<std::uint8_t>((a << 4) | b);
        i += 2;
    }
    if (bi != 16) return false;
    hi = lo = 0;
    for (int i = 0; i < 8; ++i) hi = (hi << 8) | bytes[i];
    for (int i = 0; i < 8; ++i) lo = (lo << 8) | bytes[8 + i];
    return true;
}

// ---- 쓰기용 가변 노드 트리 ----
struct WNode;
using WNodePtr = std::unique_ptr<WNode>;

struct WNode {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool        b = false;
    double      num = 0.0;
    std::string str;
    std::vector<WNodePtr> arr;
    std::vector<std::pair<std::string, WNodePtr>> obj;   // 삽입 순서 보존
};

json::Value ToJson(const WNode& n) {
    switch (n.t) {
    case WNode::T::Null: return json::Value{};
    case WNode::T::Bool: return json::Value{n.b};
    case WNode::T::Num:  return json::Value{n.num};
    case WNode::T::Str:  return json::Value{n.str};
    case WNode::T::Arr: {
        json::Value::Array a;
        a.reserve(n.arr.size());
        for (const auto& e : n.arr) a.push_back(ToJson(*e));
        return json::Value{std::move(a)};
    }
    case WNode::T::Obj: {
        json::Value::Object o;
        for (const auto& kv : n.obj) o.emplace(kv.first, ToJson(*kv.second));
        return json::Value{std::move(o)};
    }
    }
    return json::Value{};
}

} // namespace

struct JsonArchive::Impl {
    bool reading = false;

    // ---- 쓰기 상태 ----
    WNodePtr           writeRootNode;       // 최상위 노드
    std::vector<WNode*> writeStack;         // 현재 열린 컨테이너
    std::string        pendingKey;          // 다음 값이 붙을 오브젝트 키
    bool               hasPendingKey = false;
    mutable json::Value builtRoot;          // Root() 캐시(쓰기 모드)
    mutable bool       builtValid = false;

    // ---- 읽기 상태 ----
    const json::Value* readRoot = nullptr;
    struct ReadCtx {
        const json::Value* node = nullptr;  // 현재 컨테이너
        bool  isArray = false;
        std::size_t index = 0;              // 배열 진행 인덱스
    };
    std::vector<ReadCtx> readStack;
    const json::Value*  readSelected = nullptr;  // Key() 로 선택된 다음 값(오브젝트 필드)
    bool                readOk = true;

    // 쓰기: 새 노드를 현재 컨테이너에 부착하고 포인터 반환.
    WNode* Attach(WNodePtr node) {
        WNode* raw = node.get();
        if (writeStack.empty()) {
            writeRootNode = std::move(node);
            return raw;
        }
        WNode* top = writeStack.back();
        if (top->t == WNode::T::Arr) {
            top->arr.push_back(std::move(node));
        } else {  // Obj
            std::string k = hasPendingKey ? pendingKey : std::string{};
            hasPendingKey = false;
            top->obj.emplace_back(std::move(k), std::move(node));
        }
        return raw;
    }

    // 읽기: 다음 값(Value/Begin*)이 소비할 노드를 결정.
    const json::Value* NextReadNode() {
        if (readStack.empty()) return readRoot;
        ReadCtx& ctx = readStack.back();
        if (ctx.isArray) {
            if (!ctx.node || !ctx.node->IsArray()) return nullptr;
            const auto& a = ctx.node->AsArray();
            if (ctx.index >= a.size()) return nullptr;
            return &a[ctx.index++];
        }
        // 오브젝트 — Key() 로 선택된 노드.
        const json::Value* sel = readSelected;
        readSelected = nullptr;
        return sel;
    }
};

JsonArchive::JsonArchive() : m_impl(std::make_unique<Impl>()) {}
JsonArchive::JsonArchive(JsonArchive&&) noexcept = default;
JsonArchive& JsonArchive::operator=(JsonArchive&&) noexcept = default;
JsonArchive::~JsonArchive() = default;

JsonArchive JsonArchive::ForWrite() {
    JsonArchive a;
    a.m_impl->reading = false;
    return a;
}

JsonArchive JsonArchive::ForRead(const json::Value& root) {
    JsonArchive a;
    a.m_impl->reading = true;
    a.m_impl->readRoot = &root;
    return a;
}

bool JsonArchive::IsReading() const { return m_impl->reading; }

void JsonArchive::Key(std::string_view name) {
    if (m_impl->reading) {
        // 현재 오브젝트에서 name 필드를 선택.
        const json::Value* obj =
            m_impl->readStack.empty() ? m_impl->readRoot : m_impl->readStack.back().node;
        m_impl->readSelected = (obj && obj->IsObject()) ? obj->Find(name) : nullptr;
    } else {
        m_impl->pendingKey.assign(name.begin(), name.end());
        m_impl->hasPendingKey = true;
    }
}

void JsonArchive::Value(bool& v) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        if (n && n->IsBool()) v = n->AsBool();
        else if (!n) m_impl->readOk = m_impl->readOk;   // 미존재 = 기본값 유지(정상)
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Bool; node->b = v;
        m_impl->Attach(std::move(node));
    }
}

void JsonArchive::Value(std::int64_t& v) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        if (n && n->IsNumber()) v = n->AsInt();
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Num; node->num = static_cast<double>(v);
        m_impl->Attach(std::move(node));
    }
}

void JsonArchive::Value(double& v) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        if (n && n->IsNumber()) v = n->AsDouble();
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Num; node->num = v;
        m_impl->Attach(std::move(node));
    }
}

void JsonArchive::Value(std::string& v) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        if (n && n->IsString()) v = std::string(n->AsString());
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Str; node->str = v;
        m_impl->Attach(std::move(node));
    }
}

void JsonArchive::Value(mye::asset::AssetRef& v) {
    auto* ref = reinterpret_cast<mye::asset::AssetRefMirror*>(&v);
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        if (n && n->IsObject()) {
            if (const json::Value* g = n->Find("guid"); g && g->IsString()) {
                GuidFromString(g->AsString(), ref->guid.hi, ref->guid.lo);
            }
            // type 은 u64 FNV 해시 — double 로는 정밀도 손실(>2^53)이므로 10진 문자열로 왕복.
            if (const json::Value* t = n->Find("type"); t && t->IsString()) {
                ref->type = std::strtoull(std::string(t->AsString()).c_str(), nullptr, 10);
            } else if (t && t->IsNumber()) {   // 하위호환(작은 값) 폴백
                ref->type = static_cast<std::uint64_t>(t->AsInt());
            }
        }
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Obj;
        auto guidNode = std::make_unique<WNode>();
        guidNode->t = WNode::T::Str;
        guidNode->str = GuidToString(ref->guid.hi, ref->guid.lo);
        node->obj.emplace_back("guid", std::move(guidNode));
        auto typeNode = std::make_unique<WNode>();
        typeNode->t = WNode::T::Str;   // u64 해시는 문자열로(정밀도 보존).
        typeNode->str = std::to_string(ref->type);
        node->obj.emplace_back("type", std::move(typeNode));
        m_impl->Attach(std::move(node));
    }
}

void JsonArchive::BeginObject(std::string_view /*typeName*/, std::uint32_t& version) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        Impl::ReadCtx ctx;
        ctx.node = (n && n->IsObject()) ? n : nullptr;
        ctx.isArray = false;
        m_impl->readStack.push_back(ctx);
        // 버전 필드 out — 없으면 0(최초 버전) 취급.
        if (ctx.node) {
            if (const json::Value* vv = ctx.node->Find(kVersionKey); vv && vv->IsNumber()) {
                version = static_cast<std::uint32_t>(vv->AsInt());
            } else {
                version = 0;
            }
        }
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Obj;
        // __version 먼저 기록.
        auto vnode = std::make_unique<WNode>();
        vnode->t = WNode::T::Num; vnode->num = static_cast<double>(version);
        node->obj.emplace_back(kVersionKey, std::move(vnode));
        WNode* raw = m_impl->Attach(std::move(node));
        m_impl->writeStack.push_back(raw);
    }
}

void JsonArchive::EndObject() {
    if (m_impl->reading) {
        if (!m_impl->readStack.empty()) m_impl->readStack.pop_back();
    } else {
        if (!m_impl->writeStack.empty()) m_impl->writeStack.pop_back();
    }
}

void JsonArchive::BeginArray(std::size_t& count) {
    if (m_impl->reading) {
        const json::Value* n = m_impl->NextReadNode();
        Impl::ReadCtx ctx;
        ctx.node = (n && n->IsArray()) ? n : nullptr;
        ctx.isArray = true;
        ctx.index = 0;
        count = ctx.node ? ctx.node->AsArray().size() : 0;
        m_impl->readStack.push_back(ctx);
    } else {
        auto node = std::make_unique<WNode>();
        node->t = WNode::T::Arr;
        WNode* raw = m_impl->Attach(std::move(node));
        m_impl->writeStack.push_back(raw);
        (void)count;
    }
}

void JsonArchive::EndArray() { EndObject(); }

bool JsonArchive::HasKey(std::string_view name) const {
    if (!m_impl->reading) return false;
    const json::Value* obj =
        m_impl->readStack.empty() ? m_impl->readRoot : m_impl->readStack.back().node;
    return obj && obj->IsObject() && obj->Find(name) != nullptr;
}

bool JsonArchive::Ok() const { return m_impl->reading ? m_impl->readOk : true; }

const json::Value& JsonArchive::Root() const {
    if (m_impl->reading) {
        static const json::Value kNull;
        return m_impl->readRoot ? *m_impl->readRoot : kNull;
    }
    if (!m_impl->builtValid) {
        m_impl->builtRoot = m_impl->writeRootNode ? ToJson(*m_impl->writeRootNode) : json::Value{};
        m_impl->builtValid = true;
    }
    return m_impl->builtRoot;
}

} // namespace mye::ser
