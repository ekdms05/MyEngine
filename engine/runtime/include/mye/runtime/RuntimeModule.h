// mye/runtime/RuntimeModule.h — 런타임 시스템 모듈(라이프사이클 배선) (docs/06 §7, M6-A)
//
// mye_runtime 서브시스템(대화·컷신·씬전환·세이브·로컬라이제이션·오디오 리스너)을 01 IModule 로
//   감싸 서비스 등록·페이즈 틱·셧다운을 처리한다. 소유·수명·배선의 정본.
//   OnInitialize: 서브시스템 생성. OnPostInitialize: 교차 배선(ui/audio/scene/script 서비스 조회)
//     + 페이즈 틱 등록(시뮬=FixedUpdate: 이동·리스너, 표현=Update/PostUpdate: 대화·전환·카메라).
//   OnShutdown: 역순 정리(셧다운 데드락 규약 준수 — sol/오디오 스레드 경계 안전).
//
// 의존: SceneModule·AudioModule·ScriptSystem·UiSystem 서비스가 먼저 초기화돼야 한다(GetDependencies).
#pragma once

#include "mye/core/Module.h"

#include <memory>

namespace mye::runtime {

class LocalizationSystem;
class DialogueSystem;
class DialogueBox;
class CutsceneRuntime;
class MoveController;
class CameraFocusController;
class SaveSystem;
class SceneTransitionManager;
class AudioListenerBridge;

class RuntimeModule final : public IModule {
public:
    MYE_SERVICE(mye::runtime::RuntimeModule);

    RuntimeModule();
    ~RuntimeModule() override;

    const char* GetName() const override { return "RuntimeModule"; }
    std::span<const char* const> GetDependencies() const override;

    void OnLoad(EngineContext& ctx) override;
    void OnInitialize(EngineContext& ctx) override;
    void OnPostInitialize(EngineContext& ctx) override;
    void OnShutdown(EngineContext& ctx) override;

    // ---- 서브시스템 접근(데모·테스트가 배선 확인) ----
    LocalizationSystem*     Localization() const;
    DialogueSystem*         Dialogue() const;
    CutsceneRuntime*        Cutscene() const;
    SaveSystem*             Save() const;
    SceneTransitionManager* SceneTransition() const;
    AudioListenerBridge*    AudioListener() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
