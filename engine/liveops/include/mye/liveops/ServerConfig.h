// mye/liveops/ServerConfig.h — 서버 CVar·피처플래그·점검모드 (docs/mmorpg/09, M11)
//
// 서버 운영값을 코드 재배포 없이 파일로 조정한다. 타입드 CVar(정수/실수/불리언/문자열),
// 온오프 피처플래그, 점검모드. LoadFromFile 로 핫 리로드(파일만 바꾸면 다음 로드에서 반영).
// 예) drop_rate_mult, tickrate, max_players, feature "double_xp", "maintenance".
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Json.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mye::liveops {

class ServerConfig {
public:
    // ---- 타입드 CVar 조회(없으면 fallback) ----
    int64_t     GetInt(std::string_view key, int64_t fallback = 0) const;
    double      GetFloat(std::string_view key, double fallback = 0.0) const;
    bool        GetBool(std::string_view key, bool fallback = false) const;
    std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    bool        Has(std::string_view key) const;

    // ---- CVar 설정(런타임 조정) ----
    void SetInt(std::string_view key, int64_t v);
    void SetFloat(std::string_view key, double v);
    void SetBool(std::string_view key, bool v);
    void SetString(std::string_view key, std::string_view v);

    // ---- 피처 플래그 ----
    bool HasFlag(std::string_view name) const;   // 없으면 false
    void SetFlag(std::string_view name, bool on);
    bool MaintenanceMode() const { return HasFlag("maintenance"); }

    size_t CVarCount() const { return m_values.size(); }
    size_t FlagCount() const { return m_flags.size(); }

    // ---- 영속/핫 리로드 ----
    // { "cvars": { key: value... }, "flags": { name: bool... } } 형식.
    Expected<void, Error> LoadFromFile(std::string_view path);   // 기존 값 대체(핫 리로드)
    Expected<void, Error> SaveToFile(std::string_view path) const;

private:
    json::Value::Object                 m_values;   // CVar(숫자/문자열/불리언)
    std::unordered_map<std::string, bool> m_flags;  // 피처 플래그
};

} // namespace mye::liveops
