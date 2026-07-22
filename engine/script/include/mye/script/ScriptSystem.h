// mye/script/ScriptSystem.h — ScriptComponent 실행·에러 격리·핫 리로드 (docs/05 §ScriptSystem, M3-C)
//
// docs/05 §ScriptSystem: 게임 Lua 스테이트(ScriptRuntime)를 배선하고, 03 시스템 스케줄의
//   Update 페이즈에서 ScriptComponent 콜백을 실행한다. 핵심 책임:
//     - 인스턴스화: 클래스 테이블(스크립트가 return)로 인스턴스 self 를 만들고 callbacks 비트 채움.
//     - 콜백 디스패치: on_init/on_start/on_update/on_late_update/on_event/on_trigger_enter/exit —
//       전부 sol::protected_function. 정의된 콜백만 호출(callbacks 비트 게이트).
//     - 에러 격리(M3 완료 기준): 콜백/코루틴 재개 실패 시 해당 컴포넌트만 hasError=true 정지 +
//       월드 버스로 ScriptErrorEvent 발행. 엔진·다른 엔티티는 계속 진행.
//     - 핫 리로드: .lua AssetReloadedEvent 구독 → 클래스 테이블 교체, 인스턴스 self.state 생존,
//       on_init 재호출 없이 on_hot_reload 호출(정의돼 있으면). 성공 시 hasError 해제.
//     - 코루틴: ScriptRuntime 의 CoroutineScheduler tick(wait_seconds/wait_event).
//
// 이 헤더는 ScriptComponent(sol.hpp 보유)를 다루므로 <sol/sol.hpp> 를 간접 포함한다 —
//   ScriptSystem 을 포함하는 TU(데모 배선·ScriptSystem.cpp)를 최소화한다.
#pragma once

#include "mye/ecs/Entity.h"
#include "mye/script/ScriptComponent.h"
#include "mye/script/ScriptTypes.h"

#include <memory>

namespace mye {
class EventBus;
struct TimeStep;
namespace ecs { class World; class CommandBuffer; }
namespace scene { class SystemScheduler; }
namespace asset { class AssetManager; }
}

namespace mye::script {

class ScriptRuntime;

// ---------------------------------------------------------------------------
// ScriptSystem — ScriptComponent 실행기. World·ScriptRuntime·월드 버스를 배선한다.
// ---------------------------------------------------------------------------
// 수명: 게임 스테이트(ScriptRuntime)와 함께 프로세스 수명(M3-C 런타임). 에디터 Play/Stop
//   이중 World 격리(07)는 후속. World 는 비소유(SceneModule 소유), ScriptRuntime 도 비소유
//   가능(호출자 소유) 또는 내부 소유 — 생성자에서 선택.
class ScriptSystem {
public:
    // runtime·world·worldBus 는 비소유(호출자 수명 보장). worldBus 는 World::Events() 를
    //   그대로 넘긴다(트리거·애니 이벤트 구독 + ScriptErrorEvent 발행). assets 는 핫 리로드
    //   구독·스크립트 소스 조회용(nullptr 허용=테스트).
    ScriptSystem(ScriptRuntime& runtime, ecs::World& world, EventBus* worldBus,
                 asset::AssetManager* assets);
    ~ScriptSystem();
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    // ECS 에 ScriptComponent 타입 등록 + 월드 버스 이벤트 구독(TriggerEnter/Exit,
    //   AnimationEvent, AssetReloadedEvent). App/데모 배선부가 1회 호출.
    void RegisterComponent();

    // 03 스케줄러의 Update 페이즈에 이 시스템을 등록한다(SystemDesc 이름 "mye.ScriptSystem").
    //   내부적으로 Update 콜백에서 EnsureInstances → on_update → 코루틴 tick 순으로 실행.
    void RegisterInto(scene::SystemScheduler& scheduler);

    // ---- 라이프사이클(수동 호출 경로 — 스케줄러 없이 테스트·부트스트랩용) ----
    // 아직 인스턴스화되지 않은 live ScriptComponent 를 인스턴스화하고 on_init 를 호출한다.
    //   (스폰 직후·씬 시작 시 새로 생긴 컴포넌트를 매 프레임 흡수.)
    void EnsureInstances();
    // 전 엔티티 on_start(첫 update 직전 1회)·on_update(dt)·on_late_update(dt) 디스패치 +
    //   코루틴 스케줄러 tick. 스케줄러 경로(RegisterInto)와 동일 본체.
    void Update(float dt);

    // ---- 이벤트 라우팅(월드 버스 구독 핸들러가 내부 호출) ----
    // 이름 있는 게임 이벤트를 on_event(name, payload) 로 라우팅 + 코루틴 wait_event 통지.
    void DispatchEvent(std::string_view name, const sol::object& payload);
    // 03 트리거 이벤트 → on_trigger_enter/exit(other) 라우팅.
    void OnTriggerEnter(ecs::Entity self, ecs::Entity other);
    void OnTriggerExit(ecs::Entity self, ecs::Entity other);

    // ---- 핫 리로드 ----
    // .lua 재임포트(AssetReloadedEvent) 시 호출 — 해당 에셋을 쓰는 모든 ScriptComponent 의
    //   클래스 테이블을 새 청크로 교체하고 self.state 를 보존한 채 on_hot_reload 호출.
    //   클래스 재로드 실패(문법 에러) 시 기존 클래스 유지 + ScriptErrorEvent 발행(게임 지속).
    void HotReload(asset::AssetGuid scriptGuid);

    // 임의 커스텀 콜백 호출(docs/05 §확장: ScriptSystem::callOnEntity). protected 호출.
    //   정의돼 있지 않거나 컴포넌트가 not-live 면 무시. 게임 정의 on_x 라우팅용.
    void CallOnEntity(ecs::Entity e, std::string_view callback, const sol::object& arg);

private:
    // 한 컴포넌트의 콜백을 protected 호출하고, 실패 시 hasError=true + ScriptErrorEvent 발행.
    //   반환 false = 에러 발생(정지). callbackName 은 진단·이벤트 페이로드용.
    bool InvokeGuarded(ecs::Entity e, ScriptComponent& sc, std::string_view callbackName,
                       const sol::protected_function& fn);

    // 추적 중인 스크립트 엔티티 중 파괴된(더 이상 live ScriptComponent 없음) 것을 감지해
    //   on_destroy 를 호출하고 소유 코루틴을 취소한다(docs/05: 파괴 시 자동 취소). 프레임 경계에서
    //   호출(순회 밖).
    void ReconcileDestroyed();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::script
