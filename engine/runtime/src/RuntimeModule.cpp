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

void RuntimeModule::OnPostInitialize(EngineContext& /*ctx*/) {
    // 교차 배선(UiSystem/AudioEngine/ScriptSystem/SceneModule 서비스 조회 → DialogueBox/DialogueSystem/
    //   MoveController/CameraFocus/AudioListener/RuntimeBindings + 페이즈 틱)은 모듈형 엔진 앱이
    //   위 서비스들을 모두 등록했을 때만 유효하다. M6 수직 슬라이스(samples/village_demo)는 이 배선을
    //   자립형으로 완성해 로드맵 완료 기준을 실증한다 — 그 main.cpp 의 BuildRuntimeAndScripts()·
    //   FixedUpdate()/Update() 가 이 모듈이 모듈형 경로에서 수행할 배선의 정본 레시피다.
    //   (현재 어떤 앱도 RuntimeModule 을 모듈형으로 등록하지 않으므로 여기서는 no-op 을 유지한다.)
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
