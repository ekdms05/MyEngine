// mye/core/Config.h — 계층 설정 시스템 (docs/01)
//
// 스코프(뒤가 앞을 오버라이드): Engine < Project < User < RuntimeOverlay
// M0 구현 범위: Engine + Project 2스코프 로드·병합 + Save(User는 선언만, 구현 M1+).
// 키는 "section.key" 경로 문자열. 값 타입은 bool/int64/double/string/배열 최소 집합.
#pragma once

#include "mye/core/Base.h"

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mye {

class EventBus;   // Set() 시 ConfigChangedEvent Publish 용 (주입: SetEventBus)

enum class ConfigScope : uint8_t { Engine, Project, User, RuntimeOverlay };

struct ConfigValue {
    // 재귀 배열: vector<ConfigValue>는 불완전 타입 허용(C++17+)
    using Array = std::vector<ConfigValue>;
    std::variant<std::monostate, bool, int64_t, double, std::string, Array> value;
};

struct ConfigChangedEvent {
    MYE_EVENT(ConfigChangedEvent);
    std::string_view key;    // "section.key"
    ConfigScope      scope;
};

class ConfigSystem {
public:
    MYE_SERVICE(ConfigSystem);

    ConfigSystem();
    ~ConfigSystem();
    ConfigSystem(const ConfigSystem&) = delete;
    ConfigSystem& operator=(const ConfigSystem&) = delete;

    // GuardedMain이 부팅 시 주입 — 이후 Set()이 ConfigChangedEvent를 즉시 Publish한다.
    // nullptr 허용(버스 미연결 시 이벤트 생략). 소유권 없음.
    void SetEventBus(EventBus* bus);

    // 모듈·플러그인이 자기 섹션을 선언 (기본 저장 스코프 지정)
    void RegisterSection(std::string_view section, ConfigScope defaultScope);

    // 파일 로드: 해당 스코프에 JSON 병합. 파일 없음은 성공(빈 스코프) 취급.
    Expected<void, Error> LoadScopeFromFile(ConfigScope scope, std::string_view utf8Path);

    // 병합된 뷰 조회 (RuntimeOverlay > User > Project > Engine)
    bool        GetBool  (std::string_view key, bool fallback) const;
    int64_t     GetInt   (std::string_view key, int64_t fallback) const;
    double      GetFloat (std::string_view key, double fallback) const;
    std::string GetString(std::string_view key, std::string_view fallback) const;

    // 설정 + ConfigChangedEvent 발행(버스 연결 시). Engine 스코프에 Set 금지(어서트).
    void Set(std::string_view key, ConfigValue value, ConfigScope scope);

    // 스코프를 파일로 저장. Engine은 읽기 전용(실패), RuntimeOverlay는 저장 안 함(무시).
    Expected<void, Error> Save(ConfigScope scope);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye
