// mye/scene/SceneModule.cpp — 씬 모듈 구현 (docs/03 §2)
#include "mye/scene/SceneModule.h"

#include "mye/core/Events.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/scene/Transform.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"

namespace mye::scene {

// 의존성: 코어 서브시스템(EventBus/JobSystem)은 서비스이므로 모듈 이름 의존은 최소.
// 잡·에셋 모듈이 별도 IModule로 존재하면 여기에 이름을 추가한다(M2-A: 코어만 → 빈 목록).

struct SceneModule::Impl {
    std::unique_ptr<ecs::World>      world;
    std::unique_ptr<EventBus>        worldBus;   // 월드-로컬 게임플레이 이벤트 버스
    std::unique_ptr<SystemScheduler> scheduler;
    ModuleRegistry* modules = nullptr;
};

SceneModule::SceneModule() : m_impl(std::make_unique<Impl>()) {}
SceneModule::~SceneModule() = default;

std::span<const char* const> SceneModule::GetDependencies() const {
    return {};
}

void SceneModule::OnLoad(EngineContext& ctx) {
    // 서비스 등록만(타 모듈 접근 금지).
    ctx.RegisterServiceRaw(kServiceId, this);
}

void SceneModule::OnInitialize(EngineContext& /*ctx*/) {
    // CommandBuffer::SetParent(Flush)가 Transform reparent로 이어지도록 후크 설치
    // (정적 초기화에 의존하지 않는 명시 배선 — 링커 stripping 방어).
    ecs::CommandBuffer::SetReparentHook(&ApplyReparent);

    m_impl->world = std::make_unique<ecs::World>();
    m_impl->worldBus = std::make_unique<EventBus>();
    m_impl->world->SetEventBus(m_impl->worldBus.get());
    m_impl->scheduler = std::make_unique<SystemScheduler>(*m_impl->world, m_impl->worldBus.get());

    // TransformSystem을 PostUpdate에 기본 등록(월드 행렬 갱신).
    SystemDesc ts;
    ts.name = "TransformSystem";
    ts.phase = Phase::PostUpdate;
    ts.writes = { WorldTransform::kComponentTypeId };
    ts.reads  = { LocalTransform::kComponentTypeId, Parent::kComponentTypeId };
    ts.fn = [](ecs::World& w, ecs::CommandBuffer&, const TimeStep&) {
        UpdateWorldTransforms(w);
    };
    m_impl->scheduler->RegisterSystem(std::move(ts));
}

void SceneModule::OnPostInitialize(EngineContext& ctx) {
    // 5페이즈를 01 UpdatePhase 틱에 연결(docs/03 §2 매핑).
    m_impl->modules = &ctx.Modules();
    SystemScheduler* sched = m_impl->scheduler.get();

    m_impl->modules->AddTick(this, UpdatePhase::PreUpdate,
        [sched](const TimeStep& t) { sched->RunPhase(Phase::Input, t); });
    m_impl->modules->AddTick(this, UpdatePhase::FixedUpdate,
        [sched](const TimeStep& t) { sched->RunPhase(Phase::FixedUpdate, t); });
    m_impl->modules->AddTick(this, UpdatePhase::Update,
        [sched](const TimeStep& t) { sched->RunPhase(Phase::Update, t); });
    m_impl->modules->AddTick(this, UpdatePhase::PostUpdate,
        [sched](const TimeStep& t) { sched->RunPhase(Phase::PostUpdate, t); });
    // RenderExtract는 PreRender 초입 — orderKey를 작게 주어 02 렌더 틱보다 먼저 실행.
    m_impl->modules->AddTick(this, UpdatePhase::PreRender,
        [sched](const TimeStep& t) { sched->RunPhase(Phase::RenderExtract, t); }, /*orderKey*/ -1000);
}

void SceneModule::OnShutdown(EngineContext& ctx) {
    ctx.UnregisterServiceRaw(kServiceId);
    m_impl->scheduler.reset();
    m_impl->world.reset();
    m_impl->worldBus.reset();
}

ecs::World&      SceneModule::World()       { return *m_impl->world; }
SystemScheduler& SceneModule::Scheduler()   { return *m_impl->scheduler; }
EventBus&        SceneModule::WorldEvents()  { return *m_impl->worldBus; }

} // namespace mye::scene
