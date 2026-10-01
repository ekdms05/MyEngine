// EditorApp.cpp — 에디터 앱 셸 (docs/07 §1, §7)
//
// 07 §1·§7: 서브시스템(패널·커맨드·선택·플레이모드·확장·프로젝트)을 소유하고 매 프레임 mye_imgui
//   도킹 셸 위에 메뉴·작업대 선택·재생 제어·도킹
//   스페이스·패널을 빌드한다. 단축키(Ctrl+S/N/Z/Y·Ctrl+P·F10·Alt+←/→)를 처리한다. 레이아웃은
//   <project>/.myeditor/(layout.ini=ImGui ini, session.json=열린 패널 목록)에 저장·복원한다.
//
// 내장 패널도 1급 플러그인 — PanelManager::RegisterFactory 동일 경로로 등록(07 §확장).
#include "mye/editor/EditorApp.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Project.h"
#include "mye/core/Module.h"
#include "mye/core/Log.h"
#include "mye/core/JsonFile.h"
#include "mye/core/I18n.h"
#include "mye/core/Window.h"
#include "mye/ecs/World.h"
#include "mye/imgui/EditorWidgets.h"
#include "mye/editor/Viewport.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace mye::editor {

// ViewportPanel.cpp 정의. 나머지 내장 패널 팩토리는 BuiltinPanels.h.
std::unique_ptr<IEditorPanelFactory> MakeViewportPanelFactory();

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

Expected<void, Error> EditorApp::Initialize(EngineContext& engine, std::string_view projectPath, std::string_view templateDirectory) {
    m_templateDirectory = templateDirectory;
    m_engine = &engine;
    m_events = &engine.Events();

    // 서브시스템 생성.
    m_project    = std::make_unique<ProjectContext>();
    m_panels     = std::make_unique<PanelManager>();
    m_selection  = std::make_unique<SelectionManager>(m_events);
    m_playMode   = std::make_unique<PlayModeController>();
    m_extensions = std::make_unique<EditorExtensionRegistry>();
    m_inspector  = std::make_unique<InspectorRenderer>();

    m_playMode->SetEventBus(m_events);

    RegisterBuiltinPanels();

    // 프레임 컨텍스트 정적 배선(문서·커맨드·World는 매 프레임 갱신).
    m_ctx.app        = this;
    m_ctx.engine     = m_engine;
    m_ctx.events     = m_events;
    m_ctx.selection  = m_selection.get();
    m_ctx.playMode   = m_playMode.get();
    m_ctx.extensions = m_extensions.get();
    m_ctx.project    = m_project.get();

    if (!projectPath.empty()) {
        auto result = OpenProject(projectPath);
        if (!result) {
            ReportFileResult(result, "");
            RestoreLayout(); // Editor remains usable so the user can choose a valid project.
        }
    } else if (!m_templateDirectory.empty()) {
        const auto starter = Utf8Path(engine.GetPaths().userDir) / "projects" / "MeadowVillage";
        std::error_code ec;
        auto result = std::filesystem::exists(starter / "project.myeproj", ec)
            ? OpenProject(Utf8String(starter / "project.myeproj"))
            : CreateProject("초원마을", Utf8String(starter), false, true);
        if (!result) { ReportFileResult(result, ""); RestoreLayout(); }
    } else RestoreLayout();
    m_window = MainWindowOrNull();
    if (m_window) m_window->AddMessageHook(this, -100);
    return {};
}

void EditorApp::RegisterBuiltinPanels() {
    // 내장 = 1급 플러그인: 확장 레지스트리 경로가 아니라 PanelManager에 직접 등록(동일 API).
    //   등록 순서 = Window 메뉴 순서(하이어라키·뷰포트·인스펙터·에셋·콘솔).
    m_panels->RegisterFactory(MakeHierarchyPanelFactory());
    m_panels->RegisterFactory(MakeViewportPanelFactory());
    m_panels->RegisterFactory(MakeInspectorPanelFactory());
    m_panels->RegisterFactory(MakeAssetBrowserPanelFactory());
    m_panels->RegisterFactory(MakeConsolePanelFactory());
    // 콘텐츠 제작 도구(타일맵 편집·팔레트·애니메이션 에디터).
    m_panels->RegisterFactory(MakeTilemapEditorPanelFactory());
    m_panels->RegisterFactory(MakeTilePalettePanelFactory());
    m_panels->RegisterFactory(MakeAnimationEditorPanelFactory());
    m_panels->RegisterFactory(MakeDotEditorPanelFactory());   // 도트(픽셀아트) 에디터
    m_panels->RegisterFactory(MakeScenesPanelFactory());
    m_panels->RegisterFactory(MakeLuaPanelFactory());

    // 확장 경로(플러그인·MCP·Lua)로 등록된 패널 팩토리를 PanelManager로 위임(07 §확장:
    //   내장이 되는 건 플러그인도 된다). AddPanel이 보관한 팩토리를 여기서 흡수한다.
    if (m_extensions) m_extensions->DrainPanelFactories(*m_panels);

    // 콘솔 로그 싱크를 전역 Log 에 장착(패널 인스턴스와 독립적으로 로그 수집).
    InstallConsoleLogSink();

    // 기본 도구만 연다. PanelManager가 DockSlot 힌트로 주변 탭을 배치한다.
    m_panels->Open("mye.hierarchy");
    m_panels->Open("mye.viewport");
    m_panels->Open("mye.inspector");
    m_panels->Open("mye.assets");
    m_panels->Open("mye.console");
    m_panels->Open("mye.anim");
    // Dot and Lua workspaces open on demand; the scene starts visible.
}

void EditorApp::RestoreLayout() {
    // ImGui ini(도킹 레이아웃) 경로 결정. 프로젝트가 열려 있으면 프로젝트별 layout.ini,
    //   아니면 유저 스코프(<userDir>/editor_layout.ini)로 폴백 — 프로젝트 없이 그냥 실행해도
    //   사용자가 조절한 레이아웃이 저장/복원된다(첫 실행은 기본 배치, 이후엔 저장된 배치).
    std::error_code ec;
    if (m_project && m_project->IsOpen()) {
        std::filesystem::create_directories(Utf8Path(m_project->EditorStateDir()), ec);
        m_layoutIniPath = m_project->LayoutIniPath();
    } else if (m_engine) {
        const auto userDir = Utf8Path(m_engine->GetPaths().userDir);
        std::filesystem::create_directories(userDir, ec);
        m_layoutIniPath = Utf8String(userDir / "editor_layout.ini");
    }

    // ImGui가 존재하는 프레임에서만 IO 접근(테스트 경로는 ImGui 미초기화).
    if (ImGui::GetCurrentContext() && !m_layoutIniPath.empty()) {
        ImGui::GetIO().IniFilename = nullptr; // Native ImGui fopen does not accept all UTF-8 Windows paths.
        std::ifstream in(Utf8Path(m_layoutIniPath), std::ios::binary);
        if (in) {
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
        }
    }

    // 열린 패널 목록(session.json) 복원 — 프로젝트가 있을 때만.
    if (m_project && m_project->IsOpen()) {
        const std::string sessionPath = m_project->SessionJsonPath();
        if (std::filesystem::exists(Utf8Path(sessionPath), ec)) {
            auto parsed = ReadJsonFile(Utf8Path(sessionPath));
            if (parsed) {
                m_panels->DeserializeLayout(parsed.Value());
                const auto* workspace = parsed.Value().IsObject() ? parsed.Value().Find("workspace") : nullptr;
                if (workspace && workspace->IsInteger() && workspace->AsInt() >= 0 &&
                    workspace->AsInt() <= static_cast<int64_t>(Workspace::Lua))
                    SelectWorkspace(static_cast<Workspace>(workspace->AsInt()));
            }
            else MYE_LOG_WARN("Editor", "Layout restore failed: {}", parsed.GetError().message);
        }
    }
}

Expected<void, Error> EditorApp::SaveLayout() {
    // 도킹 레이아웃(ImGui ini)은 프로젝트 유무와 무관하게 저장 — 그냥 실행한 경우도 유저 스코프
    //   경로에 사용자가 조절한 배치를 남긴다.
    if (ImGui::GetCurrentContext() && !m_layoutIniPath.empty()) {
        std::size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        std::ofstream out(Utf8Path(m_layoutIniPath), std::ios::binary | std::ios::trunc);
        out.write(text, static_cast<std::streamsize>(size));
        out.flush();
        if (!out) return Error{"Could not save editor layout", 1};
    }

    // 열린 패널 목록(session.json)은 프로젝트가 있을 때만.
    if (!m_project || !m_project->IsOpen()) return {};
    json::Value session;
    m_panels->SerializeLayout(session);
    auto state = session.AsObject();
    state["workspace"] = json::Value(static_cast<int64_t>(m_workspace));
    session = json::Value(std::move(state));
    const std::string sessionPath = m_project->SessionJsonPath();
    std::error_code ec;
    std::filesystem::create_directories(Utf8Path(m_project->EditorStateDir()), ec);
    if (ec) return Error{"Could not create editor state folder: " + ec.message(), ec.value()};
    return WriteJsonFile(Utf8Path(sessionPath), session);
}

void EditorApp::OnFrame() {
    // 매 프레임 문서·커맨드 컨텍스트 갱신(포커스 문서 기준).
    RefreshDocumentContext();

    HandleShortcuts();
    DrawMenuBar();
    RefreshDocumentContext();

    // 에디터 호스트 윈도우: 메인 메뉴바 아래 작업영역 전체를 덮고, 그 안에 [툴바 행][도크스페이스]
    //   를 세로로 쌓는다. 이렇게 하면 툴바가 도크스페이스(씬 뷰포트 탭 포함) 위를 덮지 않아,
    //   뷰포트 상단도 정상적으로 클릭된다. 도크스페이스에 도킹되는 패널들은 호스트 End 후 그린다.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    const float statusHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, std::max(1.0f, vp->WorkSize.y - statusHeight)));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##EditorHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (!m_toolbarInMenu) DrawToolbar(false); // Narrow windows retain accessible controls on a second row.
    if (m_panels) m_panels->SetupDockspace(m_ctx);   // 남은 영역에 도크스페이스 + 최초 1회 기본 배치
    ImGui::End();                                    // 호스트 종료

    DrawStatusBar();
    m_animationFocused = false;
    m_dotFocused = false;
    if (m_panels) m_panels->DrawPanels(m_ctx);       // 패널들(도크스페이스로 도킹)
    DrawWorkspaceDialogs();
    DrawFileDialogs();
}

CommandStack& EditorApp::Commands() {
    // 포커스 문서 스택으로 위임. 문서 없으면 빈 정적 스택(안전한 no-op 대상).
    static CommandStack s_null;
    Document* active = m_project ? m_project->Active() : nullptr;
    return active ? active->Commands() : s_null;
}

// 현재 편집 대상 스택(플레이 중이면 플레이 스택).
CommandStack* EditorApp::ActiveStack() {
    if (m_playMode && m_playMode->IsPlaying())
        return m_playMode->PlayCommandStack();
    if (m_animationFocused) if (auto* doc = AnimationDocument()) return &doc->Commands();
    if (m_dotFocused || m_workspace == Workspace::Dot) if (auto* doc = DotDocumentForEditing()) return &doc->Commands();
    Document* active = m_project ? m_project->Active() : nullptr;
    return active ? &active->Commands() : nullptr;
}

namespace {
// 패널 id → i18n 제목. 알려진 내장 패널은 번역, 그 외(플러그인 등)는 등록 제목 유지.
const char* PanelTitle(const PanelDesc& d) {
    const std::string_view id = d.id;
    if (id == "mye.hierarchy") return mye::i18n::T("panel.hierarchy");
    if (id == "mye.viewport")  return mye::i18n::T("panel.viewport");
    if (id == "mye.inspector") return mye::i18n::T("panel.inspector");
    if (id == "mye.assets")    return mye::i18n::T("panel.assets");
    if (id == "mye.console")   return mye::i18n::T("panel.console");
    if (id == "mye.doteditor") return mye::i18n::T("panel.doteditor");
    return d.title.c_str();
}
} // namespace

IWindow* EditorApp::MainWindowOrNull() {
    if (m_engine && m_engine->GetServiceRaw(kMainWindowServiceId))
        return &m_engine->MainWindow();
    return nullptr;
}

void EditorApp::ToggleFullscreen() {
    IWindow* w = MainWindowOrNull();
    if (!w) return;
    const WindowMode next = (w->GetWindowMode() == WindowMode::BorderlessFullscreen)
        ? WindowMode::Windowed : WindowMode::BorderlessFullscreen;
    w->SetWindowMode(next);
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    using mye::i18n::T;
    if (ImGui::BeginMenu(T("menu.file"))) {
        const bool editing = m_playMode && !m_playMode->IsPlaying();
        const bool open = m_project && m_project->IsOpen();
        if (ImGui::MenuItem(T("file.newproject"), "Ctrl+Shift+N", false, editing)) RequestNewProject();
        if (ImGui::MenuItem(T("file.openproject"), "Ctrl+Shift+O", false, editing)) RequestOpenProject();
        if (ImGui::MenuItem(T("file.openfolder"), nullptr, false, editing)) RequestOpenProject(true);
        if (ImGui::MenuItem(T("file.saveproject"), "Ctrl+Alt+S", false, editing && open)) RequestSaveProject();
        ImGui::Separator();
        if (ImGui::MenuItem(T("file.newscene"), "Ctrl+N", false, editing && open)) NewScene();
        if (ImGui::MenuItem(T("file.openscene"), "Ctrl+O", false, editing && open)) RequestOpenScene();
        if (ImGui::MenuItem(T("file.save"), "Ctrl+S", false, editing && m_project->Active())) SaveActive();
        if (ImGui::MenuItem(T("file.saveas"), "Ctrl+Shift+S", false, editing && m_project->Active())) RequestSaveAs();
        ImGui::Separator();
        if (ImGui::MenuItem(T("file.savelayout"))) ReportFileResult(SaveLayout(), T("file.savelayout"));
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("menu.edit"))) {
        CommandStack* s = ActiveStack();
        const bool canUndo = s && s->CanUndo();
        const bool canRedo = s && s->CanRedo();
        std::string undoLabel = T("edit.undo");
        std::string redoLabel = T("edit.redo");
        if (canUndo) { undoLabel += " ("; undoLabel += std::string(s->UndoLabel()); undoLabel += ")"; }
        if (canRedo) { redoLabel += " ("; redoLabel += std::string(s->RedoLabel()); redoLabel += ")"; }
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo) && s) s->Undo();
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo) && s) s->Redo();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(T("menu.play"))) {
        const bool playing = m_playMode && m_playMode->IsPlaying();
        if (ImGui::MenuItem(T("play.toggle"), "Ctrl+P")) TogglePlay();
        if (ImGui::MenuItem(T("play.pause"), "Ctrl+Shift+P", false, playing)) {
            if (m_playMode->State() == PlayState::Playing) m_playMode->Pause();
            else if (m_playMode->State() == PlayState::Paused) m_playMode->Resume();
        }
        if (ImGui::MenuItem(T("play.step"), "F10", false,
                            playing && m_playMode->State() == PlayState::Paused))
            m_playMode->StepFrame();
        ImGui::EndMenu();
    }

    // Window 메뉴 — 등록된 패널 목록에서 자동 생성(07 §7). 제목은 i18n 키(panel.<id뒷부분>)로 번역.
    if (ImGui::BeginMenu(T("menu.window"))) {
        if (m_panels) {
            for (const PanelDesc& d : m_panels->RegisteredPanels()) {
                const bool open = m_panels->IsOpen(d.id);
                if (ImGui::MenuItem(PanelTitle(d), nullptr, open)) {
                    if (d.id == "mye.doteditor") SelectWorkspace(Workspace::Dot);
                    else if (d.id == "mye.scenes") SelectWorkspace(Workspace::Scenes);
                    else if (d.id == "mye.lua") SelectWorkspace(Workspace::Lua);
                    else if (d.id == "mye.viewport") SelectWorkspace(m_viewport && m_viewport->Camera().perspective ? Workspace::Scene3D : Workspace::Scene2D);
                    else { if (!open) m_panels->Open(d.id); m_panels->Focus(d.id); }
                }
            }
        }
        ImGui::EndMenu();
    }

    // 화면 메뉴 — 전체화면 토글(F11) · 수직동기.
    if (ImGui::BeginMenu(T("menu.display"))) {
        IWindow* w = MainWindowOrNull();
        const bool fs = w && w->GetWindowMode() == WindowMode::BorderlessFullscreen;
        if (ImGui::MenuItem(T("display.fullscreen"), "F11", fs, w != nullptr))
            ToggleFullscreen();
        ImGui::MenuItem(T("display.vsync"), nullptr, &m_vsync);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("도움말")) {
        if (ImGui::MenuItem("게임 제작 가이드")) OpenUserGuide();
        ImGui::EndMenu();
    }

    // 언어 메뉴 — ko/en/ja/zh 전환. 선택 시 도킹 레이아웃이 재구성된다(Version 관찰).
    if (ImGui::BeginMenu(T("menu.language"))) {
        using mye::i18n::Lang;
        const Lang cur = mye::i18n::GetLanguage();
        for (Lang l : {Lang::Ko, Lang::En, Lang::Ja, Lang::Zh}) {
            if (ImGui::MenuItem(mye::i18n::LangName(l), nullptr, cur == l))
                mye::i18n::SetLanguage(l);
        }
        ImGui::EndMenu();
    }

    // 확장이 등록한 메뉴 항목(Tools/... 등)을 최상위 세그먼트별로 얇게 노출.
    if (m_extensions) {
        for (const auto& e : m_extensions->MenuEntries()) {
            // 경로의 첫 세그먼트를 메뉴 이름으로, 마지막 세그먼트를 항목 라벨로.
            const std::string& path = e.path.path;
            std::size_t slash = path.find('/');
            std::string top = slash == std::string::npos ? path : path.substr(0, slash);
            std::string item = slash == std::string::npos ? path : path.substr(path.rfind('/') + 1);
            if (ImGui::BeginMenu(top.c_str())) {
                bool enabled = !e.desc.isEnabled || e.desc.isEnabled();
                if (ImGui::MenuItem(item.c_str(), e.desc.shortcut.c_str(), false, enabled) && e.onClick)
                    e.onClick(m_ctx);
                ImGui::EndMenu();
            }
        }
    }

    if (m_extensions && !m_extensions->ToolbarEntries().empty() && ImGui::BeginMenu("도구")) {
        for (const auto& entry : m_extensions->ToolbarEntries()) {
            const bool enabled = !entry.desc.isEnabled || entry.desc.isEnabled();
            if (ImGui::MenuItem(entry.desc.tooltip.c_str(), nullptr, false, enabled) && entry.onClick) entry.onClick(m_ctx);
        }
        ImGui::EndMenu();
    }
    m_toolbarInMenu = DrawToolbar(true);
    ImGui::EndMainMenuBar();
}

bool EditorApp::DrawToolbar(bool inMenuBar) {
    struct Tab { const char* label; Workspace workspace; };
    static constexpr Tab tabs[] = {{"2D", Workspace::Scene2D}, {"3D", Workspace::Scene3D},
        {"씬", Workspace::Scenes}, {"도트메이커", Workspace::Dot}, {"Lua", Workspace::Lua}};
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const auto padding = ImGui::GetStyle().FramePadding;
    const float verticalPadding = inMenuBar ? 0.0f : padding.y;
    float tabWidth = gap * 4;
    for (const auto& tab : tabs) tabWidth += ImGui::CalcTextSize(tab.label).x + padding.x * 2;
    const float controlsWidth = (ImGui::GetFontSize() + verticalPadding * 2) * 3 + gap * 2;
    const float right = ImGui::GetWindowWidth() - gap - controlsWidth;
    const float left = inMenuBar ? ImGui::GetCursorPosX() + gap : gap;
    if (inMenuBar && right - left < tabWidth + gap) return false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, verticalPadding));
    ImGui::SetCursorPosX(std::max(left, std::min((ImGui::GetWindowWidth() - tabWidth) * .5f, right - tabWidth - gap)));
    for (std::size_t i = 0; i < std::size(tabs); ++i) {
        if (i) ImGui::SameLine();
        if (imgui::EditorTextButton(tabs[i].label, m_workspace == tabs[i].workspace)) SelectWorkspace(tabs[i].workspace);
    }
    ImGui::SameLine(std::max(right, ImGui::GetCursorPosX() + gap));
    const auto state = m_playMode ? m_playMode->State() : PlayState::Edit;
    const bool playing = state != PlayState::Edit;
    ImGui::BeginDisabled(!m_project || !m_project->Active() || state == PlayState::Playing);
    if (imgui::EditorIconButton(imgui::EditorIcon::Play, "##run", state == PlayState::Paused ? "계속 실행" : "실행 (Ctrl+P)", state == PlayState::Playing)) {
        if (state == PlayState::Paused) m_playMode->Resume();
        else { auto result = m_playMode->Play(); if (!result) ReportFileResult(result, ""); }
        RefreshDocumentContext();
        SelectWorkspace(m_viewport && m_viewport->Camera().perspective ? Workspace::Scene3D : Workspace::Scene2D);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing);
    if (imgui::EditorIconButton(imgui::EditorIcon::Pause, "##pause", "일시정지 / 계속 (Ctrl+Shift+P)", state == PlayState::Paused)) {
        if (state == PlayState::Paused) m_playMode->Resume(); else m_playMode->Pause();
    }
    ImGui::SameLine();
    if (imgui::EditorIconButton(imgui::EditorIcon::Stop, "##stop", "중지")) { m_playMode->Stop(); RefreshDocumentContext(); }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    return true;
}

void EditorApp::DrawStatusBar() {
    const auto* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeightWithSpacing();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
    const auto flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##EditorStatus", nullptr, flags)) {
        if (ImGui::SmallButton(m_fileError ? "오류 / 콘솔" : "콘솔")) m_panels->Open("mye.console");
        ImGui::SameLine();
        // One clipped row keeps long paths/errors from resizing the workspace.
        ImGui::TextUnformatted(m_fileStatus.empty() ? "준비" : m_fileStatus.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_fileStatus.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::HandleShortcuts() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    // 텍스트 입력 위젯 포커스 중에는 편집 단축키를 가로채지 않는다.
    if (io.WantTextInput || m_showNewProject || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const bool alt = io.KeyAlt;

    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_N, false)) RequestNewProject();
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_O, false)) RequestOpenProject();
    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_N, false)) NewScene();
    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_O, false)) RequestOpenScene();
    if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveActive();
    if (ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_S, false)) RequestSaveAs();
    if (ctrl && alt && !shift && ImGui::IsKeyPressed(ImGuiKey_S, false)) RequestSaveProject();

    if (CommandStack* s = ActiveStack()) {
        if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) s->Undo();
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                     (shift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
            s->Redo();
    }

    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_P, false)) TogglePlay();
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_P, false) && m_playMode) {
        if (m_playMode->State() == PlayState::Playing) m_playMode->Pause();
        else if (m_playMode->State() == PlayState::Paused) m_playMode->Resume();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false) && m_playMode &&
        m_playMode->State() == PlayState::Paused)
        m_playMode->StepFrame();

    // F11 — 전체화면 토글.
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) ToggleFullscreen();

    // 선택 이력(Alt+←/→).
    if (alt && m_selection) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) m_selection->NavigateBack();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) m_selection->NavigateForward();
    }
}

void EditorApp::NewScene() {
    if (!m_project || !m_project->IsOpen() || (m_playMode && m_playMode->IsPlaying())) return;
    m_showNewScene = true;
    m_fileError = false;
}

void EditorApp::SaveActive() {
    if (m_playMode && m_playMode->IsPlaying()) {
        ReportFileResult(Error{mye::i18n::T("file.stopfirst"), 1}, "");
        return;
    }
    if (!m_animationFocused && (m_dotFocused || m_workspace == Workspace::Dot)) {
        if (auto* doc = DotDocumentForEditing()) {
            if (doc->Path().empty()) RequestSaveAs();
            else ReportFileResult(m_project->SaveDot(doc->Id(), doc->Path()), mye::i18n::T("file.saved"));
        }
        return;
    }
    if (m_animationFocused) {
        if (auto* doc = AnimationDocument()) {
            auto saved = m_project->SaveAnimation(doc->Id(), doc->Path());
            ReportFileResult(saved, mye::i18n::T("file.saved"));
        }
        return;
    }
    Document* active = m_project ? m_project->Active() : nullptr;
    if (!active) return;

    if (active->Path().empty()) RequestSaveAs();
    else ReportFileResult(SaveScene(active->Path()), mye::i18n::T("file.saved"));
}

void EditorApp::TogglePlay() {
    if (!m_playMode) return;
    if (m_playMode->IsPlaying()) m_playMode->Stop();
    else {
        auto started = m_playMode->Play();
        if (!started) ReportFileResult(started, "");
        else SelectWorkspace(m_viewport && m_viewport->Camera().perspective ? Workspace::Scene3D : Workspace::Scene2D);
    }
    RefreshDocumentContext();
}

void EditorApp::Shutdown() {
    // 레이아웃 저장(세션·ini). 셧다운 데드락 규약: 디바이스/윈도우 파괴 전에 UI 상태만 정리.
    auto layout = SaveLayout();
    if (!layout) MYE_LOG_WARN("Editor", "{}", layout.GetError().message);
    if (m_window) m_window->RemoveMessageHook(this);
    m_window = nullptr;

    // 서브시스템 파괴는 선언 역순.
    m_inspector.reset();
    m_extensions.reset();
    m_playMode.reset();
    m_selection.reset();
    m_panels.reset();
    m_project.reset();
}

} // namespace mye::editor
