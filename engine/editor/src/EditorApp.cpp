// EditorApp.cpp — 에디터 앱 셸 (docs/07 §1, §7)
//
// 07 §1·§7: 서브시스템(패널·커맨드·선택·플레이모드·확장·프로젝트)을 소유하고 매 프레임 mye_imgui
//   도킹 셸 위에 메뉴바(File/Edit/View/Play/Window)·툴바(New/Save·Play/Pause/Stop/Step)·도킹
//   스페이스·패널을 빌드한다. 단축키(Ctrl+S/N/Z/Y·Ctrl+P·F10·Alt+←/→)를 처리한다. 레이아웃은
//   <project>/.myeditor/(layout.ini=ImGui ini, session.json=열린 패널 목록)에 저장·복원한다.
//
// 내장 패널도 1급 플러그인 — PanelManager::RegisterFactory 동일 경로로 등록(07 §확장).
#include "mye/editor/EditorApp.h"
#include "mye/editor/CommandStack.h"
#include "mye/core/Module.h"

#include "imgui.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace mye::editor {

// HierarchyPanel.cpp 정의.
std::unique_ptr<IEditorPanelFactory> MakeHierarchyPanelFactory();

EditorApp::EditorApp() = default;
EditorApp::~EditorApp() = default;

Expected<void, Error> EditorApp::Initialize(EngineContext& engine, std::string_view projectPath) {
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

    // 프로젝트 오픈(--project). 실패 시 상위(모듈)가 처리(런처는 P2).
    if (!projectPath.empty()) {
        auto r = m_project->Open(projectPath);
        if (!r) return r.GetError();
    }

    RegisterBuiltinPanels();

    // 프레임 컨텍스트 정적 배선(문서·커맨드·World는 매 프레임 갱신).
    m_ctx.app        = this;
    m_ctx.engine     = m_engine;
    m_ctx.events     = m_events;
    m_ctx.selection  = m_selection.get();
    m_ctx.playMode   = m_playMode.get();
    m_ctx.extensions = m_extensions.get();
    m_ctx.project    = m_project.get();

    RestoreLayout();
    return {};
}

void EditorApp::RegisterBuiltinPanels() {
    // 내장 = 1급 플러그인: 확장 레지스트리 경로가 아니라 PanelManager에 직접 등록(동일 API).
    m_panels->RegisterFactory(MakeHierarchyPanelFactory());
    // 기본 레이아웃: 하이어라키를 연다(인스펙터·뷰포트·콘솔 등은 M4-B 다른 에이전트 패널).
    m_panels->Open("mye.hierarchy");
}

void EditorApp::RestoreLayout() {
    if (!m_project || !m_project->IsOpen()) return;
    // ImGui ini 경로를 프로젝트별 layout.ini로 지정(디렉터리 없으면 생성).
    const std::string dir = m_project->EditorStateDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    m_layoutIniPath = m_project->LayoutIniPath();
    // ImGui가 존재하는 프레임에서만 IO 접근(테스트 경로는 ImGui 미초기화).
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = m_layoutIniPath.c_str();
    }
    // 열린 패널 목록(session.json) 복원.
    const std::string sessionPath = m_project->SessionJsonPath();
    if (std::filesystem::exists(sessionPath, ec)) {
        std::ifstream in(sessionPath, std::ios::binary);
        if (in) {
            std::string text((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            auto parsed = json::Parse(text);
            if (parsed) m_panels->DeserializeLayout(parsed.Value());
        }
    }
}

void EditorApp::SaveLayout() {
    if (!m_project || !m_project->IsOpen()) return;
    json::Value session;
    m_panels->SerializeLayout(session);
    const std::string text = json::Stringify(session);
    const std::string sessionPath = m_project->SessionJsonPath();
    std::error_code ec;
    std::filesystem::create_directories(m_project->EditorStateDir(), ec);
    std::ofstream out(sessionPath, std::ios::binary | std::ios::trunc);
    if (out) out.write(text.data(), static_cast<std::streamsize>(text.size()));
    // ImGui ini는 IniFilename 지정 시 ImGui가 자동 저장하지만, 종료 시 명시 저장도 한다.
    if (ImGui::GetCurrentContext() && !m_layoutIniPath.empty())
        ImGui::SaveIniSettingsToDisk(m_layoutIniPath.c_str());
}

void EditorApp::OnFrame() {
    // 매 프레임 문서·커맨드 컨텍스트 갱신(포커스 문서 기준).
    Document* active = m_project ? m_project->Active() : nullptr;
    m_ctx.activeDocument = active;
    m_ctx.commands = active ? &active->Commands() : nullptr;
    // 활성 문서 스택에 컨텍스트 배선(Execute/Undo가 이 ctx를 커맨드에 전달).
    if (active) active->Commands().SetContext(&m_ctx);
    if (m_playMode && m_playMode->PlayCommandStack())
        m_playMode->PlayCommandStack()->SetContext(&m_ctx);

    HandleShortcuts();
    DrawMenuBar();
    DrawToolbar();
    if (m_panels) m_panels->BuildDockspaceAndDrawAll(m_ctx);
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
    Document* active = m_project ? m_project->Active() : nullptr;
    return active ? &active->Commands() : nullptr;
}

void EditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("파일")) {
        if (ImGui::MenuItem("새 씬", "Ctrl+N")) NewScene();
        if (ImGui::MenuItem("저장", "Ctrl+S")) SaveActive();
        ImGui::Separator();
        if (ImGui::MenuItem("레이아웃 저장")) SaveLayout();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("편집")) {
        CommandStack* s = ActiveStack();
        const bool canUndo = s && s->CanUndo();
        const bool canRedo = s && s->CanRedo();
        std::string undoLabel = "실행 취소";
        std::string redoLabel = "다시 실행";
        if (canUndo) { undoLabel += " ("; undoLabel += std::string(s->UndoLabel()); undoLabel += ")"; }
        if (canRedo) { redoLabel += " ("; redoLabel += std::string(s->RedoLabel()); redoLabel += ")"; }
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo) && s) s->Undo();
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo) && s) s->Redo();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("플레이")) {
        const bool playing = m_playMode && m_playMode->IsPlaying();
        if (ImGui::MenuItem("재생/정지", "Ctrl+P")) TogglePlay();
        if (ImGui::MenuItem("일시정지", "Ctrl+Shift+P", false, playing)) {
            if (m_playMode->State() == PlayState::Playing) m_playMode->Pause();
            else if (m_playMode->State() == PlayState::Paused) m_playMode->Resume();
        }
        if (ImGui::MenuItem("프레임 스텝", "F10", false,
                            playing && m_playMode->State() == PlayState::Paused))
            m_playMode->StepFrame();
        ImGui::EndMenu();
    }

    // Window 메뉴 — 등록된 패널 목록에서 자동 생성(07 §7).
    if (ImGui::BeginMenu("창")) {
        if (m_panels) {
            for (const PanelDesc& d : m_panels->RegisteredPanels()) {
                const bool open = m_panels->IsOpen(d.id);
                if (ImGui::MenuItem(d.title.c_str(), nullptr, open)) {
                    if (!open) m_panels->Open(d.id);
                }
            }
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

    ImGui::EndMainMenuBar();
}

void EditorApp::DrawToolbar() {
    // 메인 메뉴바 아래 고정 툴바(간단 버튼 행). 도킹 대상 아님 — 상단 오버레이 윈도우.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##toolbar", nullptr, flags)) {
        if (ImGui::Button("새 씬")) NewScene();
        ImGui::SameLine();
        if (ImGui::Button("저장")) SaveActive();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        const bool playing = m_playMode && m_playMode->IsPlaying();
        if (ImGui::Button(playing ? "■ 정지"
                                  : "▶ 재생"))
            TogglePlay();
        ImGui::SameLine();
        if (playing) {
            const bool paused = m_playMode->State() == PlayState::Paused;
            if (ImGui::Button(paused ? "▶ 재개"
                                     : "❚❚ 일시정지")) {
                if (paused) m_playMode->Resume(); else m_playMode->Pause();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!paused);
            if (ImGui::Button("스텝")) m_playMode->StepFrame();
            ImGui::EndDisabled();
        }

        // 확장 툴바 버튼.
        if (m_extensions) {
            for (const auto& t : m_extensions->ToolbarEntries()) {
                ImGui::SameLine();
                bool enabled = !t.desc.isEnabled || t.desc.isEnabled();
                ImGui::BeginDisabled(!enabled);
                std::string caption = t.desc.icon.empty() ? t.desc.tooltip : t.desc.icon;
                if (ImGui::Button(caption.c_str()) && t.onClick) t.onClick(m_ctx);
                ImGui::EndDisabled();
            }
        }
    }
    ImGui::End();
}

void EditorApp::HandleShortcuts() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    // 텍스트 입력 위젯 포커스 중에는 편집 단축키를 가로채지 않는다.
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    const bool alt = io.KeyAlt;

    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_N, false)) NewScene();
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveActive();

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

    // 선택 이력(Alt+←/→).
    if (alt && m_selection) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) m_selection->NavigateBack();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) m_selection->NavigateForward();
    }
}

void EditorApp::NewScene() {
    if (!m_project) return;
    Document* doc = m_project->NewScene();
    (void)doc;
}

void EditorApp::SaveActive() {
    Document* active = m_project ? m_project->Active() : nullptr;
    if (!active) return;
    // 실제 씬 파일 기록은 SceneSerializer(다른 에이전트) 경유 — 배선은 M4-B 저장 커맨드에서.
    //   여기서는 저장 지점만 표시해 dirty 플래그를 해제한다(탭 '*' 제거).
    active->Commands().MarkSaved();
    SaveLayout();
}

void EditorApp::TogglePlay() {
    if (!m_playMode) return;
    // 07 §3: SetEditWorld는 EditorModule이 SceneModule::World()로 배선. 편집 World가 없으면
    //   Play는 빈 Play World로 진입(상태 전이만).
    if (m_playMode->IsPlaying()) m_playMode->Stop();
    else m_playMode->Play();
}

void EditorApp::Shutdown() {
    // 레이아웃 저장(세션·ini). 셧다운 데드락 규약: 디바이스/윈도우 파괴 전에 UI 상태만 정리.
    SaveLayout();

    // 서브시스템 파괴는 선언 역순.
    m_inspector.reset();
    m_extensions.reset();
    m_playMode.reset();
    m_selection.reset();
    m_panels.reset();
    m_project.reset();
}

} // namespace mye::editor
