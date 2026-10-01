// AssetBrowserPanel.cpp — 에셋 브라우저 패널(VFS 폴더 트리·파일 목록·검색·드래그드롭)
//   (docs/07 에셋 브라우저)
//
// 07 에셋 브라우저: VFS assets:// 마운트를 폴더 트리 + 파일 목록으로 표시한다. 파일 아이콘은
//   확장자별(v1은 텍스트 아이콘 — 텍스처 썸네일은 디바이스 접근이 필요한 뷰포트 배선과 함께
//   후속). 검색으로 필터. 각 파일은 드래그 소스("MYE_ASSET" 페이로드 = vpath). 씬 뷰포트가
//   드롭을 받아 종류별 커맨드를 발행한다:
//     - 스프라이트(.png/.jpg/...) → SpriteRenderer 엔티티 생성
//     - .prefab                  → 프리팹 인스턴스화
//     - .lua                     → ScriptComponent 부착 엔티티 생성
//   드롭 처리(커맨드 발행)는 InstantiateAssetToWorld() 로 노출해 뷰포트 에이전트가 호출한다.
//
// 소유: 내장 패널도 1급 플러그인 — IEditorPanelFactory 로 등록(RegisterBuiltinPanels 경유).
#include "mye/editor/Panel.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/Viewport.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/Command.h"
#include "mye/core/I18n.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Prefab.h"
#include "mye/editor/Project.h"
#include "mye/editor/ProjectAssetOperations.h"
#include "mye/imgui/EditorWidgets.h"

#include "mye/asset/FileSystem.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetGuid.h"

#include "mye/core/Module.h"
#include "mye/core/Log.h"
#include "mye/core/JsonFile.h"

#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/refl/TypeId.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"

#include "imgui.h"
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace mye::editor {

namespace {

const PanelDesc kAssetBrowserDesc{
    /*id*/ "mye.assets",
    /*title*/ "에셋",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::LeftBottom,
};

CommandStack* Stack(EditorContext& ctx) {
    if (ctx.playMode && ctx.playMode->IsPlaying())
        return ctx.playMode->PlayCommandStack();
    return ctx.commands;
}

// 파일 확장자(소문자, 점 없이). 없으면 빈 문자열.
std::string ExtOf(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return {};
    std::string e(path.substr(dot + 1));
    std::transform(e.begin(), e.end(), e.begin(),
                   [](char c) { return static_cast<char>(std::tolower((unsigned char)c)); });
    return e;
}

bool IsTextureExt(const std::string& e) {
    return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "tga" ||
           e == "gif" || e == "psd";
}
bool IsSpriteAssetExt(const std::string& e) {
    return IsTextureExt(e) || e == "sprite" || e == "spr";
}

// 확장자 → 아이콘 텍스트(v1 — 텍스처 썸네일은 후속).
const char* IconFor(const std::string& e) {
    if (IsTextureExt(e))       return "[IMG]";
    if (e == "prefab")          return "[PFB]";
    if (e == "lua")             return "[LUA]";
    if (e == "scene")           return "[SCN]";
    if (e == "wav" || e == "ogg" || e == "mp3") return "[SND]";
    if (e == "obj" || e == "fbx" || e == "gltf" || e == "glb") return "[MSH]";
    return "[   ]";
}

bool ContainsCI(std::string_view s, std::string_view q) {
    if (q.empty()) return true;
    auto it = std::search(s.begin(), s.end(), q.begin(), q.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) ==
                                     std::tolower((unsigned char)b);
                          });
    return it != s.end();
}

// -----------------------------------------------------------------------------
// 커맨드: 에셋 드롭으로 생성되는 엔티티/인스턴스(Undo 가능). Custom kind — 병합 불필요.
// -----------------------------------------------------------------------------

// 스프라이트 에셋 → SpriteRenderer 엔티티 생성. Undo 시 파괴.
class CreateSpriteEntityCommand final : public IEditorCommand {
public:
    CreateSpriteEntityCommand(asset::AssetRef sprite, Vec2 worldPos, std::string label)
        : m_sprite(sprite), m_pos(worldPos), m_label(std::move(label)) {}

    void Execute(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world) return;
        m_created = world->Create();
        auto& lt = world->Add<scene::LocalTransform>(m_created);
        lt.position = Vec3{m_pos.x, m_pos.y, 0.0f};
        world->Add<scene::WorldTransform>(m_created);
        auto& sr = world->Add<scene::SpriteRenderer>(m_created);
        sr.sprite = m_sprite;
    }
    void Undo(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world || m_created.IsNull()) return;
        if (world->Valid(m_created)) world->Destroy(m_created);
        m_created = ecs::Entity::Null();
    }
    std::string_view Label() const override { return m_label; }
    ecs::Entity Created() const { return m_created; }

private:
    asset::AssetRef m_sprite;
    Vec2            m_pos;
    ecs::Entity     m_created = ecs::Entity::Null();
    std::string     m_label;
};

// 프리팹(.prefab) → 씬 인스턴스화. Undo 시 인스턴스 서브트리 파괴.
class InstantiatePrefabCommand final : public IEditorCommand {
public:
    InstantiatePrefabCommand(std::string osPath, Vec2 worldPos, std::string label)
        : m_osPath(std::move(osPath)), m_pos(worldPos), m_label(std::move(label)) {}

    void Execute(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world) return;
        auto asset = PrefabAsset::LoadFromFile(m_osPath);
        if (!asset) {
            MYE_LOG_ERROR("Editor", "프리팹 로드 실패: {}", asset.GetError().message);
            return;
        }
        auto inst = asset.Value().Instantiate(*world);
        if (!inst) {
            MYE_LOG_ERROR("Editor", "프리팹 인스턴스화 실패: {}", inst.GetError().message);
            return;
        }
        m_root = inst.Value();
        // 루트 위치 이동(있으면).
        if (!m_root.IsNull() && world->Valid(m_root)) {
            if (auto* lt = world->TryGet<scene::LocalTransform>(m_root))
                lt->position = Vec3{m_pos.x, m_pos.y, lt->position.z};
        }
    }
    void Undo(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world || m_root.IsNull()) return;
        DestroySubtree(*world, m_root);
        m_root = ecs::Entity::Null();
    }
    std::string_view Label() const override { return m_label; }

private:
    static void DestroySubtree(ecs::World& world, ecs::Entity root) {
        if (!world.Valid(root)) return;
        if (auto* ch = world.TryGet<scene::Children>(root)) {
            std::vector<ecs::Entity> kids = ch->list;
            for (ecs::Entity k : kids) DestroySubtree(world, k);
        }
        scene::ApplyReparent(world, root, ecs::Entity::Null(), /*keepWorld*/ false);
        world.Destroy(root);
    }

    std::string m_osPath;
    Vec2        m_pos;
    ecs::Entity m_root = ecs::Entity::Null();
    std::string m_label;
};

// .lua → ScriptComponent 부착 엔티티. ScriptComponent 는 리플렉션 등록 타입으로만 접근(mye_script
//   헤더/링크 결합 회피). 미등록이면 빈 엔티티만 생성(안전 fallback).
class CreateScriptEntityCommand final : public IEditorCommand {
public:
    CreateScriptEntityCommand(std::string vpath, Vec2 worldPos, std::string label)
        : m_vpath(std::move(vpath)), m_pos(worldPos), m_label(std::move(label)) {}

    void Execute(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world) return;
        m_created = world->Create();
        auto& lt = world->Add<scene::LocalTransform>(m_created);
        lt.position = Vec3{m_pos.x, m_pos.y, 0.0f};
        world->Add<scene::WorldTransform>(m_created);
        // ScriptComponent 를 리플렉션 이름으로 부착(등록돼 있을 때만).
        const auto scriptId =
            static_cast<ecs::ComponentTypeId>(refl::TypeIdFromName("ScriptComponent"));
        if (world->IsRegistered(scriptId))
            world->AddDynamic(m_created, scriptId);
        else
            MYE_LOG_WARN("Editor", "ScriptComponent 미등록 — 빈 엔티티만 생성({})", m_vpath);
    }
    void Undo(EditorContext& ctx) override {
        ecs::World* world = ctx.activeWorld();
        if (!world || m_created.IsNull()) return;
        if (world->Valid(m_created)) world->Destroy(m_created);
        m_created = ecs::Entity::Null();
    }
    std::string_view Label() const override { return m_label; }

private:
    std::string m_vpath;
    Vec2        m_pos;
    ecs::Entity m_created = ecs::Entity::Null();
    std::string m_label;
};

// vpath → OS 경로(프로젝트 루트 기준). VFS 는 절대 OS 경로를 직접 주지 않으므로 프리팹 로드는
//   프로젝트 루트 + assets 상대경로로 구성한다(assets:// 마운트가 <root>/assets 라는 규약 전제).
std::string VpathToOsPath(EditorContext& ctx, std::string_view vpath) {
    // "assets://foo/bar.prefab" → "<projectRoot>/assets/foo/bar.prefab"
    auto split = asset::VirtualFileSystem::SplitVpath(vpath);
    if (!split) return std::string(vpath);
    const std::string& mount = split.Value().first;
    const std::string& rel = split.Value().second;
    std::string root;
    if (ctx.project) root = std::string(ctx.project->RootDir());
    if (root.empty()) return std::string(vpath);
    if (!root.empty() && root.back() != '/' && root.back() != '\\') root += '/';
    return root + mount + "/" + rel;
}

class AssetBrowserPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kAssetBrowserDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin(PanelWindowTitle("panel.assets", "mye.assets").c_str())) { ImGui::End(); return; }
        const std::string assetsRoot = AssetsRootOf(ctx);
        if (assetsRoot.empty() || !ctx.app) { ImGui::TextDisabled("프로젝트를 열면 에셋을 추가할 수 있습니다."); ImGui::End(); return; }
        const std::string projectRoot(ctx.project->RootDir());
        if (projectRoot != m_projectRoot) { m_projectRoot = projectRoot; m_status.clear(); m_deletePath.clear(); m_folderRequested = false; }
        std::error_code ec;
        const auto folder = Utf8Path(m_currentFolder);
        bool safe = !folder.is_absolute() && !folder.has_root_name() && m_currentFolder.find(':') == std::string::npos;
        for (const auto& segment : folder) if (segment == ".." || segment == ".") safe = false;
        if (!safe || !std::filesystem::is_directory(Utf8Path(assetsRoot) / folder, ec)) m_currentFolder.clear();
        const bool editing = !ctx.playMode || !ctx.playMode->IsPlaying();
        ImGui::BeginDisabled(!editing);
        if (imgui::EditorButton(imgui::EditorIcon::Open, "에셋 추가")) Import(ctx);
        ImGui::EndDisabled();
        if (!editing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("플레이를 중지한 뒤 에셋 파일을 변경할 수 있습니다.");
        ImGui::SameLine();
        if (ImGui::Button("새로 고침")) Refresh(ctx);
        ImGui::Spacing();
        char search[256]; std::snprintf(search, sizeof(search), "%s", m_search.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##asset_search", "파일 이름 검색", search, sizeof(search))) m_search = search;
        if (!m_currentFolder.empty()) {
            if (ImGui::SmallButton("상위 폴더")) { m_currentFolder = Utf8String(Utf8Path(m_currentFolder).parent_path()); m_search.clear(); }
        }
        ImGui::TextWrapped("%s", m_currentFolder.empty() ? "assets://" : ("assets://" + m_currentFolder).c_str());
        ImGui::TextDisabled("우클릭으로 추가·폴더 만들기·삭제");
        if (!m_status.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_status.c_str());
        }
        ImGui::Separator();
        if (ImGui::BeginChild("##asset_files", ImVec2(0, 0))) DrawFileList(ctx, assetsRoot, editing);
        ImGui::EndChild();
        DrawDialogs(ctx);
        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.assets"));
        o["folder"] = json::Value(m_currentFolder);
        out = json::Value(std::move(o));
    }
    void DeserializeState(const json::Value& in) override {
        if (const json::Value* f = in.Find("folder"); f && f->IsString())
            m_currentFolder = f->AsString();
    }

private:
    // <projectRoot>/assets 의 OS 경로. assets:// 마운트가 이 디렉터리라는 규약(04) 전제.
    static std::string AssetsRootOf(EditorContext& ctx) {
        if (!ctx.project) return {};
        std::string root(ctx.project->RootDir());
        if (root.empty()) return {};
        std::error_code ec;
        const auto p = Utf8Path(root) / "assets";
        if (!std::filesystem::exists(p, ec)) return {};
        return Utf8String(p);
    }

    void SetResult(const Expected<void, Error>& result, const char* success) {
        m_status = result ? success : result.GetError().message;
        if (!result) MYE_LOG_ERROR("Editor", "{}", m_status);
    }
    void Refresh(EditorContext& ctx) {
        if (!ctx.app->Viewport()) { m_status = "에셋 인덱스가 아직 준비되지 않았습니다."; return; }
        SetResult(ctx.app->Viewport()->RefreshAssetIndex(), "에셋 목록을 새로 읽었습니다.");
    }
    void Import(EditorContext& ctx) {
        auto source = ctx.app->BrowseAssetFile();
        if (!source) { m_status = source.GetError().message; return; }
        if (source.Value().empty()) return;
        const auto name = Utf8String(Utf8Path(source.Value()).filename());
        const auto destination = m_currentFolder.empty() ? name : m_currentFolder + "/" + name;
        auto imported = ImportProjectAsset(ctx.project->RootDir(), source.Value(), destination);
        SetResult(imported, "에셋을 추가했습니다.");
        if (imported) Refresh(ctx);
    }
    void FolderActions(EditorContext& ctx, const std::string& folder, bool editing) {
        ImGui::BeginDisabled(!editing);
        if (ImGui::MenuItem("에셋 추가...")) { m_currentFolder = folder; Import(ctx); }
        if (ImGui::MenuItem("새 폴더...")) { m_currentFolder = folder; m_folderRequested = true; m_folderName[0] = '\0'; m_dialogError.clear(); }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("탐색기에서 열기")) SetResult(RevealProjectAssetFolder(ctx.project->RootDir(), folder), "탐색기에서 폴더를 열었습니다.");
    }
    void DrawFileList(EditorContext& ctx, const std::string& assetsRoot, bool editing) {
        std::vector<std::string> directories, files;
        EnumerateDir(assetsRoot, m_currentFolder, true, directories);
        EnumerateDir(assetsRoot, m_currentFolder, false, files);
        size_t displayed = 0;
        std::string nextFolder;
        bool navigate = false;
        for (const auto& directory : directories) {
            if (!ContainsCI(directory, m_search)) continue;
            ++displayed;
            ImGui::PushID(directory.c_str());
            const auto path = m_currentFolder.empty() ? directory : m_currentFolder + "/" + directory;
            if (ImGui::Selectable(("폴더  " + directory + "/").c_str(), false)) { nextFolder = path; navigate = true; }
            if (ImGui::BeginPopupContextItem("folder_context")) { FolderActions(ctx, path, editing); ImGui::EndPopup(); }
            ImGui::PopID();
        }
        for (const auto& name : files) {
            if (!ContainsCI(name, m_search)) continue;
            ++displayed;
            const auto extension = ExtOf(name);
            const auto vpath = MakeVpath(m_currentFolder, name);
            ImGui::PushID(name.c_str());
            ImGui::Selectable((std::string(IconFor(extension)) + "  " + name).c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (extension == "anim") SetResult(ctx.app->OpenAnimation(VpathToOsPath(ctx, vpath)), "애니메이션을 열었습니다.");
                else if (extension == "scene") SetResult(ctx.app->OpenScene(VpathToOsPath(ctx, vpath)), "씬을 열었습니다.");
            }
            if (ImGui::BeginPopupContextItem("file_context")) {
                if (extension == "anim" && ImGui::MenuItem("애니메이션 열기")) SetResult(ctx.app->OpenAnimation(VpathToOsPath(ctx, vpath)), "애니메이션을 열었습니다.");
                if (extension == "scene" && ImGui::MenuItem("씬 열기")) SetResult(ctx.app->OpenScene(VpathToOsPath(ctx, vpath)), "씬을 열었습니다.");
                FolderActions(ctx, m_currentFolder, editing);
                ImGui::Separator();
                ImGui::BeginDisabled(!editing);
                if (ImGui::MenuItem("휴지통으로 이동...")) {
                    m_deletePath = m_currentFolder.empty() ? name : m_currentFolder + "/" + name;
                    m_deleteRequested = true;
                    auto checked = CheckProjectAssetDeletion(*ctx.project, m_deletePath);
                    m_dialogError = checked ? std::string{} : checked.GetError().message;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("MYE_ASSET", vpath.c_str(), vpath.size() + 1);
                ImGui::TextUnformatted(name.c_str()); ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
        if (displayed == 0) ImGui::TextWrapped("%s", m_search.empty() ? "에셋이 없습니다. 우클릭 또는 에셋 추가를 사용하세요." : "검색과 일치하는 파일이 없습니다.");
        if (ImGui::BeginPopupContextWindow("asset_context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) { FolderActions(ctx, m_currentFolder, editing); ImGui::EndPopup(); }
        if (navigate) { m_currentFolder = std::move(nextFolder); m_search.clear(); }
    }
    void DrawDialogs(EditorContext& ctx) {
        if (m_folderRequested) { ImGui::OpenPopup("폴더 만들기###asset_new_folder"); m_folderRequested = false; }
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 27, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("폴더 만들기###asset_new_folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("현재 에셋 폴더에 새 폴더를 만듭니다.");
            ImGui::Spacing();
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            const bool enter = ImGui::InputTextWithHint("##folder_name", "폴더 이름", m_folderName.data(), m_folderName.size(), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!m_dialogError.empty()) ImGui::TextWrapped("%s", m_dialogError.c_str());
            ImGui::Spacing();
            const bool create = ImGui::Button("만들기"); ImGui::SameLine();
            if (ImGui::Button("취소") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (create || enter) {
                const std::string name(m_folderName.data());
                Expected<void, Error> result = name.find_first_of("/\\:") != std::string::npos ? Expected<void, Error>(Error{"폴더 이름에는 경로 구분자를 사용할 수 없습니다.", 1})
                    : CreateProjectAssetFolder(ctx.project->RootDir(), m_currentFolder.empty() ? name : m_currentFolder + "/" + name);
                m_dialogError = result ? std::string{} : result.GetError().message;
                if (result) { m_status = "폴더를 만들었습니다."; ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
        if (m_deleteRequested) { ImGui::OpenPopup("에셋 삭제###asset_delete"); m_deleteRequested = false; }
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("에셋 삭제###asset_delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", m_deletePath.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("파일과 .meta를 함께 휴지통으로 이동합니다. 복구는 Windows 휴지통에서 진행하세요.");
            if (!m_dialogError.empty()) ImGui::TextWrapped("%s", m_dialogError.c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(!m_dialogError.empty() || (ctx.playMode && ctx.playMode->IsPlaying()));
            const bool remove = ImGui::Button("휴지통으로 이동"); ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("취소") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (remove) {
                auto result = RecycleProjectAsset(*ctx.project, m_deletePath);
                SetResult(result, "에셋을 휴지통으로 이동했습니다.");
                if (result) { Refresh(ctx); ImGui::CloseCurrentPopup(); }
                else m_dialogError = result.GetError().message;
            }
            ImGui::EndPopup();
        }
    }

    // assetsRoot/dir OS 디렉터리를 스캔. wantDirs=true면 하위 폴더명, false면 파일명.
    static void EnumerateDir(const std::string& assetsRoot, const std::string& dir,
                             bool wantDirs, std::vector<std::string>& out) {
        std::error_code ec;
        auto base = Utf8Path(assetsRoot);
        if (!dir.empty()) base /= Utf8Path(dir);
        if (!std::filesystem::exists(base, ec)) return;
        for (const auto& e : std::filesystem::directory_iterator(base, ec)) {
            if (ec) break;
            const DWORD attributes = GetFileAttributesW(e.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const bool isDir = e.is_directory(ec);
            std::string name = Utf8String(e.path().filename());
            if (name.empty() || name.front() == '.' || e.path().extension() == ".meta") continue;   // .meta·숨김 스킵.
            if (isDir != wantDirs) continue;
            out.push_back(std::move(name));
        }
        std::sort(out.begin(), out.end());
    }

    static std::string MakeVpath(const std::string& dir, const std::string& fname) {
        return dir.empty() ? ("assets://" + fname) : ("assets://" + dir + "/" + fname);
    }

    std::string m_projectRoot;
    std::string m_status;
    std::string m_dialogError;
    std::string m_deletePath;
    std::array<char, 128> m_folderName{};
    bool m_folderRequested = false;
    bool m_deleteRequested = false;
    std::string m_search;
    std::string m_currentFolder;   // assets 마운트 기준 상대 폴더
};

class AssetBrowserPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kAssetBrowserDesc; }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<AssetBrowserPanel>();
    }
};

} // namespace

// -----------------------------------------------------------------------------
// 공개 드롭 처리 — 씬 뷰포트가 "MYE_ASSET" 페이로드를 받았을 때 호출한다.
//   vpath 종류를 판정해 적절한 생성 커맨드를 활성 스택에 발행한다. worldPos 는 드롭 지점의
//   월드 좌표(뷰포트가 픽 좌표를 넘긴다). 발행된 커맨드는 Undo 가능(Ctrl+Z).
// -----------------------------------------------------------------------------
void InstantiateAssetToWorld(EditorContext& ctx, std::string_view vpath, Vec2 worldPos) {
    CommandStack* s = Stack(ctx);
    if (!s) return;

    const std::string ext = ExtOf(vpath);
    const std::string name = std::string(vpath);

    if (ext == "prefab") {
        const std::string os = VpathToOsPath(ctx, vpath);
        s->Push(std::make_unique<InstantiatePrefabCommand>(os, worldPos,
                                                           "Instantiate Prefab"));
        return;
    }
    if (ext == "lua") {
        s->Push(std::make_unique<CreateScriptEntityCommand>(name, worldPos,
                                                            "Create Script Entity"));
        return;
    }
    if (IsSpriteAssetExt(ext)) {
        // vpath → GUID(AssetDatabase 있으면). 없으면 빈 AssetRef(경로만 참조는 후속).
        asset::AssetRef ref;
        if (ctx.engine) {
            if (auto* db = ctx.engine->GetService<asset::AssetDatabase>()) {
                ref.guid = db->GuidFromPath(vpath);
                ref.type = refl::TypeIdFromName("mye::asset::Texture");
            }
        }
        s->Push(std::make_unique<CreateSpriteEntityCommand>(ref, worldPos,
                                                           "Create Sprite Entity"));
        return;
    }

    MYE_LOG_INFO("Editor", "드롭 처리 미지원 에셋: {}", name);
}

std::unique_ptr<IEditorPanelFactory> MakeAssetBrowserPanelFactory() {
    return std::make_unique<AssetBrowserPanelFactory>();
}

} // namespace mye::editor
