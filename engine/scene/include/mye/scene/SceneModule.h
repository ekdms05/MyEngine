// mye/scene/SceneModule.h — 코어 Module/App 페이즈에 얹는 씬 모듈 (docs/03 §2 매핑)
//
// 01 IModule로 등록되어, 5페이즈 SystemScheduler를 01 UpdatePhase 틱에 하위 스케줄러로
// 연결한다. 페이즈 매핑(docs/03 §2):
//   Input        → PreUpdate   (전역 버스 Flush 이후)
//   FixedUpdate  → FixedUpdate
//   Update       → Update
//   PostUpdate   → PostUpdate  (TransformSystem 등)
//   RenderExtract→ PreRender   (초입, 02 렌더보다 먼저)
//
// active World의 월드-로컬 EventBus를 소유·배선하고, TransformSystem을 PostUpdate에 기본
// 등록한다. RenderExtract에서 02로 프록시를 밀어주는 실제 추출은 후속(M2-B) — 여기서는
// 스케줄러 골격과 페이즈 배선만 확립한다.
#pragma once

#include "mye/core/Module.h"

#include <memory>

namespace mye { class EventBus; }
namespace mye::ecs { class World; }

namespace mye::scene {

class SystemScheduler;

class SceneModule : public IModule {
public:
    MYE_SERVICE(SceneModule);

    SceneModule();
    ~SceneModule() override;

    const char* GetName() const override { return "SceneModule"; }
    // 잡·에셋보다 뒤, 렌더보다 앞. 코어(EventBus/JobSystem) 의존.
    std::span<const char* const> GetDependencies() const override;

    void OnLoad(EngineContext& ctx) override;          // 서비스 등록만
    void OnInitialize(EngineContext& ctx) override;    // World·Scheduler 생성, 버스 배선
    void OnPostInitialize(EngineContext& ctx) override;// 페이즈 틱 등록(AddTick)
    void OnShutdown(EngineContext& ctx) override;

    // active World 접근(07 플레이 모드 이중 World 대비 — M2-A는 단일 World).
    ecs::World&      World();
    SystemScheduler& Scheduler();
    EventBus&        WorldEvents();     // 월드-로컬 버스(게임플레이 이벤트)

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::scene
