// Config.cpp — 계층 설정 시스템: 스코프 로드·병합·조회·ConfigChangedEvent 발행 (docs/01)
//
// M0 구현 범위: Engine + Project 로드·병합 (User/RuntimeOverlay 스코프도 동일 경로로 동작).
// 조회 병합 순서: RuntimeOverlay > User > Project > Engine. 메인 스레드 전용(뮤텍스 없음).
#include "mye/core/Config.h"
#include "mye/core/Assert.h"
#include "mye/core/Events.h"
#include "mye/core/Json.h"
#include "mye/core/Log.h"

#include <Windows.h>

#include <cmath>
#include <format>
#include <map>
#include <vector>

namespace mye {
namespace {

constexpr size_t kScopeCount = 4;

constexpr const char* ScopeName(ConfigScope scope) {
    switch (scope) {
        case ConfigScope::Engine:         return "Engine";
        case ConfigScope::Project:        return "Project";
        case ConfigScope::User:           return "User";
        case ConfigScope::RuntimeOverlay: return "RuntimeOverlay";
    }
    return "?";
}

// UTF-8 → UTF-16 로컬 헬퍼 — mye::Widen(App.cpp)을 쓰면 정적 링크 시 App.cpp가 딸려와
// CreateApplication 미정의 참조가 생긴다(mye_tests). 통합 단계에서 Widen이 독립 TU로
// 이동하면 교체 가능. (Log.cpp에도 동일 헬퍼 존재)
std::wstring ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                          static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), len);
    return out;
}

constexpr uint64_t kMaxConfigFileSize = 16ull * 1024 * 1024;   // 설정 파일 안전장치 16MB

// 파일 전체 읽기 (UTF-8, BOM 제거). 반환 false + notFound=true는 "파일 없음"(에러 아님).
bool ReadFileBytes(std::string_view utf8Path, std::string& out, bool& notFound, Error& err) {
    notFound = false;
    const std::wstring wide = ToWide(utf8Path);
    HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD lastError = ::GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND) {
            notFound = true;
            return false;
        }
        err = Error{std::format("failed to open '{}' (win32 error {})", utf8Path, lastError),
                    static_cast<int32_t>(lastError)};
        return false;
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size) ||
        static_cast<uint64_t>(size.QuadPart) > kMaxConfigFileSize) {
        ::CloseHandle(file);
        err = Error{std::format("'{}' is too large for a config file (limit {} bytes)",
                                utf8Path, kMaxConfigFileSize), 1};
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t total = 0;
    while (total < out.size()) {
        DWORD read = 0;
        if (!::ReadFile(file, out.data() + total,
                        static_cast<DWORD>(out.size() - total), &read, nullptr) || read == 0) {
            const DWORD lastError = ::GetLastError();
            ::CloseHandle(file);
            err = Error{std::format("failed to read '{}' (win32 error {})", utf8Path, lastError),
                        static_cast<int32_t>(lastError)};
            return false;
        }
        total += read;
    }
    ::CloseHandle(file);
    if (out.starts_with("\xEF\xBB\xBF")) out.erase(0, 3);   // UTF-8 BOM
    return true;
}

void CreateParentDirectories(std::wstring_view widePath) {
    for (size_t i = 1; i < widePath.size(); ++i) {
        const wchar_t c = widePath[i];
        if (c != L'/' && c != L'\\') continue;
        std::wstring dir(widePath.substr(0, i));
        if (dir.size() == 2 && dir[1] == L':') continue;   // 드라이브 루트
        ::CreateDirectoryW(dir.c_str(), nullptr);          // 실패(이미 존재)는 무시
    }
}

bool WriteFileBytes(std::string_view utf8Path, std::string_view bytes, Error& err) {
    const std::wstring wide = ToWide(utf8Path);
    CreateParentDirectories(wide);
    HANDLE file = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD lastError = ::GetLastError();
        err = Error{std::format("failed to create '{}' (win32 error {})", utf8Path, lastError),
                    static_cast<int32_t>(lastError)};
        return false;
    }
    size_t total = 0;
    while (total < bytes.size()) {
        DWORD written = 0;
        if (!::WriteFile(file, bytes.data() + total,
                         static_cast<DWORD>(bytes.size() - total), &written, nullptr)) {
            const DWORD lastError = ::GetLastError();
            ::CloseHandle(file);
            err = Error{std::format("failed to write '{}' (win32 error {})", utf8Path, lastError),
                        static_cast<int32_t>(lastError)};
            return false;
        }
        total += written;
    }
    ::CloseHandle(file);
    return true;
}

// ---------------------------------------------------------------------------
// json::Value ↔ ConfigValue 변환
// ---------------------------------------------------------------------------
ConfigValue JsonToConfig(const json::Value& v) {
    ConfigValue out;
    switch (v.GetType()) {
        case json::Type::Null: break;   // monostate
        case json::Type::Bool: out.value = v.AsBool(); break;
        case json::Type::Number: {
            const double d = v.AsDouble();
            // 정수값은 int64로 보존 (2^53 이하 — double 정밀 정수 범위)
            if (d == std::floor(d) && std::abs(d) <= 9007199254740992.0)
                out.value = static_cast<int64_t>(d);
            else
                out.value = d;
            break;
        }
        case json::Type::String: out.value = std::string(v.AsString()); break;
        case json::Type::Array: {
            ConfigValue::Array arr;
            arr.reserve(v.AsArray().size());
            for (const auto& elem : v.AsArray())
                arr.push_back(JsonToConfig(elem));   // 배열 안 오브젝트는 monostate로 강등
            out.value = std::move(arr);
            break;
        }
        case json::Type::Object: break;   // 오브젝트는 호출 측에서 재귀 평탄화 — 여기 도달 시 monostate
    }
    return out;
}

json::Value ConfigToJson(const ConfigValue& v) {
    if (const bool* b = std::get_if<bool>(&v.value)) return json::Value(*b);
    if (const int64_t* i = std::get_if<int64_t>(&v.value)) return json::Value(*i);
    if (const double* d = std::get_if<double>(&v.value)) return json::Value(*d);
    if (const std::string* s = std::get_if<std::string>(&v.value)) return json::Value(*s);
    if (const ConfigValue::Array* a = std::get_if<ConfigValue::Array>(&v.value)) {
        json::Value::Array arr;
        arr.reserve(a->size());
        for (const auto& elem : *a) arr.push_back(ConfigToJson(elem));
        return json::Value(std::move(arr));
    }
    return json::Value();   // monostate → null
}

using FlatMap = std::map<std::string, ConfigValue, std::less<>>;

// 루트/섹션 오브젝트를 "section.key" 평탄 키로 병합 (중첩 오브젝트는 점 연결로 재귀)
void FlattenObject(FlatMap& dst, const std::string& prefix, const json::Value& obj) {
    for (const auto& [key, val] : obj.AsObject()) {
        std::string full = prefix.empty() ? key : prefix + "." + key;
        if (val.IsObject())
            FlattenObject(dst, full, val);
        else
            dst.insert_or_assign(std::move(full), JsonToConfig(val));
    }
}

// Save용: 평탄 키("a.b.c")를 중첩 JSON 오브젝트로 재조립.
// entries는 (남은 키, 값) — 맵 순회 순서(정렬)를 유지해 "seg." 프리픽스 블록이 연속임을 이용.
using SaveEntry = std::pair<std::string_view, const ConfigValue*>;

json::Value::Object BuildObject(const std::vector<SaveEntry>& entries) {
    json::Value::Object obj;
    size_t i = 0;
    while (i < entries.size()) {
        const std::string_view key = entries[i].first;
        const size_t dot = key.find('.');
        if (dot == std::string_view::npos) {
            obj.insert_or_assign(std::string(key), ConfigToJson(*entries[i].second));
            ++i;
            continue;
        }
        const std::string_view seg = key.substr(0, dot);
        std::vector<SaveEntry> sub;
        while (i < entries.size()) {
            const std::string_view k = entries[i].first;
            if (!(k.size() > seg.size() && k.substr(0, seg.size()) == seg &&
                  k[seg.size()] == '.'))
                break;
            sub.emplace_back(k.substr(seg.size() + 1), entries[i].second);
            ++i;
        }
        // 리프 "seg"와 중첩 "seg.x"가 공존하면 중첩이 승리
        obj.insert_or_assign(std::string(seg), json::Value(BuildObject(sub)));
    }
    return obj;
}

} // namespace

// ---------------------------------------------------------------------------
// ConfigSystem
// ---------------------------------------------------------------------------
struct ConfigSystem::Impl {
    // 스코프별 key("section.key") → ConfigValue 평탄 맵. 병합 조회는 역순(Overlay→Engine).
    FlatMap scopes[kScopeCount];
    std::map<std::string, ConfigScope, std::less<>> sections;
    std::string scopePaths[kScopeCount];   // Save 대상 경로 (LoadScopeFromFile이 기록)
    EventBus* bus = nullptr;               // 비소유 — GuardedMain이 주입

    const ConfigValue* FindMerged(std::string_view key) const {
        for (int s = static_cast<int>(kScopeCount) - 1; s >= 0; --s) {
            const auto it = scopes[s].find(key);
            if (it != scopes[s].end() &&
                !std::holds_alternative<std::monostate>(it->second.value))
                return &it->second;
        }
        return nullptr;
    }
};

ConfigSystem::ConfigSystem() : m_impl(std::make_unique<Impl>()) {}
ConfigSystem::~ConfigSystem() = default;

void ConfigSystem::SetEventBus(EventBus* bus) { m_impl->bus = bus; }

void ConfigSystem::RegisterSection(std::string_view section, ConfigScope defaultScope) {
    m_impl->sections.emplace(std::string(section), defaultScope);
}

Expected<void, Error> ConfigSystem::LoadScopeFromFile(ConfigScope scope,
                                                      std::string_view utf8Path) {
    const size_t idx = static_cast<size_t>(scope);
    MYE_ASSERT(idx < kScopeCount);
    m_impl->scopePaths[idx] = std::string(utf8Path);   // 파일이 아직 없어도 Save 대상 기억

    std::string text;
    bool notFound = false;
    Error err;
    if (!ReadFileBytes(utf8Path, text, notFound, err)) {
        if (notFound) {
            MYE_LOG_DEBUG("Config", "{} scope: '{}' not found (empty scope)",
                          ScopeName(scope), utf8Path);
            return {};   // 파일 없음 = 성공(빈 스코프)
        }
        return err;
    }

    auto parsed = json::Parse(text);
    if (!parsed)
        return Error{std::format("{}: {}", utf8Path, parsed.GetError().message),
                     parsed.GetError().code};

    const json::Value& root = parsed.Value();
    if (!root.IsObject())
        return Error{std::format("{}: config root must be a JSON object", utf8Path), 1};

    FlattenObject(m_impl->scopes[idx], std::string(), root);
    MYE_LOG_DEBUG("Config", "{} scope loaded from '{}' ({} keys total)",
                  ScopeName(scope), utf8Path, m_impl->scopes[idx].size());
    return {};
}

bool ConfigSystem::GetBool(std::string_view key, bool fallback) const {
    const ConfigValue* v = m_impl->FindMerged(key);
    if (!v) return fallback;
    if (const bool* b = std::get_if<bool>(&v->value)) return *b;
    return fallback;
}

int64_t ConfigSystem::GetInt(std::string_view key, int64_t fallback) const {
    const ConfigValue* v = m_impl->FindMerged(key);
    if (!v) return fallback;
    if (const int64_t* i = std::get_if<int64_t>(&v->value)) return *i;
    if (const double* d = std::get_if<double>(&v->value)) return static_cast<int64_t>(*d);
    return fallback;
}

double ConfigSystem::GetFloat(std::string_view key, double fallback) const {
    const ConfigValue* v = m_impl->FindMerged(key);
    if (!v) return fallback;
    if (const double* d = std::get_if<double>(&v->value)) return *d;
    if (const int64_t* i = std::get_if<int64_t>(&v->value)) return static_cast<double>(*i);
    return fallback;
}

std::string ConfigSystem::GetString(std::string_view key, std::string_view fallback) const {
    const ConfigValue* v = m_impl->FindMerged(key);
    if (!v) return std::string(fallback);
    if (const std::string* s = std::get_if<std::string>(&v->value)) return *s;
    return std::string(fallback);
}

void ConfigSystem::Set(std::string_view key, ConfigValue value, ConfigScope scope) {
    MYE_ASSERT(scope != ConfigScope::Engine, "Engine 스코프는 읽기 전용 — Set 금지");
    if (scope == ConfigScope::Engine) return;   // Release에서도 무시 (계약 유지)

    auto& map = m_impl->scopes[static_cast<size_t>(scope)];
    const auto it = map.insert_or_assign(std::string(key), std::move(value)).first;
    if (m_impl->bus) {
        ConfigChangedEvent e{};
        e.key = it->first;   // 맵 키 스토리지 — 동기 Publish 동안 유효
        e.scope = scope;
        m_impl->bus->Publish(e);
    }
}

Expected<void, Error> ConfigSystem::Save(ConfigScope scope) {
    if (scope == ConfigScope::Engine)
        return Error{"Engine scope is read-only and cannot be saved", 1};
    if (scope == ConfigScope::RuntimeOverlay)
        return {};   // 런타임 오버레이(CVar)는 저장하지 않음 — 무시(성공)

    const size_t idx = static_cast<size_t>(scope);
    const std::string& path = m_impl->scopePaths[idx];
    if (path.empty())
        return Error{std::format("{} scope has no file path (call LoadScopeFromFile first)",
                                 ScopeName(scope)), 1};

    std::vector<SaveEntry> entries;
    entries.reserve(m_impl->scopes[idx].size());
    for (const auto& [key, value] : m_impl->scopes[idx])
        entries.emplace_back(std::string_view(key), &value);

    std::string text = json::Stringify(json::Value(BuildObject(entries)), 2);
    text.push_back('\n');

    Error err;
    if (!WriteFileBytes(path, text, err)) return err;
    MYE_LOG_DEBUG("Config", "{} scope saved to '{}'", ScopeName(scope), path);
    return {};
}

} // namespace mye
