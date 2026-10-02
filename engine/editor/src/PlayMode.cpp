// PlayMode.cpp — 에디트↔플레이 모드 컨트롤러 (docs/07 §3)
//
// 07 §3: Play=편집 World를 인메모리 스냅샷(04 직렬화)으로 떠서 별도 Play World로 역직렬화,
//   Stop=Play World 파기 + 편집 World 재표시(스냅샷 복원이 아니라 단순 파기). 편집 World는
//   Play 내내 보존된다. 플레이 중 편집은 Play World에만 적용되고 Stop 시 휘발(별도 Undo 스택도
//   통째로 파기). Pause/Resume/StepFrame 상태 전이. ActiveWorld가 표시 대상 라우팅의 정본.
#include "mye/editor/PlayMode.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/core/Events.h"
#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/runtime/SceneTransition.h"
#include "mye/core/JsonFile.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera2D.h"
#include "mye/gameplay/Progression.h"
#include <filesystem>

#include <memory>
#include <string>

namespace mye::editor {

struct PlayModeController::Impl {
    std::unique_ptr<runtime::ObjectSystem> objects;
    render::Camera2D defaultCamera;
    runtime::SceneTransitionManager transition;
    std::unique_ptr<ecs::World> candidate;
    std::string root, spawnName, loadError;
    bool activate = false, transitionBound = false;
    bool stepRequested = false;
    std::unique_ptr<EventBus>     playEvents;     // Play World 전용 월드-로컬 버스(Stop 시 파기).
    std::unique_ptr<ecs::World>   playWorld;      // 버스보다 먼저 파괴.
    std::unique_ptr<CommandStack> playCommands;   // 플레이 중 Undo(Stop 시 파기).
};

PlayModeController::PlayModeController() : m_impl(std::make_unique<Impl>()) {}
PlayModeController::~PlayModeController() { Stop(); }
const render::Camera2D& PlayModeController::DefaultCamera() const { return m_impl->defaultCamera; }

static void PublishStateChange(EventBus* events, PlayState prev, PlayState cur) {
    if (!events || prev == cur) return;
    PlayStateChangedEvent ev;
    ev.previous = prev;
    ev.current = cur;
    events->Publish(ev);
}

// Play World에 편집 World와 동일한 컴포넌트 풀을 미리 등록한다.
//
//   SceneSerializer::ReadInto는 World::AddDynamic으로 컴포넌트를 붙이는데, AddDynamic은 풀이
//   미등록이면 nullptr을 반환한다(자동 등록 안 함). 새로 만든 Play World에는 풀이 하나도 없으므로,
//   역직렬화 전에 편집 World의 실제 ComponentTypeDesc(생성/파괴/이동 훅 포함)를 그대로 복제
//   등록해 둔다. 열거는 리플렉션 레지스트리의 Struct 타입 중 편집 World에 등록된 것을 기준으로
//   하며(SceneSerializer의 컴포넌트 선별 규약과 동일), 실제 desc는 편집 World 풀에서 가져와
//   훅 무결성을 보존한다.
static void MirrorComponentPools(const ecs::World& src, ecs::World& dst) {
    for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
        if (!t || t->GetKind() != refl::Kind::Struct) continue;
        const auto cid = static_cast<ecs::ComponentTypeId>(refl::TypeIdFromName(t->Name()));
        if (!src.IsRegistered(cid)) continue;
        if (dst.IsRegistered(cid)) continue;
        if (const ecs::ComponentPool* pool = src.Pool(cid))
            dst.RegisterComponent(pool->Desc());   // 실제 훅 포함 desc 복제.
    }
}

Expected<void, Error> PlayModeController::Play() {
    if (m_state != PlayState::Edit) {
        if (m_state == PlayState::Paused) Resume();
        return {};   // 이미 Playing이면 no-op.
    }
    if (m_editWorld) {
        auto valid = runtime::ValidateObjectComponents(*m_editWorld);
        if (!valid) return valid.GetError();
    }
    const PlayState prev = m_state;

    // 07 §3: 편집 World를 인메모리 스냅샷으로 직렬화 → 새 World로 역직렬화.
    auto world = std::make_unique<ecs::World>();
    if (m_editWorld) {
        // 역직렬화 전 컴포넌트 풀 미러링(AddDynamic이 풀 없으면 no-op이므로 필수).
        MirrorComponentPools(*m_editWorld, *world);
        SceneSerializer ser;
        auto snap = ser.Snapshot(*m_editWorld);
        if (!snap) return snap.GetError();
        auto restored = ser.Restore(*world, snap.Value());
        if (!restored) return restored.GetError();
        auto valid = runtime::ValidateObjectComponents(*world);
        if (!valid) return valid.GetError();
        if (auto camera = scene::UpdateGameCamera2D(*world, 0); !camera) return camera.GetError();
        // Play World 전용 월드-로컬 이벤트 버스(Stop 시 파기). 편집 World 버스를 공유하면
        //   플레이 중 publish가 편집-World 구독자로 새고, 스크립트/리스너 구독이 Stop 후
        //   dangling된다. 게임플레이 이벤트는 Play World 안에서 자족적으로 흐르게 한다
        //   (의도한 이벤트만 필요 시 상위에서 브리지).
        m_impl->playEvents = std::make_unique<EventBus>();
        world->SetEventBus(m_impl->playEvents.get());
    }
    m_impl->playWorld = std::move(world);
    runtime::UpdateDefaultCamera2D(*m_impl->playWorld, m_impl->defaultCamera, true);

    // 플레이 전용 Undo 스택(Stop 시 파기). 문서 스택과 분리(07 §3).
    m_impl->playCommands = std::make_unique<CommandStack>();

    m_impl->stepRequested = false;
    m_state = PlayState::Playing;
    PublishStateChange(m_events, prev, m_state);
    return {};
}

void PlayModeController::Pause() {
    if (m_state != PlayState::Playing) return;
    const PlayState prev = m_state;
    m_state = PlayState::Paused;
    PublishStateChange(m_events, prev, m_state);
}

void PlayModeController::Resume() {
    if (m_state != PlayState::Paused) return;
    const PlayState prev = m_state;
    m_impl->stepRequested = false;
    m_state = PlayState::Playing;
    PublishStateChange(m_events, prev, m_state);
}

void PlayModeController::StepFrame() {
    if (m_state == PlayState::Paused) m_impl->stepRequested = true;
}
bool PlayModeController::ConsumeStepRequest() {
    const bool requested = m_state == PlayState::Paused && m_impl->stepRequested;
    m_impl->stepRequested = false;
    return requested;
}

void PlayModeController::Stop() {
    if (m_state == PlayState::Edit) return;
    const PlayState prev = m_state;

    // 07 §3: Play World 파기 + 플레이 Undo 스택 파기 → 편집 World 재표시. 스냅샷 복원 아님.
    //   파괴 순서: World 먼저(구독자 보유 가능) → 그 버스. 그다음 커맨드 스택.
    m_impl->objects.reset();
    m_impl->transition.Shutdown();
    m_impl->transitionBound = false;
    m_impl->candidate.reset();
    m_impl->activate = false;
    m_impl->loadError.clear();
    m_inputEnabled = false;
    m_impl->playWorld.reset();
    m_impl->playEvents.reset();
    if (m_impl->playCommands) m_impl->playCommands->Clear();
    m_impl->playCommands.reset();

    m_impl->stepRequested = false;
    m_state = PlayState::Edit;
    PublishStateChange(m_events, prev, m_state);
}

Expected<void, Error> PlayModeController::Tick(float dt, Vec2 movement, bool interact, std::string_view projectRoot) {
    return Tick(dt,runtime::GameInput{movement,interact},projectRoot);
}
Expected<void, Error> PlayModeController::Tick(float dt, const runtime::GameInput& input, std::string_view projectRoot) {
    auto& s = *m_impl;
    if (!IsPlaying() || !s.playWorld) return {};
    if (!s.objects) {
        s.objects = std::make_unique<runtime::ObjectSystem>(*s.playWorld);
        auto initialized = s.objects->Initialize();
        if (!initialized) { s.objects.reset(); return initialized.GetError(); }
    }
    if (!s.transitionBound) {
        s.root = std::string(projectRoot);
        runtime::SceneLoaderFn loader;
        loader.begin = [this](const runtime::SceneRef& ref) {
            auto& state = *m_impl;
            state.candidate.reset(); state.loadError.clear();
            std::error_code ec;
            const auto root = std::filesystem::weakly_canonical(Utf8Path(state.root) / "assets", ec);
            const auto path = std::filesystem::weakly_canonical(Utf8Path(state.root) / Utf8Path(ref.vpath), ec);
            const auto relative = path.lexically_relative(root);
            if (ec || relative.empty() || relative.is_absolute() || *relative.begin() == ".." || path.extension() != ".scene") {
                state.loadError = "Map must be a .scene file inside project assets"; return runtime::SceneLoadTicket{1};
            }
            // ponytail: small scene files load synchronously; use asset async loading when measured stalls warrant it.
            auto world = std::make_unique<ecs::World>();
            MirrorComponentPools(*m_editWorld, *world);
            auto loaded = SceneSerializer{}.LoadFromFile(*world, Utf8String(path));
            if (!loaded) { state.loadError = loaded.GetError().message; return runtime::SceneLoadTicket{1}; }
            auto valid = runtime::ValidateObjectComponents(*world);
            if (!valid) { state.loadError = valid.GetError().message; return runtime::SceneLoadTicket{1}; }
            scene::UpdateWorldTransforms(*world);
            bool found = false;
            Vec3 spawn;
            world->Query<scene::ObjectName, scene::WorldTransform>().Each([&](ecs::Entity, const scene::ObjectName& n, const scene::WorldTransform& t) {
                if (n.value == state.spawnName) { found = true; spawn = {t.matrix.m[3][0], t.matrix.m[3][1], t.matrix.m[3][2]}; }
            });
            if (!found) { state.loadError = "Destination spawn object not found: " + state.spawnName; return runtime::SceneLoadTicket{1}; }
            const gameplay::Progression* progression = nullptr;
            state.playWorld->Query<runtime::CharacterController2D, gameplay::Progression>().Each([&](ecs::Entity, const auto& c, const auto& p) { if (c.enabled) progression = &p; });
            state.playWorld->Query<runtime::CharacterController3D, gameplay::Progression>().Each([&](ecs::Entity, const auto& c, const auto& p) { if (c.enabled) progression = &p; });
            bool playerFound = false;
            world->Query<runtime::CharacterController2D, scene::LocalTransform>().Each([&](ecs::Entity e, const auto& c, scene::LocalTransform& t) {
                if (!c.enabled) return;
                playerFound = true; t.position = spawn; t.dirty = true;
                if (progression) if (auto* p = world->TryGet<gameplay::Progression>(e)) *p = *progression;
            });
            world->Query<runtime::CharacterController3D, scene::LocalTransform>().Each([&](ecs::Entity e, const auto& c, scene::LocalTransform& t) {
                if (!c.enabled) return;
                playerFound = true; t.position = spawn; t.dirty = true;
                if (progression) if (auto* p = world->TryGet<gameplay::Progression>(e)) *p = *progression;
            });
            if (!playerFound) { state.loadError = "Destination map needs a character controller"; return runtime::SceneLoadTicket{1}; }
            valid=runtime::ValidateObjectComponents(*world);
            if (!valid) { state.loadError=valid.GetError().message; return runtime::SceneLoadTicket{1}; }
            state.candidate = std::move(world);
            return runtime::SceneLoadTicket{1};
        };
        loader.poll = [](runtime::SceneLoadTicket, bool& done) { done = true; return 1.0f; };
        loader.activate = [this](runtime::SceneLoadTicket) -> Expected<void, Error> {
            if (!m_impl->candidate) return Error{m_impl->loadError, 1};
            m_impl->activate = true; return {};
        };
        // Controller lifetime spans map changes; swapping worlds never destroys an active Update call.
        s.transition.Initialize(nullptr, nullptr, std::move(loader), nullptr);
        s.transitionBound = true;
    }
    s.transition.Update(dt);
    if (s.activate) {
        s.activate = false;
        s.objects.reset();
        s.playWorld = std::move(s.candidate);
        s.playWorld->SetEventBus(s.playEvents.get());
        runtime::UpdateDefaultCamera2D(*s.playWorld, s.defaultCamera, true);
        s.playCommands->Clear();
        s.objects = std::make_unique<runtime::ObjectSystem>(*s.playWorld);
        auto initialized = s.objects->Initialize();
        if (!initialized) return initialized.GetError();
    }
    if (!s.transition.ConsumesInput()) {
        auto tick=s.objects->Tick(dt,input);
        if (!tick) return tick.GetError();
        runtime::UpdateDefaultCamera2D(*s.playWorld, s.defaultCamera);
    }
    const auto request = s.objects->TakeMapRequest();
    if (!request.scenePath.empty() && !s.transition.IsTransitioning()) {
        s.spawnName = request.spawnName;
        runtime::TransitionDesc desc; desc.fadeOutSec = .15f; desc.fadeInSec = .15f; desc.showLoadingScreen = false;
        auto changed = s.transition.ChangeScene(runtime::SceneRef{request.scenePath}, desc);
        if (!changed) return changed.GetError();
    }
    if (!s.loadError.empty() && !s.transition.IsTransitioning()) {
        const auto error = std::exchange(s.loadError, {});
        return Error{error, 1};
    }
    return {};
}
std::string_view PlayModeController::Message() const {
    return m_impl->objects ? m_impl->objects->Message() : std::string_view{};
}
std::string_view PlayModeController::Prompt() const {
    return m_impl->objects ? m_impl->objects->Prompt() : std::string_view{};
}
float PlayModeController::FadeAlpha() const { return m_impl->transition.FadeAlpha(); }

ecs::World* PlayModeController::ActiveWorld() const {
    // 07 §3: Edit=편집 World, Playing/Paused=Play World.
    if (m_state == PlayState::Edit) return m_editWorld;
    return m_impl->playWorld ? m_impl->playWorld.get() : m_editWorld;
}

CommandStack* PlayModeController::PlayCommandStack() const {
    return m_impl->playCommands.get();
}

} // namespace mye::editor
