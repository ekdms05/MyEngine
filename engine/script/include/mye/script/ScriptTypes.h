// Script runtime value types, events, assets, and callback names.
//
// 이 헤더는 바인딩/Lua 를 포함하지 않는 "가벼운" 표면이다 — ScriptErrorEvent(월드 버스 발행),
// ScriptAsset(.lua 에셋), 콜백 이름 상수 등 바인딩 세부에 의존하지 않는 계약을 모은다.
// VM·클래스 테이블·콜백 을 다루는 표면은 ScriptRuntime.h/
// ScriptComponent.h 로 분리한다(무거운 LuaApi.h 인클루드 격리).
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
// ScriptErrorEvent — 스크립트 런타임 에러를 월드-로컬 EventBus 로 발행.
// ---------------------------------------------------------------------------
// A failed lifecycle callback stops its component. A failed coroutine stops that
// task. Both publish diagnostics while unrelated scripts continue. Strings need
// Publish (immediate dispatch), not Enqueue (byte copy).
struct ScriptErrorEvent {
    MYE_EVENT(mye::script::ScriptErrorEvent);

    ecs::Entity entity;              // 에러가 난 스크립트 엔티티(전역 에러면 Null)
    std::string file;                // .lua vpath (예: "scripts/entities/npc.lua")
    int32_t     line = 0;            // 에러 라인(파싱 불가 시 0)
    std::string message;             // traceback 포함 메시지(UTF-8)
    std::string callback;            // 실패한 콜백 이름("on_update" 등, 있으면)
};

// ---------------------------------------------------------------------------
// ScriptAsset — .lua 소스 에셋(AssetManager 가 임포트·핫 리로드). 데이터만 소유.
// ---------------------------------------------------------------------------
// Compile UTF-8 text when the runtime loads a chunk; no precompiled bytecode is stored.
struct ScriptAsset {
    MYE_ASSET_TYPE(mye::asset::ScriptAsset);   // AssetTypeId 부여(04 규약과 동일 매크로)

    std::string sourcePath;          // VFS path used in diagnostics.
    std::string source;              // .lua 원문(UTF-8)
    uint64_t    sourceHash = 0;      // 소스 FNV-1a 해시(변경 감지·리로드 판정)
    uint32_t    version = 1;
};

// ---------------------------------------------------------------------------
// 라이프사이클 콜백 이름 상수(snake_case, docs/19-lua-api.md).
// ---------------------------------------------------------------------------
// 정의된 콜백만 디스패치 대상에 올려 미정의 콜백의 호출 비용을 0으로 만든다(성능 가이드).
namespace callbacks {
inline constexpr std::string_view kOnInit        = "on_init";          // 씬 시작/스폰 직후 1회
inline constexpr std::string_view kOnStart       = "on_start";         // 첫 update 직전 1회
inline constexpr std::string_view kOnUpdate      = "on_update";        // 매 시뮬레이션 틱(dt 초)
inline constexpr std::string_view kOnLateUpdate  = "on_late_update";   // 같은 틱, update 이후
inline constexpr std::string_view kOnEvent       = "on_event";         // (name, payload)
inline constexpr std::string_view kOnTriggerEnter= "on_trigger_enter"; // (other)
inline constexpr std::string_view kOnTriggerExit = "on_trigger_exit";  // (other)
inline constexpr std::string_view kOnHotReload   = "on_hot_reload";    // 핫 리로드 후(on_init 대체 아님)
inline constexpr std::string_view kOnDestroy     = "on_destroy";       // 파괴·컴포넌트 제거를 다음 경계에서 감지
} // namespace callbacks

} // namespace mye::script
