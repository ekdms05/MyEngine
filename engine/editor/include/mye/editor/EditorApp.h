// mye/editor/EditorApp.h — 에디터 앱 셸(도킹·메뉴·서브시스템 소유) (docs/07 §1)
//
// 07 §1: MyEditor.exe는 L5의 얇은 앱. 게임과 동일한 엔진 모듈 스택(L0~L4) 위에 EditorModule을
//   얹는다. EditorApp은 에디터의 하위 서비스(패널·커맨드·선택·플레이모드·확장·프로젝트)를 소유
//   하고 매 프레임 mye_imgui 도킹 셸 위에 메뉴바·툴바·도킹 스페이스·패널을 빌드한다.
//
// EditorApp은 mye::Application을 상속하지 않는다 — 실제 프레임 훅은 EditorModule(IModule)이
//   PreRender 틱에서 잡고, EditorApp은 그 안에서 UI를 빌드하는 오케스트레이터다(01 라이프사이클
//   준수, 셧다운 데드락 규약 준수).
#pragma once

#include "mye/editor/EditorContext.h"
#include "mye/editor/Panel.h"
#include "mye/editor/Selection.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/ExtensionRegistry.h"
#include "mye/editor/Project.h"
#include "mye/editor/Inspector.h"

#include <memory>
#include <string>

namespace mye { class IWindow; }
namespace mye::editor { class IEditorViewport; }

namespace mye::editor {

class EditorApp {
public:
    EditorApp();
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // ---- 라이프사이클(EditorModule이 호출) ----
    // 서브시스템 생성·이벤트 배선·내장 패널 등록·프로젝트 오픈·레이아웃 복원.
    Expected<void, Error> Initialize(EngineContext& engine, std::string_view projectPath);
    // 매 프레임(PreRender): DebugUi::BeginFrame 이후 호출 → 메뉴바·툴바·도킹·패널 빌드.
    void OnFrame();
    // 종료: 레이아웃 저장·문서 정리(디바이스/윈도우 파괴 전, 셧다운 데드락 규약).
    void Shutdown();

    // ---- 서브시스템 접근(07 API 스케치) ----
    ProjectContext&          Project() { return *m_project; }
    PanelManager&            Panels() { return *m_panels; }
    SelectionManager&        Selection() { return *m_selection; }
    CommandStack&            Commands();          // 포커스 문서 스택으로 위임
    PlayModeController&      PlayMode() { return *m_playMode; }
    EditorExtensionRegistry& Extensions() { return *m_extensions; }
    InspectorRenderer&       Inspector() { return *m_inspector; }
    EventBus*                Events() { return m_events; }

    // 씬 뷰포트 렌더러(EditorModule이 디바이스 초기화 후 배선). ViewportPanel이 소비.
    //   null이면 뷰포트 패널은 "렌더 미가용" 안내만 그린다(헤드리스/부팅 실패).
    void             SetViewport(IEditorViewport* vp) { m_viewport = vp; }
    IEditorViewport* Viewport() { return m_viewport; }

    // 현재 프레임 컨텍스트(패널·커맨드에 전달). 프레임 시작 시 재구성.
    EditorContext& Context() { return m_ctx; }

    // 디스플레이 옵션 — EditorModule 이 Present(vsync) 로 소비.
    bool Vsync() const { return m_vsync; }

private:
    void RegisterBuiltinPanels();   // 하이어라키·인스펙터·씬 뷰포트·콘솔 등 내장 패널
    void DrawMenuBar();             // File/Edit/View/... + 확장 메뉴 항목
    void DrawToolbar();             // New/Save · 기즈모 · Play/Stop/Step · 확장 버튼
    void HandleShortcuts();         // Ctrl+S/Z/Y/P 등(포커스 문서 기준)

    // 레이아웃 저장/복원(<project>/.myeditor/layout.ini · session.json).
    void RestoreLayout();
    void SaveLayout();

    // 편집 대상 커맨드 스택(플레이 중=플레이 스택, 아니면 포커스 문서 스택). null 가능.
    CommandStack* ActiveStack();

    // 메뉴/툴바/단축키 액션.
    void NewScene();
    void SaveActive();
    void TogglePlay();

    // 디스플레이: 창 접근(헤드리스면 null) + 전체화면 토글.
    IWindow* MainWindowOrNull();
    void     ToggleFullscreen();

    // 미저장 새 씬의 기본 저장 경로(<projectRoot>/assets/scenes/untitled.scene). 미오픈 시 빈.
    std::string DefaultScenePath() const;

    EngineContext*   m_engine = nullptr;
    EventBus*        m_events = nullptr;
    IEditorViewport* m_viewport = nullptr;   // EditorModule 소유(비소유 포인터)
    std::string    m_layoutIniPath;   // ImGui IniFilename로 지정하는 프로젝트별 경로(수명 유지)

    std::unique_ptr<ProjectContext>          m_project;
    std::unique_ptr<PanelManager>            m_panels;
    std::unique_ptr<SelectionManager>        m_selection;
    std::unique_ptr<PlayModeController>      m_playMode;
    std::unique_ptr<EditorExtensionRegistry> m_extensions;
    std::unique_ptr<InspectorRenderer>       m_inspector;

    EditorContext m_ctx{};
    bool          m_vsync = false;   // 표시 옵션(수직동기)
};

} // namespace mye::editor
