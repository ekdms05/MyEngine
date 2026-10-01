// Lua VM ownership and protected game scripting.
#pragma once

#include "mye/asset/AssetHandle.h"
#include "mye/ecs/ComponentType.h"
#include "mye/script/ScriptTypes.h"

#include "mye/script/LuaApi.h"

#include <cstdint>
#include <string>

namespace mye::script {

// 정의된 콜백을 비트마스크로 캐시 — 미정의 콜백 디스패치 비용 0.
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

//   스크립트의 props 선언(mye.prop.*)에서 스키마를 유도하고 에디터가 편집한다 — 여기선 데이터만.
struct ScriptPropertyBag {
    LuaReference values;               // 유효하지 않으면(=nil) 프로퍼티 주입 생략
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
    // Scene-owned source uses the same protected class/callback path as .lua assets.
    std::string inlineSource;

    // ---- 런타임 전용(직렬화 제외) ----
    // 클래스 테이블(스크립트가 return 한 테이블 — 함수들). 핫 리로드 시 이 참조를 교체.
    LuaReference classTable;
    // 인스턴스 테이블(setmetatable(instance, {__index=classTable})). self 로 콜백에 전달.
    //   self.state 는 핫 리로드에서 생존하는 데이터 영역.
    LuaReference instance;

    // 정의된 콜백 present 비트(CallbackBit OR). 0 이면 디스패치 대상 아님.
    uint32_t   callbacks = 0;

    bool hasError = false;           // 에러 시 실행 중지 플래그(핫 리로드 성공 시 해제)
    bool started = false;            // on_start 1회 발행 여부

    bool HasCallback(CallbackBit b) const {
        return (callbacks & static_cast<uint32_t>(b)) != 0;
    }
    bool IsLive() const { return enabled && !hasError && instance.Valid(); }
};

} // namespace mye::script
