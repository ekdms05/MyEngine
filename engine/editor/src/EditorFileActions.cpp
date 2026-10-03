#include "mye/editor/EditorApp.h"

#include "mye/core/I18n.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Log.h"
#include "mye/core/Module.h"
#include "mye/ecs/World.h"

#include "imgui.h"
#include <Windows.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace mye::editor {
namespace {
using mye::i18n::T;
enum class FileDialog { Project, Scene, SaveScene, Folder, Image, Asset };

Expected<std::string, Error> Browse(IWindow* window, FileDialog kind, std::string_view initial) {
    if (!window) return Error{"File dialogs require an editor window", 1};
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return Error{"Cannot initialize the file dialog", static_cast<int32_t>(initialized)};
    struct ComScope { ~ComScope() { CoUninitialize(); } } comScope;
    IFileDialog* raw = nullptr;
    const bool save = kind == FileDialog::SaveScene;
    HRESULT result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
    if (FAILED(result)) return Error{"Cannot create the file dialog", static_cast<int32_t>(result)};
    const auto release = [](IFileDialog* dialog) { dialog->Release(); };
    std::unique_ptr<IFileDialog, decltype(release)> dialog(raw, release);
    FILEOPENDIALOGOPTIONS options = FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    if (kind == FileDialog::Folder) options |= FOS_PICKFOLDERS;
    else if (save) options |= FOS_OVERWRITEPROMPT;
    else options |= FOS_FILEMUSTEXIST;
    result = dialog->SetOptions(options);
    if (FAILED(result)) return Error{"Cannot configure the file dialog", static_cast<int32_t>(result)};
    if (kind != FileDialog::Folder) {
        const COMDLG_FILTERSPEC filter = kind == FileDialog::Project
            ? COMDLG_FILTERSPEC{L"MyEngine project", L"*.myeproj"}
            : kind == FileDialog::Image ? COMDLG_FILTERSPEC{L"PNG image", L"*.png"}
            : kind == FileDialog::Asset ? COMDLG_FILTERSPEC{L"Game assets (PNG, animation, UI, Lua, WAV, GLB, glTF)", L"*.png;*.anim;*.animstate;*.ui;*.lua;*.wav;*.glb;*.gltf"}
            : COMDLG_FILTERSPEC{L"MyEngine scene", L"*.scene"};
        result = dialog->SetFileTypes(1, &filter);
        if (FAILED(result)) return Error{"Cannot set the file filter", static_cast<int32_t>(result)};
        result = dialog->SetDefaultExtension(kind == FileDialog::Project ? L"myeproj" : kind == FileDialog::Image ? L"png" : L"scene");
        if (FAILED(result)) return Error{"Cannot set the file extension", static_cast<int32_t>(result)};
    }
    const auto path = Utf8Path(initial);
    std::error_code ec;
    const auto folder = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
    if (!folder.empty()) {
        IShellItem* item = nullptr;
        result = SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (SUCCEEDED(result)) {
            result = dialog->SetFolder(item);
            item->Release();
            if (FAILED(result)) return Error{"Cannot select the initial folder", static_cast<int32_t>(result)};
        }
    }
    if (save && !path.filename().empty()) {
        result = dialog->SetFileName(path.filename().c_str());
        if (FAILED(result)) return Error{"Cannot set the scene name", static_cast<int32_t>(result)};
    }
    result = dialog->Show(static_cast<HWND>(window->GetNativeHandle()));
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::string{};
    if (FAILED(result)) return Error{"File dialog failed", static_cast<int32_t>(result)};
    IShellItem* selected = nullptr;
    result = dialog->GetResult(&selected);
    if (FAILED(result)) return Error{"Cannot read the selected path", static_cast<int32_t>(result)};
    PWSTR text = nullptr;
    result = selected->GetDisplayName(SIGDN_FILESYSPATH, &text);
    selected->Release();
    if (FAILED(result)) return Error{"Cannot read the selected path", static_cast<int32_t>(result)};
    const std::string utf8 = Utf8String(std::filesystem::path(text));
    CoTaskMemFree(text);
    return utf8;
}
}

void EditorApp::RefreshDocumentContext() {
    Document* doc = m_project ? m_project->Active() : nullptr;
    ecs::World* world = doc ? &doc->World() : nullptr;
    if (m_playMode && !m_playMode->IsPlaying()) m_playMode->SetEditWorld(world);
    m_ctx.project = m_project.get();
    m_ctx.activeDocument = doc;
    m_ctx.commands = doc ? &doc->Commands() : nullptr;
    const auto displayWorld = m_ctx.activeWorld();
    const bool changed = displayWorld != m_boundWorld;
    m_boundWorld = displayWorld;
    if (changed && m_selection) m_selection->Reset();
    if (doc) doc->Commands().SetContext(&m_ctx);
    if (m_playMode && m_playMode->PlayCommandStack())
        m_playMode->PlayCommandStack()->SetContext(&m_ctx);
}

void EditorApp::ActivateDocument(DocumentId id) {
    if (!m_project || m_playMode->IsPlaying()) return;
    m_focusedAsset = {};
    m_project->SetActive(id);
    RefreshDocumentContext();
    m_selectDocumentTab = true;
}

Expected<void, Error> EditorApp::CreateProject(std::string_view name, std::string_view directory,
                                             bool discardUnsaved, bool useStarter) {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    auto layout = SaveLayout();
    if (!layout) return layout.GetError();
    auto created = m_project->Create(name, directory, discardUnsaved, useStarter ? m_templateDirectory : "");
    if (!created) return created.GetError();
    m_showInputSettings = false;
    m_animationId = {};
    m_uiId = {};
    SelectWorkspace(Workspace::Scene2D);
    RestoreLayout();
    RefreshDocumentContext();
    m_selectDocumentTab = true;
    ExpandEditorWindow();
    return {};
}

Expected<void, Error> EditorApp::OpenProject(std::string_view path, bool discardUnsaved) {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    auto layout = SaveLayout();
    if (!layout) return layout.GetError();
    auto opened = m_project->Open(path, discardUnsaved);
    if (!opened) return opened.GetError();
    m_showInputSettings = false;
    m_animationId = {};
    m_uiId = {};
    SelectWorkspace(Workspace::Scene2D);
    RestoreLayout();
    RefreshDocumentContext();
    m_selectDocumentTab = true;
    ExpandEditorWindow();
    return {};
}

Expected<void, Error> EditorApp::OpenScene(std::string_view path) {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    auto opened = m_project->OpenScene(path);
    if (!opened) return opened.GetError();
    ActivateDocument(opened.Value()->Id());
    return {};
}

Document* EditorApp::AnimationDocument() {
    if (!m_project) return nullptr;
    for (auto* doc : m_project->Documents())
        if (doc->Id() == m_animationId && doc->GetKind() == Document::Kind::Asset) return doc;
    return nullptr;
}

Expected<void, Error> EditorApp::OpenAnimation(std::string_view path) {
    auto opened = m_project->OpenAnimation(path);
    if (!opened) return opened.GetError();
    m_animationId = opened.Value()->Id();
    opened.Value()->Commands().SetContext(&m_ctx);
    m_panels->Open("mye.anim");
    m_panels->Focus("mye.anim");
    return {};
}

Document* EditorApp::FocusedAssetDocument() {
    if (m_project) for (auto* doc : m_project->Documents())
        if (doc->Id() == m_focusedAsset && doc->GetKind() != Document::Kind::Scene) return doc;
    return nullptr;
}

Document* EditorApp::UiDocument() {
    if (m_project) for (auto* doc : m_project->Documents())
        if (doc->Id() == m_uiId && doc->GetKind() == Document::Kind::Ui) return doc;
    return nullptr;
}

Expected<void, Error> EditorApp::OpenUi(std::string_view path) {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    auto opened = m_project->OpenUi(path);
    if (!opened) return opened.GetError();
    m_uiId = opened.Value()->Id();
    opened.Value()->Commands().SetContext(&m_ctx);
    m_panels->Open("mye.ui");
    m_panels->Focus("mye.ui");
    return {};
}

Expected<std::string, Error> EditorApp::BrowseImageFile() {
    return Browse(m_window, FileDialog::Image, Utf8String(Utf8Path(m_project->RootDir()) / "assets"));
}
Expected<std::string, Error> EditorApp::BrowseAssetFile() {
    return Browse(m_window, FileDialog::Asset, Utf8String(Utf8Path(m_project->RootDir()) / "assets"));
}
Expected<void, Error> EditorApp::SaveScene(std::string_view path) {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    Document* doc = m_project->Active();
    if (!doc) return Error{"No active scene", 1};
    auto saved = m_project->SaveScene(doc->Id(), path);
    if (!saved) return saved.GetError();
    auto layout = SaveLayout();
    if (!layout) MYE_LOG_WARN("Editor", "Scene saved; layout save failed: {}", layout.GetError().message);
    return {};
}

Expected<void, Error> EditorApp::SaveProject() {
    if (m_playMode->IsPlaying()) return Error{T("file.stopfirst"), 1};
    auto saved = m_project->Save();
    if (!saved) return saved.GetError();
    auto layout = SaveLayout();
    if (!layout) MYE_LOG_WARN("Editor", "Project saved; layout save failed: {}", layout.GetError().message);
    return {};
}

void EditorApp::ReportFileResult(const Expected<void, Error>& result, std::string_view success) {
    m_fileError = !result;
    m_fileStatus = result ? std::string(success) : result.GetError().message;
    if (result) MYE_LOG_INFO("Editor", "{}", m_fileStatus);
    else MYE_LOG_ERROR("Editor", "{}", m_fileStatus);
}

bool EditorApp::ConfirmProjectChange() {
    if (!m_project->HasUnsavedChanges()) return true;
    if (!m_window) return false;
    const int choice = MessageBoxW(static_cast<HWND>(m_window->GetNativeHandle()),
                                  Utf8Path(T("file.unsavedprompt")).c_str(), L"MyEditor",
                                  MB_YESNOCANCEL | MB_ICONWARNING);
    if (choice == IDNO) return true;
    if (choice != IDYES) return false;
    RequestSaveProject();
    return !m_fileError && !m_project->HasUnsavedChanges();
}

bool EditorApp::OnMessage(void*, uint32_t msg, uint64_t, int64_t) {
    return msg == WM_CLOSE && !ConfirmProjectChange();
}

void EditorApp::RequestNewProject() {
    if (m_playMode->IsPlaying()) return;
    std::snprintf(m_newProjectName.data(), m_newProjectName.size(), "%s", "New Project");
    const auto base = Utf8Path(m_engine->GetPaths().userDir) / "projects" / "NewProject";
    std::snprintf(m_newProjectDirectory.data(), m_newProjectDirectory.size(), "%s", Utf8String(base).c_str());
    m_fileStatus.clear();
    m_fileError = false;
    m_showNewProject = true;
}

void EditorApp::RequestOpenProject(bool folder) {
    if (m_playMode->IsPlaying()) return;
    auto path = Browse(m_window, folder ? FileDialog::Folder : FileDialog::Project, m_project->RootDir());
    if (!path) { ReportFileResult(path.GetError(), ""); return; }
    if (path.Value().empty() || !ConfirmProjectChange()) return;
    ReportFileResult(OpenProject(path.Value(), true), T("file.opened"));
}

void EditorApp::RequestOpenScene() {
    if (!m_project->IsOpen() || m_playMode->IsPlaying()) return;
    auto path = Browse(m_window, FileDialog::Scene,
                       Utf8String(Utf8Path(m_project->RootDir()) / "assets" / "scenes"));
    if (!path) { ReportFileResult(path.GetError(), ""); return; }
    if (!path.Value().empty()) ReportFileResult(OpenScene(path.Value()), T("file.opened"));
}

void EditorApp::OpenUserGuide() {
    const auto guide = Utf8Path(m_engine->GetPaths().engineDir) / "docs/guide/index.html";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(guide, ec) || ec) {
        ReportFileResult(Error{"제작 가이드 파일을 찾을 수 없습니다: " + Utf8String(guide), 1}, "");
        return;
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", guide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) ReportFileResult(Error{"제작 가이드를 열 수 없습니다", static_cast<int32_t>(result)}, "");
}

void EditorApp::RequestSaveAs() {
    if (m_playMode->IsPlaying()) return;
    if (auto* doc = FocusedAssetDocument()) {
        if (doc->GetKind() == Document::Kind::Ui) { m_panels->Open("mye.ui"); m_panels->Focus("mye.ui"); ReportFileResult(Error{"UI 패널의 저장 경로를 지정한 뒤 저장하세요.", 1}, ""); return; }
        m_panels->Open("mye.anim");
        ReportFileResult(Error{"애니메이션 패널에서 저장 경로를 지정하세요", 1}, "");
        return;
    }
    if (m_playMode->IsPlaying()) return;
    Document* doc = m_project->Active();
    if (!doc) return;
    std::string initial(doc->Path());
    if (initial.empty()) initial = Utf8String(Utf8Path(m_project->RootDir()) / "assets" / "scenes" /
                                             ("untitled-" + std::to_string(doc->Id().value) + ".scene"));
    auto path = Browse(m_window, FileDialog::SaveScene, initial);
    if (!path) { ReportFileResult(path.GetError(), ""); return; }
    if (!path.Value().empty()) ReportFileResult(SaveScene(path.Value()), T("file.saved"));
}

void EditorApp::RequestSaveProject() {
    if (!m_project->IsOpen() || m_playMode->IsPlaying()) {
        ReportFileResult(Error{T("file.stopfirst"), 1}, "");
        return;
    }
    for (Document* doc : m_project->Documents()) {
        if (!doc->Path().empty()) continue;
        if (doc->GetKind() != Document::Kind::Scene) {
            if (doc->GetKind() == Document::Kind::Ui) { m_uiId = doc->Id(); m_panels->Open("mye.ui"); m_panels->Focus("mye.ui"); ReportFileResult(Error{"Save the new UI in its panel first", 1}, ""); }
            else { m_animationId = doc->Id(); m_panels->Open("mye.anim"); ReportFileResult(Error{"Save the new animation in the animation panel first", 1}, ""); }
            if (!doc->Path().empty() && !m_fileError) continue;
            return;
        }
        ActivateDocument(doc->Id());
        RequestSaveAs();
        if (doc->Path().empty() || m_fileError) return;
    }
    ReportFileResult(SaveProject(), T("file.saved"));
}

void EditorApp::DrawDocumentTabs() {
    if (!m_project->IsOpen()) {
        ImGui::TextWrapped("%s", T("file.welcome"));
        return;
    }
    if (!ImGui::BeginTabBar("##scene_documents")) return;
    const bool selectRequested = m_selectDocumentTab;
    const Document* active = m_project->Active();
    DocumentId selected{};
    for (Document* doc : m_project->Documents()) {
        if (doc->GetKind() != Document::Kind::Scene) continue;
        ImGui::PushID(static_cast<int>(doc->Id().value));
        ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
        if (selectRequested && doc == active) flags |= ImGuiTabItemFlags_SetSelected;
        const auto label = doc->TabTitle() + "###scene_document";
        if (ImGui::BeginTabItem(label.c_str(), nullptr, flags)) {
            selected = doc->Id();
            ImGui::EndTabItem();
        }
        ImGui::PopID();
    }
    ImGui::EndTabBar();
    m_selectDocumentTab = false;
    if (!selectRequested && selected.IsValid() && (!active || selected != active->Id())) {
        ActivateDocument(selected);
        m_selectDocumentTab = false;
    }
}

void EditorApp::DrawFileDialogs() {
    const std::string title = std::string(T("file.newproject")) + "###new_project";
    if (m_showNewProject) {
        ImGui::OpenPopup(title.c_str());
        m_showNewProject = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText(T("file.projectname"), m_newProjectName.data(), m_newProjectName.size());
    ImGui::InputText(T("file.projectfolder"), m_newProjectDirectory.data(), m_newProjectDirectory.size());
    if (ImGui::Button(T("file.browsefolder"))) {
        auto folder = Browse(m_window, FileDialog::Folder, m_newProjectDirectory.data());
        if (!folder) ReportFileResult(folder.GetError(), "");
        else if (!folder.Value().empty())
            std::snprintf(m_newProjectDirectory.data(), m_newProjectDirectory.size(), "%s", folder.Value().c_str());
    }
    if (!m_templateDirectory.empty()) ImGui::Checkbox("초원마을 기본 에셋 포함", &m_useStarter);
    ImGui::TextWrapped("%s", T("file.emptyfolder"));
    if (m_fileError) ImGui::TextWrapped("%s: %s", T("file.error"), m_fileStatus.c_str());
    ImGui::BeginDisabled(m_newProjectName[0] == '\0' || m_newProjectDirectory[0] == '\0');
    if (ImGui::Button(T("file.create")) && ConfirmProjectChange()) {
        auto result = CreateProject(m_newProjectName.data(), m_newProjectDirectory.data(), true, m_useStarter);
        ReportFileResult(result, T("file.created"));
        if (result) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(T("file.cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace mye::editor
