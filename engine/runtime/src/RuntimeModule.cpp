// mye/runtime/RuntimeModule.cpp — 런타임 모듈 라이프사이클 배선 골격 (M6-A)
//
// 골격 범위: 서브시스템 소유·생성·서비스 등록·의존성 선언. 교차 배선(ui/audio/scene/script 서비스
//   조회)·페이즈 틱 등록은 실서비스 접근이 필요하므로 구현 에이전트가 OnPostInitialize 에서 채운다.
#include "mye/runtime/RuntimeModule.h"

#include "mye/runtime/Localization.h"
#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/SaveSystem.h"
#include "mye/runtime/SceneTransition.h"
#include "mye/runtime/AudioListener.h"

#include "mye/core/Log.h"

// 교차 배선용 서비스(가용 시에만 조회 — 미존재 시 헤드리스로 축소).
#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioModule.h"   // AudioEngine 은 AudioModule::kServiceId 로 등록됨
#include "mye/scene/SceneModule.h"
#include "mye/ecs/World.h"

namespace mye::runtime {

struct RuntimeModule::Impl {
    std::unique_ptr<LocalizationSystem>     loc;
    std::unique_ptr<DialogueBox>            box;
    std::unique_ptr<DialogueSystem>         dialogue;
    std::unique_ptr<MoveController>         move;
    std::unique_ptr<CameraFocusController>  camera;
    std::unique_ptr<CutsceneRuntime>        cutscene;
    std::unique_ptr<SaveSystem>             save;
    std::unique_ptr<SceneTransitionManager> sceneTransition;
    std::unique_ptr<AudioListenerBridge>    audioListener;
    scene::SceneModule*                     sceneModForView = nullptr;  // 뷰 틱 World 조회 캐시(비소유)
};

RuntimeModule::RuntimeModule() : m_impl(std::make_unique<Impl>()) {}
RuntimeModule::~RuntimeModule() = default;

std::span<const char* const> RuntimeModule::GetDependencies() const {
    // ui/audio/script/scene 서비스가 먼저 초기화돼야 한다(교차 배선 대상).
    static const char* const kDeps[] = {
        "SceneModule", "AudioModule", "ScriptSystem", "UiSystem"
    };
    return kDeps;
}

void RuntimeModule::OnLoad(EngineContext& ctx) {
    Impl& s = *m_impl;
    s.loc             = std::make_unique<LocalizationSystem>();
    s.box             = std::make_unique<DialogueBox>();
    s.dialogue        = std::make_unique<DialogueSystem>();
    s.move            = std::make_unique<MoveController>();
    s.camera          = std::make_unique<CameraFocusController>();
    s.cutscene        = std::make_unique<CutsceneRuntime>();
    s.save            = std::make_unique<SaveSystem>();
    s.sceneTransition = std::make_unique<SceneTransitionManager>();
    s.audioListener   = std::make_unique<AudioListenerBridge>();

    // 서비스 등록(다른 모듈·데모·Lua 바인딩이 조회).
    ctx.RegisterServiceRaw(LocalizationSystem::kServiceId, s.loc.get());
    ctx.RegisterServiceRaw(DialogueSystem::kServiceId, s.dialogue.get());
    ctx.RegisterServiceRaw(CutsceneRuntime::kServiceId, s.cutscene.get());
    ctx.RegisterServiceRaw(SaveSystem::kServiceId, s.save.get());
    ctx.RegisterServiceRaw(SceneTransitionManager::kServiceId, s.sceneTransition.get());
}

void RuntimeModule::OnInitialize(EngineContext& ctx) {
    // 이벤트 버스만 있으면 초기화 가능한 서브시스템 배선(나머지는 PostInitialize 교차 배선).
    m_impl->loc->Initialize(&ctx.Events());
}

void RuntimeModule::OnPostInitialize(EngineContext& ctx) {
    // 교차 배선(모듈형 경로 정본): 가용 서비스를 조회해 서브시스템을 실제 배선하고, 페이즈 틱을
    //   등록한다. 서비스가 없으면 우아하게 축소(헤드리스) — 부분 배선도 안전하게 동작한다.
    //   village_demo(자립형)는 UI 오버레이·스크립트 런타임까지 손수 배선하지만, 이 모듈은 서비스
    //   레지스트리로 접근 가능한 범위(오디오·씬 World·이벤트)를 배선해 재사용 가능한 정본을 제공한다.
    Impl& s = *m_impl;

    // AudioEngine 은 AudioModule::kServiceId("AudioEngine") 로 등록된다(모듈이 엔진 포인터를 등록).
    auto* audio    = static_cast<audio::AudioEngine*>(
        ctx.GetServiceRaw(audio::AudioModule::kServiceId));
    auto* sceneMod = ctx.GetService<scene::SceneModule>();
    ecs::World* world = sceneMod ? &sceneMod->World() : nullptr;

    // DialogueSystem: box=nullptr(헤드리스 — UI 오버레이는 앱/표현 계층 몫). loc/audio/events 배선.
    if (s.dialogue) s.dialogue->Initialize(nullptr, s.loc.get(), audio, &ctx.Events());

    // MoveController: World 필요(nav 없음 → 직선 이동). World 미존재 시 미배선(무동작).
    if (s.move && world) s.move->Initialize(world, nullptr);

    // CameraFocusController: 실제 카메라 적용 콜백은 앱이 소유(표현). 여기선 no-op 포커스로 초기화해
    //   컷신 코루틴(camera.focus_wait)이 즉시 완료되도록 한다(헤드리스 안전).
    if (s.camera) s.camera->Initialize([](Vec2) {}, Vec2{});

    // CutsceneRuntime: dialogue/move/camera 조합.
    if (s.cutscene) s.cutscene->Initialize(s.dialogue.get(), s.move.get(), s.camera.get());

    // AudioListenerBridge: audio + World(관심점 엔티티는 게임이 SetListenerEntity 로 지정).
    if (s.audioListener) s.audioListener->Initialize(audio, world);

    // 페이즈 틱: 시뮬(FixedUpdate)=move/listener, 표현(Update)=dialogue/scene, PostUpdate=cutscene view.
    ctx.Modules().AddTick(this, UpdatePhase::FixedUpdate, [this](const TimeStep& step) {
        Impl& im = *m_impl;
        const float dt = static_cast<float>(step.deltaSeconds);
        if (im.move) im.move->Update(dt);
        if (im.audioListener) im.audioListener->Update();
    }, 0);
    ctx.Modules().AddTick(this, UpdatePhase::Update, [this](const TimeStep& step) {
        Impl& im = *m_impl;
        const float dt = static_cast<float>(step.deltaSeconds);
        if (im.dialogue) im.dialogue->Update(dt);
        if (im.sceneTransition) im.sceneTransition->Update(dt);
    }, 0);
    ctx.Modules().AddTick(this, UpdatePhase::PostUpdate, [this](const TimeStep& step) {
        Impl& im = *m_impl;
        auto* sm = im.sceneModForView;
        ecs::World* w = sm ? &sm->World() : nullptr;
        if (im.cutscene) im.cutscene->UpdateView(static_cast<float>(step.deltaSeconds), w);
    }, 0);

    // View 틱에서 매번 서비스 조회를 피하기 위해 SceneModule 포인터를 캐시.
    s.sceneModForView = sceneMod;

    if (!audio || !world) {
        MYE_LOG_INFO("RuntimeModule",
            "OnPostInitialize: 부분 배선(audio={} world={}) — 미가용 서비스는 헤드리스로 축소",
            audio ? 1 : 0, world ? 1 : 0);
    }
}

void RuntimeModule::OnShutdown(EngineContext& ctx) {
    Impl& s = *m_impl;
    // 역순 셧다운(데드락 규약: 오디오/스크립트 스레드 경계 안전 — 서비스 해제 후 파기).
    ctx.UnregisterServiceRaw(SceneTransitionManager::kServiceId);
    ctx.UnregisterServiceRaw(SaveSystem::kServiceId);
    ctx.UnregisterServiceRaw(CutsceneRuntime::kServiceId);
    ctx.UnregisterServiceRaw(DialogueSystem::kServiceId);
    ctx.UnregisterServiceRaw(LocalizationSystem::kServiceId);

    if (s.sceneTransition) s.sceneTransition->Shutdown();
    if (s.save) s.save->Shutdown();
    if (s.cutscene) s.cutscene->Shutdown();
    if (s.dialogue) s.dialogue->Shutdown();
    if (s.box) s.box->Shutdown();
    if (s.loc) s.loc->Shutdown();

    s.sceneModForView = nullptr;   // 비소유 뷰 캐시 무효화(틱 재진입 방지).
    s.audioListener.reset();
    s.sceneTransition.reset();
    s.save.reset();
    s.cutscene.reset();
    s.camera.reset();
    s.move.reset();
    s.dialogue.reset();
    s.box.reset();
    s.loc.reset();
}

LocalizationSystem*     RuntimeModule::Localization() const { return m_impl->loc.get(); }
DialogueSystem*         RuntimeModule::Dialogue() const { return m_impl->dialogue.get(); }
CutsceneRuntime*        RuntimeModule::Cutscene() const { return m_impl->cutscene.get(); }
SaveSystem*             RuntimeModule::Save() const { return m_impl->save.get(); }
SceneTransitionManager* RuntimeModule::SceneTransition() const { return m_impl->sceneTransition.get(); }
AudioListenerBridge*    RuntimeModule::AudioListener() const { return m_impl->audioListener.get(); }

} // namespace mye::runtime
