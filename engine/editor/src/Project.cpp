// Project.cpp — 프로젝트 컨텍스트·문서 골격 (M4-A: 문서 관리·경로 구현)
//
// 07 §2·§6: --project 오픈, .myeditor 상태 경로, 문서(씬)별 CommandStack·dirty. 골격은 경로
//   유도·문서 목록·포커스 관리를 구현하고, 실제 씬 로드(SceneSerializer)와 .myeproj 파싱은
//   M4-B가 채운다.
#include "mye/editor/Project.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace mye::editor {

// ---- Document ----
Document::Document(DocumentId id, Kind kind, std::string path)
    : m_id(id), m_kind(kind), m_path(std::move(path)) {}
Document::~Document() = default;

std::string Document::TabTitle() const {
    std::string name = m_path.empty()
        ? std::string("untitled")
        : std::filesystem::path(m_path).filename().string();
    if (IsDirty()) name += "*";
    return name;
}

// ---- ProjectContext ----
struct ProjectContext::Impl {
    std::string rootDir;   // 프로젝트 루트(에셋 DB 루트)
    bool        open = false;

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

Expected<void, Error> ProjectContext::Open(std::string_view projectPath) {
    // .myeproj 파일이면 부모 디렉터리를, 디렉터리면 그대로 루트로 삼는다.
    std::filesystem::path p(projectPath);
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) p = p.parent_path();
    m_impl->rootDir = p.string();
    m_impl->open = true;
    // TODO(M4-B): .myeproj 파싱(엔진 버전 검사), 04 에셋 DB 루트 배선, .myeditor 생성.
    return {};
}

bool ProjectContext::IsOpen() const { return m_impl->open; }
std::string_view ProjectContext::RootDir() const { return m_impl->rootDir; }

std::string ProjectContext::EditorStateDir() const {
    return (std::filesystem::path(m_impl->rootDir) / ".myeditor").string();
}
std::string ProjectContext::LayoutIniPath() const {
    return (std::filesystem::path(EditorStateDir()) / "layout.ini").string();
}
std::string ProjectContext::SessionJsonPath() const {
    return (std::filesystem::path(EditorStateDir()) / "session.json").string();
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

Document* ProjectContext::OpenScene(std::string_view path) {
    for (auto& d : m_impl->documents)
        if (d->Path() == path) { m_impl->active = d->Id(); return d.get(); }

    auto doc = std::make_unique<Document>(DocumentId{m_impl->nextDocId++},
                                          Document::Kind::Scene, std::string(path));
    Document* raw = doc.get();
    m_impl->documents.push_back(std::move(doc));
    m_impl->RefreshPtrs();
    m_impl->active = raw->Id();
    // TODO(M4-B): SceneSerializer::LoadFromFile로 씬 내용 로드.
    return raw;
}

void ProjectContext::CloseDocument(DocumentId id) {
    auto& v = m_impl->documents;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const std::unique_ptr<Document>& d) { return d->Id() == id; }),
            v.end());
    m_impl->RefreshPtrs();
    if (m_impl->active == id) {
        m_impl->active = v.empty() ? DocumentId{} : v.back()->Id();
    }
}

Document* ProjectContext::Active() const { return m_impl->Find(m_impl->active); }
void ProjectContext::SetActive(DocumentId id) {
    if (m_impl->Find(id)) m_impl->active = id;
}
std::span<Document* const> ProjectContext::Documents() const { return m_impl->documentPtrs; }

} // namespace mye::editor
