// mye/runtime/Localization.cpp — StringTable·LocalizedText 골격 (M6-A)
//
// 골격 범위: Set/Get/현재 로케일/폴백 체인 + 자리표시자 치환({name}/{0})의 최소 동작 구현.
//   JSON 로드·VFS 로드·후처리 훅은 계약 스텁(구현 에이전트가 core json 파서로 채운다).
#include "mye/runtime/Localization.h"

#include "mye/core/Events.h"
#include "mye/core/Json.h"
#include "mye/asset/FileSystem.h"

namespace mye::runtime {

struct LocalizationSystem::Impl {
    EventBus* events = nullptr;
    Locale    current = Locale::Korean;
    FormatterHook hook;
    // locale → (key → value)
    std::unordered_map<std::string, std::string> tables[2];

    std::unordered_map<std::string, std::string>& Table(Locale loc) {
        return tables[static_cast<size_t>(loc)];
    }
    const std::unordered_map<std::string, std::string>& Table(Locale loc) const {
        return tables[static_cast<size_t>(loc)];
    }
};

LocalizationSystem::LocalizationSystem() : m_impl(std::make_unique<Impl>()) {}
LocalizationSystem::~LocalizationSystem() = default;

void LocalizationSystem::Initialize(EventBus* events) {
    m_impl->events = events;
}

void LocalizationSystem::Shutdown() {
    m_impl->tables[0].clear();
    m_impl->tables[1].clear();
    m_impl->events = nullptr;
}

Expected<void, Error> LocalizationSystem::LoadTableJson(Locale loc, std::string_view json) {
    // core 최소 JSON 파서로 { "key": "value", ... } 오브젝트를 파싱해 테이블에 병합.
    //   같은 key 는 나중 로드가 덮어쓴다(핫패치·오버레이 규약). 값이 문자열이 아니면 스킵.
    auto parsed = json::Parse(json);
    if (!parsed)
        return MakeError(RuntimeError::ParseFailed,
                         "LoadTableJson 파싱 실패: " + parsed.GetError().message);
    const json::Value& root = parsed.Value();
    if (!root.IsObject())
        return MakeError(RuntimeError::ParseFailed,
                         "LoadTableJson: 최상위가 오브젝트가 아님(key→value 테이블 기대)");

    auto& table = m_impl->Table(loc);
    for (const auto& [key, value] : root.AsObject()) {
        if (!value.IsString()) continue;   // 문자열 값만 문자열 테이블에 편입
        table[key] = std::string(value.AsString());
    }
    return Expected<void, Error>{};
}

Expected<void, Error> LocalizationSystem::LoadTableFromFile(
    Locale loc, asset::VirtualFileSystem& vfs, std::string_view vpath) {
    auto blob = vfs.ReadAll(vpath);
    if (!blob)
        return MakeError(RuntimeError::IoFailed,
                         "LoadTableFromFile 읽기 실패(" + std::string(vpath) + "): "
                             + blob.GetError().message);
    const auto& bytes = blob.Value();
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // UTF-8 BOM 제거
    return LoadTableJson(loc, text);
}

void LocalizationSystem::Set(Locale loc, std::string_view key, std::string value) {
    m_impl->Table(loc)[std::string(key)] = std::move(value);
}

void LocalizationSystem::SetLocale(Locale loc) {
    if (m_impl->current == loc) return;
    m_impl->current = loc;
    if (m_impl->events) {
        LocaleChangedEvent e{static_cast<uint8_t>(loc)};
        m_impl->events->Publish(e);
    }
}

Locale LocalizationSystem::CurrentLocale() const { return m_impl->current; }

void LocalizationSystem::SetFormatterHook(FormatterHook hook) { m_impl->hook = std::move(hook); }

std::string LocalizationSystem::Get(std::string_view key) const {
    const std::string k(key);
    // 폴백 체인: 현재 → ko → en → key 자체.
    const Locale order[3] = { m_impl->current, Locale::Korean, Locale::English };
    for (Locale loc : order) {
        const auto& t = m_impl->Table(loc);
        auto it = t.find(k);
        if (it != t.end()) return it->second;
    }
    return k;
}

std::string LocalizationSystem::Format(std::string_view key,
                                       const std::vector<FormatArg>& args) const {
    std::string result = Get(key);
    // 최소 자리표시자 치환: "{arg.key}" → arg.value. 단순 순차 replace(골격).
    for (const auto& a : args) {
        const std::string token = "{" + a.key + "}";
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), a.value);
            pos += a.value.size();
        }
    }
    if (m_impl->hook) result = m_impl->hook(m_impl->current, result);
    return result;
}

std::string LocalizationSystem::Resolve(const LocalizedText& text,
                                        const std::vector<FormatArg>& args) const {
    if (text.IsKeyed()) return Format(text.key, args);
    return text.literal;
}

} // namespace mye::runtime
