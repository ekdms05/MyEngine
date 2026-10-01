// Project.cpp — project metadata, isolated scene documents and checked file I/O.
#include "mye/editor/Project.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/core/Events.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/gameplay/Progression.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/phys/Collision.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace mye::editor {
namespace {
namespace fs = std::filesystem;

Expected<fs::path, Error> ScenePath(const fs::path& root, std::string_view text, std::string_view extension = ".scene") {
    auto path = Utf8Path(text);
    if (path.empty() || text.find('\0') != std::string_view::npos || path.extension() != extension)
        return Error{"Select a " + std::string(extension) + " file", 1};
    std::error_code ec;
    path = fs::weakly_canonical(path.is_absolute() ? path : root / path, ec);
    if (ec) return Error{"Scene path is unavailable: " + ec.message(), ec.value()};
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        return Error{"Scene files must be inside the current project", 1};
    if (extension == ".anim") {
        const auto assetRoot = fs::weakly_canonical(root / "assets", ec);
        if (ec) return Error{"Asset folder is unavailable: " + ec.message(), ec.value()};
        const auto assetRelative = path.lexically_relative(assetRoot);
        if (assetRelative.empty() || assetRelative.is_absolute() || *assetRelative.begin() == "..")
            return Error{"Animation files must be inside the project assets folder", 1};
    }
    return path;
}

json::Value ProjectMetadata(std::string_view name, std::string_view mainScene) {
    return json::Value(json::Value::Object{
        {"version", json::Value(int64_t{1})},
        {"name", json::Value(std::string(name))},
        {"mainScene", json::Value(std::string(mainScene))}});
}

bool SameFile(std::string_view path, const fs::path& target) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::equivalent(Utf8Path(path), target, ec);
}
}

// ---- Document ----
Document::Document(DocumentId id, Kind kind, std::string path)
    : m_id(id), m_kind(kind), m_path(std::move(path)),
      m_worldEvents(std::make_unique<EventBus>()), m_world(std::make_unique<ecs::World>()) {
    scene::RegisterCoreComponentReflection();
    gameplay::RegisterProgressionReflection();
    runtime::RegisterObjectComponents(*m_world);
    m_world->RegisterComponent<scene::ObjectName>("ObjectName");
    m_world->RegisterComponent<phys::Collider2D>("Collider2D");
    m_world->RegisterComponent<phys::KinematicBody2D>("KinematicBody2D");
    m_world->SetEventBus(m_worldEvents.get());
    m_world->RegisterComponent<scene::LocalTransform>("LocalTransform");
    m_world->RegisterComponent<scene::WorldTransform>("WorldTransform");
    m_world->RegisterComponent<scene::Parent>("Parent");
    m_world->RegisterComponent<scene::Children>("Children");
    m_world->RegisterComponent<scene::SpriteRenderer>("SpriteRenderer");
    m_world->RegisterComponent<scene::FloorLevel>("FloorLevel");
    m_world->RegisterComponent<anim::SpriteAnimator>("SpriteAnimator");
    m_world->RegisterComponent<gameplay::Progression>("Progression");
}
Document::~Document() = default;

std::string Document::TabTitle() const {
    std::string name = m_path.empty()
        ? "untitled-" + std::to_string(m_id.value)
        : Utf8String(Utf8Path(m_path).filename());
    if (IsDirty()) name += "*";
    return name;
}

// ---- ProjectContext ----
struct ProjectContext::Impl {
    std::string rootDir;   // 프로젝트 루트(에셋 DB 루트)
    std::string name;
    std::string projectFile;
    std::string mainScene;
    json::Value metadata;
    bool        open = false;
    bool        metadataDirty = false;

    std::vector<std::unique_ptr<Document>> documents;
    std::vector<Document*> documentPtrs;   // Documents() 반환 캐시
    DocumentId  active{};
    std::uint64_t nextDocId = 1;

    void RefreshPtrs() {
        documentPtrs.clear();
        for (auto& d : documents) documentPtrs.push_back(d.get());
    }
    Document* Find(DocumentId id) {
        for (auto& d : documents) if (d->Id() == id) return d.get();
        return nullptr;
    }
};

ProjectContext::ProjectContext() : m_impl(std::make_unique<Impl>()) {}
ProjectContext::~ProjectContext() = default;

Expected<void, Error> ProjectContext::Open(std::string_view projectPath, bool discardUnsaved) {
    if (!discardUnsaved && HasUnsavedChanges())
        return Error{"Save or explicitly discard the current scenes before changing projects", 1};
    if (projectPath.empty() || projectPath.find('\0') != std::string_view::npos)
        return Error{"Select a project file or folder", 1};
    std::error_code ec;
    auto path = fs::canonical(Utf8Path(projectPath), ec);
    if (ec) return Error{"Project path is unavailable: " + ec.message(), ec.value()};
    fs::path root, projectFile;
    if (fs::is_directory(path, ec)) {
        root = path;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".myeproj") continue;
            if (!projectFile.empty()) return Error{"Multiple project files: select one .myeproj file", 1};
            projectFile = it->path();
        }
        if (ec) return Error{"Cannot list project folder: " + ec.message(), ec.value()};
    } else if (fs::is_regular_file(path, ec) && path.extension() == ".myeproj") {
        projectFile = path;
        root = path.parent_path();
    } else return Error{"Select a project folder or .myeproj file", 1};

    auto candidate = std::make_unique<Impl>();
    candidate->rootDir = Utf8String(root);
    candidate->name = Utf8String(root.filename());
    if (projectFile.empty()) {
        // Existing sample projects predate project metadata; opening never rewrites them.
        if (!fs::is_directory(root / "assets", ec))
            return Error{"No .myeproj file or legacy assets folder found", 1};
        projectFile = root / "project.myeproj";
        candidate->metadata = ProjectMetadata(candidate->name, "");
    } else {
        auto metadata = ReadJsonFile(projectFile);
        if (!metadata) return metadata.GetError();
        const auto* version = metadata.Value().Find("version");
        const auto* name = metadata.Value().Find("name");
        const auto* main = metadata.Value().Find("mainScene");
        if (!metadata.Value().IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
            !name || !name->IsString() || name->AsString().empty() || !main || !main->IsString())
            return Error{"Invalid or unsupported project metadata (version 1 required)", 1};
        candidate->name = name->AsString();
        candidate->mainScene = main->AsString();
        candidate->metadata = std::move(metadata).Value();
    }
    candidate->projectFile = Utf8String(projectFile);
    if (!candidate->mainScene.empty()) {
        const auto main = Utf8Path(candidate->mainScene);
        if (main.is_absolute()) return Error{"Project mainScene must be a relative path", 1};
        auto resolved = ScenePath(root, candidate->mainScene);
        if (!resolved) return resolved.GetError();
        auto doc = std::make_unique<Document>(DocumentId{candidate->nextDocId++}, Document::Kind::Scene,
                                              Utf8String(resolved.Value()));
        auto loaded = SceneSerializer{}.LoadFromFile(doc->World(), doc->Path());
        if (!loaded) return loaded.GetError();
        auto valid = runtime::ValidateObjectComponents(doc->World());
        if (!valid) return valid.GetError();
        candidate->active = doc->Id();
        candidate->documents.push_back(std::move(doc));
    }
    candidate->open = true;
    candidate->RefreshPtrs();
    m_impl = std::move(candidate);
    return {};
}

Expected<void, Error> ProjectContext::Create(std::string_view name, std::string_view directory,
                                            bool discardUnsaved, std::string_view templateDirectory) {
    if (!discardUnsaved && HasUnsavedChanges())
        return Error{"Save or explicitly discard the current scenes before changing projects", 1};
    if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string_view::npos || directory.empty() ||
        name.find('\0') != std::string_view::npos || directory.find('\0') != std::string_view::npos)
        return Error{"Project name and folder are required", 1};
    std::error_code ec;
    auto root = fs::weakly_canonical(Utf8Path(directory), ec);
    if (ec) return Error{"Invalid project folder: " + ec.message(), ec.value()};
    const bool existed = fs::exists(root, ec);
    if (ec) return Error{"Cannot inspect project folder: " + ec.message(), ec.value()};
    if (existed && (!fs::is_directory(root, ec) || !fs::is_empty(root, ec) || ec))
        return Error{"Choose a new or empty folder; existing files will not be overwritten", 1};
    if (!templateDirectory.empty()) {
        const auto source = fs::canonical(Utf8Path(templateDirectory), ec);
        if (ec) return Error{"Starter template is unavailable: " + ec.message(), 1};
        ProjectContext checked;
        auto valid = checked.Open(Utf8String(source));
        if (!valid) return valid.GetError();
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_symlink(ec)) return Error{"Starter templates must not contain links", 1};
            if (it->is_directory(ec) && it->path().filename() == ".myeditor") { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file(ec)) files.push_back(it->path());
        }
        if (ec) return Error{"Cannot read starter template: " + ec.message(), ec.value()};
        for (const auto& file : files) {
            const auto target = root / file.lexically_relative(source);
            fs::create_directories(target.parent_path(), ec);
            if (ec) return Error{"Cannot create starter folder: " + ec.message(), ec.value()};
            fs::copy_file(file, target, fs::copy_options::none, ec);
            if (ec) return Error{"Cannot copy starter asset: " + ec.message(), ec.value()};
        }
        const auto manifest = root / Utf8Path(checked.ProjectFilePath()).filename();
        auto metadata = ReadJsonFile(manifest);
        if (!metadata) return metadata.GetError();
        auto renamed = metadata.Value().AsObject();
        renamed["name"] = json::Value(std::string(name));
        auto saved = WriteJsonFile(manifest, json::Value(std::move(renamed)));
        if (!saved) return saved.GetError();
        return Open(Utf8String(manifest), discardUnsaved);
    }
    fs::create_directories(root / "assets" / "scenes", ec);
    if (ec) return Error{"Cannot create project folder: " + ec.message(), ec.value()};
    const auto main = root / "assets" / "scenes" / "main.scene";
    Document initial(DocumentId{1}, Document::Kind::Scene, Utf8String(main));
    auto saved = SceneSerializer{}.SaveToFile(initial.World(), initial.Path());
    if (!saved) return saved.GetError();
    const auto projectFile = root / "project.myeproj";
    saved = WriteJsonFile(projectFile, ProjectMetadata(name, "assets/scenes/main.scene"));
    if (!saved) {
        // Only remove files/directories created in this previously empty folder.
        fs::remove(main, ec);
        fs::remove(root / "assets" / "scenes", ec);
        fs::remove(root / "assets", ec);
        if (!existed) fs::remove(root, ec);
        return saved.GetError();
    }
    return Open(Utf8String(projectFile), discardUnsaved);
}

Expected<void, Error> ProjectContext::Save() {
    if (!IsOpen()) return Error{"Open a project first", 1};
    for (const auto& doc : m_impl->documents)
        if (doc->Path().empty()) return Error{"Choose a file name for each unsaved scene first", 1};
    for (const auto& doc : m_impl->documents) {
        auto saved = doc->GetKind() == Document::Kind::Scene
            ? SaveScene(doc->Id(), doc->Path()) : SaveAnimation(doc->Id(), doc->Path());
        if (!saved) return saved.GetError();
    }
    auto metadata = m_impl->metadata.AsObject();
    metadata["name"] = json::Value(m_impl->name);
    metadata["mainScene"] = json::Value(m_impl->mainScene);
    auto saved = WriteJsonFile(Utf8Path(m_impl->projectFile), json::Value(std::move(metadata)));
    if (!saved) return saved.GetError();
    m_impl->metadataDirty = false;
    return {};
}

bool ProjectContext::IsOpen() const { return m_impl->open; }
bool ProjectContext::HasUnsavedChanges() const {
    return m_impl->metadataDirty || std::any_of(m_impl->documents.begin(), m_impl->documents.end(),
                       [](const auto& doc) { return doc->IsDirty(); });
}
std::string_view ProjectContext::Name() const { return m_impl->name; }
std::string_view ProjectContext::ProjectFilePath() const { return m_impl->projectFile; }
std::string_view ProjectContext::RootDir() const { return m_impl->rootDir; }

std::string ProjectContext::EditorStateDir() const {
    return Utf8String(Utf8Path(m_impl->rootDir) / ".myeditor");
}
std::string ProjectContext::LayoutIniPath() const {
    return Utf8String(Utf8Path(EditorStateDir()) / "layout.ini");
}
std::string ProjectContext::SessionJsonPath() const {
    return Utf8String(Utf8Path(EditorStateDir()) / "session.json");
}

Document* ProjectContext::NewScene() {
    auto doc = std::make_unique<Document>(DocumentId{m_impl->nextDocId++},
                                          Document::Kind::Scene, std::string{});
    Document* raw = doc.get();
    m_impl->documents.push_back(std::move(doc));
    m_impl->RefreshPtrs();
    m_impl->active = raw->Id();
    return raw;
}

Expected<Document*, Error> ProjectContext::OpenScene(std::string_view path) {
    if (!IsOpen()) return Error{"Open a project first", 1};
    auto resolved = ScenePath(Utf8Path(m_impl->rootDir), path);
    if (!resolved) return resolved.GetError();
    const auto canonicalPath = Utf8String(resolved.Value());
    for (auto& d : m_impl->documents)
        if (SameFile(d->Path(), resolved.Value())) { m_impl->active = d->Id(); return d.get(); }

    auto doc = std::make_unique<Document>(DocumentId{m_impl->nextDocId++},
                                          Document::Kind::Scene, canonicalPath);
    auto loaded = SceneSerializer{}.LoadFromFile(doc->World(), doc->Path());
    if (!loaded) return loaded.GetError();
    auto valid = runtime::ValidateObjectComponents(doc->World());
    if (!valid) return valid.GetError();
    Document* raw = doc.get();
    m_impl->documents.push_back(std::move(doc));
    m_impl->RefreshPtrs();
    m_impl->active = raw->Id();
    return raw;
}

Expected<void, Error> ProjectContext::SaveScene(DocumentId id, std::string_view path) {
    if (!IsOpen()) return Error{"Open a project first", 1};
    Document* doc = m_impl->Find(id);
    if (!doc || doc->GetKind() != Document::Kind::Scene) return Error{"Scene document no longer exists", 1};
    auto resolved = ScenePath(Utf8Path(m_impl->rootDir), path);
    if (!resolved) return resolved.GetError();
    const auto target = Utf8String(resolved.Value());
    for (const auto& other : m_impl->documents)
        if (other->Id() != id && SameFile(other->Path(), resolved.Value()))
            return Error{"That file is already open in another scene document", 1};
    auto valid = runtime::ValidateObjectComponents(doc->World());
    if (!valid) return valid.GetError();
    std::error_code ec;
    fs::create_directories(resolved.Value().parent_path(), ec);
    if (ec) return Error{"Cannot create scene folder: " + ec.message(), ec.value()};
    auto saved = SceneSerializer{}.SaveToFile(doc->World(), target);
    if (!saved) return saved.GetError();
    const auto oldRelative = Utf8String(Utf8Path(doc->Path()).lexically_relative(Utf8Path(m_impl->rootDir)));
    if (m_impl->mainScene.empty() || oldRelative == m_impl->mainScene) {
        const auto relative = Utf8String(resolved.Value().lexically_relative(Utf8Path(m_impl->rootDir)));
        m_impl->metadataDirty |= relative != m_impl->mainScene;
        m_impl->mainScene = relative;
    }
    doc->SetPath(target);
    doc->Commands().MarkSaved();
    return {};
}

Document* ProjectContext::NewAnimation() {
    auto doc = std::make_unique<Document>(DocumentId{m_impl->nextDocId++}, Document::Kind::Asset, std::string{});
    doc->Animation().clip.name = "new_animation";
    auto* raw = doc.get();
    m_impl->documents.push_back(std::move(doc));
    m_impl->RefreshPtrs();
    return raw; // Scene focus is independent of the animation panel.
}

Expected<Document*, Error> ProjectContext::OpenAnimation(std::string_view path) {
    if (!IsOpen()) return Error{"Open a project first", 1};
    auto resolved = ScenePath(Utf8Path(m_impl->rootDir), path, ".anim");
    if (!resolved) return resolved.GetError();
    for (auto& doc : m_impl->documents)
        if (SameFile(doc->Path(), resolved.Value())) return doc.get();
    auto value = ReadJsonFile(resolved.Value());
    if (!value) return value.GetError();
    auto animation = asset::AnimationAsset::FromJson(value.Value());
    if (!animation) return animation.GetError();
    auto doc = std::make_unique<Document>(DocumentId{m_impl->nextDocId++}, Document::Kind::Asset,
                                          Utf8String(resolved.Value()));
    doc->Animation() = std::move(animation).Value();
    auto* raw = doc.get();
    m_impl->documents.push_back(std::move(doc));
    m_impl->RefreshPtrs();
    return raw;
}

Expected<void, Error> ProjectContext::SaveAnimation(DocumentId id, std::string_view path) {
    if (!IsOpen()) return Error{"Open a project first", 1};
    auto* doc = m_impl->Find(id);
    if (!doc || doc->GetKind() != Document::Kind::Asset) return Error{"No animation document", 1};
    auto valid = doc->Animation().Validate();
    if (!valid) return valid.GetError();
    auto resolved = ScenePath(Utf8Path(m_impl->rootDir), path, ".anim");
    if (!resolved) return resolved.GetError();
    for (const auto& other : m_impl->documents)
        if (other->Id() != id && SameFile(other->Path(), resolved.Value()))
            return Error{"That animation file is already open", 1};
    std::error_code ec;
    fs::create_directories(resolved.Value().parent_path(), ec);
    if (ec) return Error{"Cannot create animation folder: " + ec.message(), ec.value()};
    auto saved = WriteJsonFile(resolved.Value(), doc->Animation().ToJson());
    if (!saved) return saved.GetError();
    doc->SetPath(Utf8String(resolved.Value()));
    doc->Commands().MarkSaved();
    return {};
}

void ProjectContext::CloseDocument(DocumentId id) {
    auto& v = m_impl->documents;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const std::unique_ptr<Document>& d) { return d->Id() == id; }),
            v.end());
    m_impl->RefreshPtrs();
    if (m_impl->active == id) {
        m_impl->active = {};
        for (const auto& doc : v) if (doc->GetKind() == Document::Kind::Scene) m_impl->active = doc->Id();
    }
}

Document* ProjectContext::Active() const { return m_impl->Find(m_impl->active); }
void ProjectContext::SetActive(DocumentId id) {
    if (auto* doc = m_impl->Find(id); doc && doc->GetKind() == Document::Kind::Scene) m_impl->active = id;
}
std::span<Document* const> ProjectContext::Documents() const { return m_impl->documentPtrs; }

} // namespace mye::editor
