// mye/script/ScriptTypes.h — 스크립트 런타임 공용 타입·이벤트·에셋 (docs/05, M3-C)
//
// 이 헤더는 sol2/Lua 를 포함하지 않는 "가벼운" 표면이다 — ScriptErrorEvent(월드 버스 발행),
// ScriptAsset(.lua 에셋), 콜백 이름 상수 등 sol2 세부에 의존하지 않는 계약을 모은다.
// sol::state·sol::table·sol::protected_function 을 다루는 표면은 ScriptRuntime.h/
// ScriptComponent.h 로 분리한다(무거운 sol.hpp 인클루드 격리).
#pragma once

#include "mye/asset/AssetGuid.h"  // MYE_ASSET_TYPE, AssetTypeId
#include "mye/core/Base.h"        // MYE_EVENT, EventTypeId, Expected, Error
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mye::script {

// ---------------------------------------------------------------------------
// ScriptErrorEvent — 스크립트 런타임 에러를 월드-로컬 EventBus 로 발행(docs/05 §에러 격리).
// ---------------------------------------------------------------------------
// 콜백/코루틴 재개가 protected_function 실패로 끝나면 ScriptSystem 이 해당 컴포넌트를
//   hasError=true 로 정지시키고 이 이벤트를 발행한다(엔진은 계속 — M3 완료 기준).
//   07 에디터 콘솔이 파일·라인 링크로 표시. NOTE: trivially copyable 이 아니다(std::string)
//   → Publish(즉시 디스패치)만 사용하고 Enqueue(바이트 복사)는 쓰지 않는다.
struct ScriptErrorEvent {
    MYE_EVENT(mye::script::ScriptErrorEvent);

    ecs::Entity entity;              // 에러가 난 스크립트 엔티티(전역 에러면 Null)
    std::string file;                // .lua vpath (예: "scripts/entities/npc.lua")
    int32_t     line = 0;            // 에러 라인(파싱 불가 시 0)
    std::string message;             // traceback 포함 메시지(UTF-8)
    std::string callback;            // 실패한 콜백 이름("on_update" 등, 있으면)
};

// ---------------------------------------------------------------------------
// ScriptAsset — .lua 소스 에셋(04 AssetManager 가 임포트·핫 리로드). 데이터만 소유.
// ---------------------------------------------------------------------------
// docs/05: ".lua 를 ScriptAsset 으로 취급, 핫 리로드는 04 파일 워처 이벤트에 반응."
//   런타임 컴파일은 ScriptRuntime 이 소스 문자열을 sol 로 로드하는 시점에 수행한다
//   (M3-C 는 원문 임베드 — 바이트코드 프리컴파일은 오픈이슈 #5, 후속).
struct ScriptAsset {
    MYE_ASSET_TYPE(mye::asset::ScriptAsset);   // AssetTypeId 부여(04 규약과 동일 매크로)

    std::string sourcePath;          // VFS vpath(진단·require 경로 해석용)
    std::string source;              // .lua 원문(UTF-8)
    uint64_t    sourceHash = 0;      // 소스 FNV-1a 해시(변경 감지·리로드 판정)
    uint32_t    version = 1;
};

// ---------------------------------------------------------------------------
// 라이프사이클 콜백 이름 상수(snake_case, docs/05 §Lua 스크립트 규약).
// ---------------------------------------------------------------------------
// 정의된 콜백만 디스패치 대상에 올려 미정의 콜백의 호출 비용을 0으로 만든다(성능 가이드).
namespace callbacks {
inline constexpr std::string_view kOnInit        = "on_init";          // 씬 시작/스폰 직후 1회
inline constexpr std::string_view kOnStart       = "on_start";         // 첫 update 직전 1회
inline constexpr std::string_view kOnUpdate      = "on_update";        // 매 프레임(dt)
inline constexpr std::string_view kOnLateUpdate  = "on_late_update";   // 매 프레임(dt), update 이후
inline constexpr std::string_view kOnEvent       = "on_event";         // (name, payload)
inline constexpr std::string_view kOnTriggerEnter= "on_trigger_enter"; // (other)
inline constexpr std::string_view kOnTriggerExit = "on_trigger_exit";  // (other)
inline constexpr std::string_view kOnHotReload   = "on_hot_reload";    // 핫 리로드 후(on_init 대체 아님)
inline constexpr std::string_view kOnDestroy     = "on_destroy";       // 엔티티 파괴 직전
} // namespace callbacks

} // namespace mye::script
