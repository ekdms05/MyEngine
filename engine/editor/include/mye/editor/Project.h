// mye/editor/Project.h — 프로젝트 컨텍스트 + 열린 문서 (docs/07 §2, §6)
//
// 에셋 DB 루트는 <project>/assets/. 에디터 상태는 <project>/.myeditor/(layout.ini·session.json)에 저장, 씬 파일엔
//   저장 금지(.gitignore 대상 — 규약 확정).
//
// 문서(Document): 열린 씬/에셋 하나. 문서별 CommandStack·dirty를 소유. Ctrl+Z·저장은 포커스
//   문서에 적용(07 §4 스코프).
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/editor/CommandStack.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/core/InputActions.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

// 열린 씬·애니메이션. 문서별 데이터와 Undo 스택 소유.
class Document {
public:
    enum class Kind : std::uint8_t { Scene, Asset };

    Document(DocumentId id, Kind kind, std::string path);
    ~Document();

    DocumentId       Id() const { return m_id; }
    Kind             GetKind() const { return m_kind; }
    std::string_view Path() const { return m_path; }         // 저장 경로(빈="미저장 새 씬")
    void             SetPath(std::string p) { m_path = std::move(p); }

    CommandStack&    Commands() { return m_commands; }
    bool             IsDirty() const { return m_path.empty() || m_commands.IsDirty(); }
    std::string      TabTitle() const;                        // 파일명 + dirty '*'
    ecs::World&      World() { return *m_world; }
    const ecs::World& World() const { return *m_world; }
    asset::AnimationAsset& Animation() { return m_animation; }
    const asset::AnimationAsset& Animation() const { return m_animation; }

private:
    asset::AnimationAsset m_animation;
    DocumentId   m_id;
    Kind         m_kind;
    std::string  m_path;
    CommandStack m_commands;
    std::unique_ptr<EventBus> m_worldEvents;
    std::unique_ptr<ecs::World> m_world;
};

// 프로젝트 컨텍스트 — 경로·에디터 상태 디렉터리·열린 문서 목록.
class ProjectContext {
public:
    ProjectContext();
    ~ProjectContext();
    ProjectContext(const ProjectContext&) = delete;
    ProjectContext& operator=(const ProjectContext&) = delete;

    // Validate/load a candidate before replacing the current project. Discard is explicit.
    Expected<void, Error> Open(std::string_view projectPath, bool discardUnsaved = false);
    Expected<void, Error> Create(std::string_view name, std::string_view directory,
                                 bool discardUnsaved = false, std::string_view templateDirectory = {});
    Expected<void, Error> Save(); // All named documents, then project metadata.
    bool IsOpen() const;
    bool HasUnsavedChanges() const;
    std::string_view Name() const;
    std::string_view ProjectFilePath() const;
    const InputMap& InputSettings() const;
    // Save settings without saving/replacing scene documents. Failure keeps both
    // the manifest and the running editor settings unchanged.
    Expected<void, Error> SaveInputSettings(const InputMap& settings);

    std::string_view RootDir() const;       // 프로젝트 루트
    std::string      EditorStateDir() const; // <root>/.myeditor
    std::string      LayoutIniPath() const;  // <root>/.myeditor/layout.ini
    std::string      SessionJsonPath() const;// <root>/.myeditor/session.json

    // ---- 문서 관리 ----
    Document* NewScene();                              // 빈 새 씬 문서(미저장)
    Expected<Document*, Error> OpenScene(std::string_view path); // Failure preserves all documents.
    Expected<void, Error> SaveScene(DocumentId id, std::string_view path);
    Document* NewAnimation();
    Expected<Document*, Error> OpenAnimation(std::string_view path);
    Expected<void, Error> SaveAnimation(DocumentId id, std::string_view path);
    void      CloseDocument(DocumentId id);
    Document* Active() const;                           // 포커스 문서(null 가능)
    void      SetActive(DocumentId id);
    std::span<Document* const> Documents() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::editor
