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
#include "mye/core/Window.h"

#include <array>
#include <memory>
#include <string>

namespace mye { class IWindow; }
namespace mye::editor { class IEditorViewport; }

namespace mye::editor {

class EditorApp : private IWindowMessageHook {
public:
    enum class Workspace : std::uint8_t { Scene2D = 0, Scene3D = 1, Lua = 4 };
    enum class SceneElement : std::uint8_t { Object, Sprite, Character, Collider, Trigger, Interaction, Spawn, Lua, Mesh, Billboard, Camera, Character3D, Collider3D, Ramp3D, Trigger3D, Camera2D };
    EditorApp();
    ~EditorApp() override;
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // ---- 라이프사이클(EditorModule이 호출) ----
    // 서브시스템 생성·이벤트 배선·내장 패널 등록·프로젝트 오픈·레이아웃 복원.
    Expected<void, Error> Initialize(EngineContext& engine, std::string_view projectPath, std::string_view templateDirectory = {});
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

    Expected<void, Error> CreateProject(std::string_view name, std::string_view directory,
                                        bool discardUnsaved = false, bool useStarter = false);
    Expected<void, Error> OpenProject(std::string_view path, bool discardUnsaved = false);
    Expected<void, Error> OpenScene(std::string_view path);
    Expected<void, Error> SaveProject();
    Expected<void, Error> SaveScene(std::string_view path);
    Expected<void, Error> OpenAnimation(std::string_view path);
    Document* AnimationDocument();
    void SetAnimationFocused() { m_focusedAsset = m_animationId; }
    void SelectAnimationDocument(DocumentId id) { m_animationId = id; }
    Expected<void, Error> OpenUi(std::string_view path);
    Document* UiDocument();
    void SelectUiDocument(DocumentId id) { m_uiId = id; }
    void SetUiFocused() { m_focusedAsset = m_uiId; }
    Expected<void, Error> OpenAnimationState(std::string_view path);
    Document* AnimationStateDocument();
    void SelectAnimationStateDocument(DocumentId id) { m_animationStateId = id; }
    void SetAnimationStateFocused() { m_focusedAsset = m_animationStateId; }
    Expected<std::string, Error> BrowseImageFile();
    Expected<std::string, Error> BrowseAssetFile();
    void ActivateDocument(DocumentId id);
    void RefreshDocumentContext(); // Also called before the module renders/ticks a world.
    Workspace CurrentWorkspace() const { return m_workspace; }
    std::string_view CentralPanelId() const;
    void SelectWorkspace(Workspace workspace);
    Expected<void, Error> CreateScene(bool perspective);
    Expected<ecs::Entity, Error> CreateSceneElement(SceneElement element, ecs::Entity parent = ecs::Entity::Null());
    void NewScene(); // Request the 2D/3D choice, shared by menu, shortcut and panels.
    void RequestAddElement(ecs::Entity parent = ecs::Entity::Null());
    Expected<void, Error> RequestInputSettings();
    void DrawDocumentTabs(); // Scene documents belong to the viewport work area.
    void RequestOpenScene();
    void SaveActive();
    void ReportError(const Error& error) { ReportFileResult(error, ""); }

private:
    void RegisterBuiltinPanels();   // 하이어라키·인스펙터·씬 뷰포트·콘솔 등 내장 패널
    void DrawMenuBar();             // File/Edit/View/... + 확장 메뉴 항목
    bool DrawToolbar(bool inMenuBar); // Centered workspaces and right-aligned playback.
    void DrawStatusBar();
    void DrawProjectLauncher();
    void ExpandEditorWindow();
    void HandleShortcuts();         // Ctrl+S/Z/Y/P 등(포커스 문서 기준)

    // 레이아웃 저장/복원(<project>/.myeditor/layout.ini · session.json).
    void RestoreLayout();
    Expected<void, Error> SaveLayout();

    // 편집 대상 커맨드 스택(플레이 중=플레이 스택, 아니면 포커스 문서 스택). null 가능.
    CommandStack* ActiveStack();
    Document* FocusedAssetDocument();

    // 메뉴/툴바/단축키 액션.
    void TogglePlay();
    void DrawFileDialogs();
    void DrawWorkspaceDialogs();
    void DrawInputSettings();
    void RequestNewProject();
    void RequestOpenProject(bool folder = false);
    void RequestSaveAs();
    void OpenUserGuide();
    void RequestSaveProject();
    bool ConfirmProjectChange();
    void ReportFileResult(const Expected<void, Error>& result, std::string_view success);
    bool OnMessage(void* hwnd, uint32_t msg, uint64_t wparam, int64_t lparam) override;

    // 디스플레이: 창 접근(헤드리스면 null) + 전체화면 토글.
    IWindow* MainWindowOrNull();
    void     ToggleFullscreen();

    EngineContext*   m_engine = nullptr;
    EventBus*        m_events = nullptr;
    IEditorViewport* m_viewport = nullptr;   // EditorModule 소유(비소유 포인터)
    std::string    m_layoutIniPath;   // UTF-8 path; ini is read/written through native filesystem paths.
    IWindow*       m_window = nullptr; // Message hook is detached before editor teardown.
    ecs::World*    m_boundWorld = nullptr;
    bool           m_selectDocumentTab = false;
    bool           m_showNewProject = false;
    bool           m_showNewScene = false;
    bool           m_showAddElement = false;
    bool           m_showInputSettings = false;
    InputMap       m_inputDraft;
    Vec2           m_inputSettingsViewportSize{};
    std::array<char, 65> m_inputActionName{};
    std::string    m_inputActionError;
    bool           m_toolbarInMenu = false;
    Workspace      m_workspace = Workspace::Scene2D;
    ecs::Entity    m_elementParent = ecs::Entity::Null();
    DocumentId     m_elementDocument{};
    SceneElement   m_elementChoice = SceneElement::Object;
    std::array<char, 128> m_elementSearch{};
    bool           m_useStarter = true;
    std::string    m_templateDirectory;
    DocumentId     m_animationId{};
    DocumentId     m_uiId{}, m_focusedAsset{};
    DocumentId     m_animationStateId{};
    std::array<char, 128> m_newProjectName{};
    std::array<char, 4096> m_newProjectDirectory{};
    std::string    m_fileStatus;
    bool           m_fileError = false;

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
