// mye/editor/EditorModule.h — 에디터를 01 모듈 라이프사이클에 얹는 IModule (docs/07 §1)
//
// 07 §1: 게임과 동일한 엔진 모듈 스택 위에 EditorModule을 추가로 얹는다. 이 모듈이 mye_imgui
//   DebugUi(도킹 셸)를 소유·초기화하고, PreRender 페이즈 틱에서 DebugUi::BeginFrame →
//   EditorApp::OnFrame(UI 빌드) → DebugUi::EndFrame 순서를 오케스트레이션한다.
//
// 의존: SceneModule(편집 World)·렌더(오프스크린 RT는 M4-B 뷰포트에서)·mye_imgui.
//   셧다운은 정확한 역순(DebugUi가 윈도우보다 먼저 Shutdown — DebugUi 수명 계약).
#pragma once

#include "mye/core/Module.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mye::editor {

class EditorApp;

class EditorModule : public IModule {
public:
    MYE_SERVICE(EditorModule);

    EditorModule();
    ~EditorModule() override;

    const char* GetName() const override { return "EditorModule"; }
    // SceneModule·렌더·imgui 이후 초기화되도록 의존 선언(위상 정렬).
    std::span<const char* const> GetDependencies() const override;

    void OnLoad(EngineContext& ctx) override;          // 서비스 등록
    void OnInitialize(EngineContext& ctx) override;    // DebugUi 초기화 + EditorApp 생성
    void OnPostInitialize(EngineContext& ctx) override;// 프로젝트 오픈·PreRender 틱 등록
    void OnShutdown(EngineContext& ctx) override;      // EditorApp Shutdown → DebugUi Shutdown

    EditorApp* App();

    // 자동 검증 CLI 배선(main.cpp 가 부팅 인자를 파싱해 호출). onExit=프레임 한도 도달 시 종료 콜백.
    void SetCliControl(bool frameLimit, std::uint64_t maxFrames, bool dumpEnabled,
                       std::string dumpPath, std::function<void()> onExit);
    // Paused 상태에서 F10 스텝 요청(다음 프레임에 Play World 1회 tick). EditorApp/단축키가 호출.
    void RequestStepFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void Frame(const TimeStep& step);          // PreRender 오케스트레이션
    void TickPlayWorld(const TimeStep& step);  // 플레이 tick 게이팅
    void HandleDump();
    void RequestExit();
};

} // namespace mye::editor
