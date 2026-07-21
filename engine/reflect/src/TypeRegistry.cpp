// TypeRegistry.cpp — 전역 타입 레지스트리 (M3-A 골격 스텁)
//
// 실제 등록/조회 인덱스·충돌 검사는 구현 에이전트(refl 구현)가 채운다. 여기서는 헤더 계약을
// 만족하는 최소 골격만 두어 링크가 통과하도록 한다.
#include "mye/refl/TypeRegistry.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mye::refl {

struct TypeRegistry::Impl {
    std::unordered_map<TypeId, std::unique_ptr<TypeInfo>> byId;
    std::vector<const TypeInfo*> ordered;
};

TypeRegistry::TypeRegistry() : m_impl(std::make_unique<Impl>()) {}
TypeRegistry::~TypeRegistry() = default;

TypeRegistry& TypeRegistry::Get() {
    static TypeRegistry instance;
    return instance;
}

const TypeInfo* TypeRegistry::Find(TypeId id) const {
    auto it = m_impl->byId.find(id);
    return it == m_impl->byId.end() ? nullptr : it->second.get();
}

const TypeInfo* TypeRegistry::Find(std::string_view name) const {
    return Find(TypeIdFromName(name));
}

Expected<const TypeInfo*, Error> TypeRegistry::Register(std::unique_ptr<TypeInfo> info) {
    if (!info) return Error{"TypeRegistry::Register: null TypeInfo", 1};
    const TypeId id = info->Id();
    if (auto it = m_impl->byId.find(id); it != m_impl->byId.end()) {
        const TypeInfo* existing = it->second.get();
        // 같은 TypeId 인데 이름이 다르면 해시 충돌(FNV-1a 다른 문자열 동일 해시) — 치명적 에러.
        if (existing->Name() != info->Name()) {
            return Error{std::string("TypeRegistry::Register: TypeId collision between '") +
                             std::string(existing->Name()) + "' and '" +
                             std::string(info->Name()) + "'",
                         2};
        }
        // 같은 이름 = 재진입/중복 등록. 기존 안정 포인터를 그대로 반환(멱등).
        return existing;
    }
    const TypeInfo* raw = info.get();
    m_impl->byId.emplace(id, std::move(info));
    m_impl->ordered.push_back(raw);
    return raw;
}

void TypeRegistry::Unregister(std::span<const TypeId> ids) {
    for (TypeId id : ids) {
        auto it = m_impl->byId.find(id);
        if (it == m_impl->byId.end()) continue;
        const TypeInfo* raw = it->second.get();
        for (auto oit = m_impl->ordered.begin(); oit != m_impl->ordered.end(); ++oit) {
            if (*oit == raw) { m_impl->ordered.erase(oit); break; }
        }
        m_impl->byId.erase(it);
    }
}

std::span<const TypeInfo* const> TypeRegistry::All() const {
    return m_impl->ordered;
}

// ---- EnumInfo 조회 헬퍼 ----
bool EnumInfo::ValueOf(std::string_view name, std::int64_t& out) const {
    for (const auto& c : m_constants) {
        if (c.name == name) { out = c.value; return true; }
    }
    return false;
}
bool EnumInfo::NameOf(std::int64_t value, std::string_view& out) const {
    for (const auto& c : m_constants) {
        if (c.value == value) { out = c.name; return true; }
    }
    return false;
}

// ---- TypeInfo 조회·벡터 훅 위임 ----
const FieldInfo* TypeInfo::FindField(std::string_view name) const {
    for (const auto& f : m_fields) {
        if (f.Name() == name) return &f;
    }
    return nullptr;
}

std::size_t TypeInfo::VectorSize(const void* vec) const { return m_vecSize ? m_vecSize(vec) : 0; }
void TypeInfo::VectorResize(void* vec, std::size_t n) const { if (m_vecResize) m_vecResize(vec, n); }
void* TypeInfo::VectorElementAt(void* vec, std::size_t i) const { return m_vecAt ? m_vecAt(vec, i) : nullptr; }
const void* TypeInfo::VectorElementAt(const void* vec, std::size_t i) const { return m_vecAtConst ? m_vecAtConst(vec, i) : nullptr; }

const Attribute* FieldInfo::FindAttribute(AttributeKind kind) const {
    for (const auto& a : m_attributes) {
        if (a.kind == kind) return &a;
    }
    return nullptr;
}

} // namespace mye::refl
