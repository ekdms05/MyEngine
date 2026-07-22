// mye/script/ScriptComponent.h — Lua 스크립트 ECS 컴포넌트 (docs/05 §ScriptComponent, M3-C)
//
// docs/05: ScriptComponent 는 편집 중에는 데이터(script 핸들·properties)만, Play 중에는 Lua
//   인스턴스 테이블을 갖는다. M3-C(런타임)는 게임 스테이트가 프로세스 수명이므로 인스턴스
//   테이블을 바로 실체화한다(에디터 플레이 모드 이중 World 격리는 07, 후속).
//
// 이 헤더는 sol::table(인스턴스·클래스·self.state)을 값으로 보유하므로 <sol/sol.hpp> 를
//   직접 인클루드한다. sol 이 무거우므로 ScriptComponent 를 포함하는 TU 를 최소화한다
//   (ScriptSystem.cpp·데모 배선부만).
#pragma once

#include "mye/asset/AssetHandle.h"
#include "mye/ecs/ComponentType.h"
#include "mye/script/ScriptTypes.h"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>

namespace mye::script {

// 정의된 콜백을 비트마스크로 캐시 — 미정의 콜백 디스패치 비용 0(docs/05 성능 가이드).
//   ScriptSystem 이 인스턴스화 시 클래스 테이블을 훑어 present 비트를 채운다.
enum class CallbackBit : uint32_t {
    None          = 0,
    OnInit        = 1u << 0,
    OnStart       = 1u << 1,
    OnUpdate      = 1u << 2,
    OnLateUpdate  = 1u << 3,
    OnEvent       = 1u << 4,
    OnTriggerEnter= 1u << 5,
    OnTriggerExit = 1u << 6,
    OnHotReload   = 1u << 7,
    OnDestroy     = 1u << 8,
};
inline constexpr uint32_t operator|(CallbackBit a, CallbackBit b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}

// 인스펙터 노출 프로퍼티 값 백(docs/05 §props). M3-C 는 최소 표현(직렬화·스키마 유도는 07/04).
//   스크립트의 props 선언(mye.prop.*)에서 스키마를 유도하고 에디터가 편집한다 — 여기선 데이터만.
struct ScriptPropertyBag {
    // 런타임 표현: 인스턴스 self 에 주입할 초기값 테이블(키→Lua 값). M3-C 최소 골격은 비어있다.
    sol::table values;               // 유효하지 않으면(=nil) 프로퍼티 주입 생략
};

// ---------------------------------------------------------------------------
// ScriptComponent — ECS 컴포넌트. 데이터(에셋 핸들·프로퍼티) + 런타임 Lua 인스턴스.
// ---------------------------------------------------------------------------
struct ScriptComponent {
    MYE_COMPONENT(ScriptComponent);

    // ---- 편집·직렬화 데이터 ----
    asset::AssetHandle<ScriptAsset> script;   // 04 가 로드하는 .lua 에셋
    ScriptPropertyBag               properties;
    bool enabled = true;

    // ---- 런타임 전용(직렬화 제외) ----
    // 클래스 테이블(스크립트가 return 한 테이블 — 함수들). 핫 리로드 시 이 참조를 교체.
    sol::table classTable;
    // 인스턴스 테이블(setmetatable(instance, {__index=classTable})). self 로 콜백에 전달.
    //   self.state 는 핫 리로드에서 생존하는 데이터 영역(docs/05 §핫 리로드).
    sol::table instance;

    // 정의된 콜백 present 비트(CallbackBit OR). 0 이면 디스패치 대상 아님.
    uint32_t   callbacks = 0;

    bool hasError = false;           // 에러 시 실행 중지 플래그(핫 리로드 성공 시 해제)
    bool started = false;            // on_start 1회 발행 여부

    bool HasCallback(CallbackBit b) const {
        return (callbacks & static_cast<uint32_t>(b)) != 0;
    }
    bool IsLive() const { return enabled && !hasError && instance.valid(); }
};

} // namespace mye::script
