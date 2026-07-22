// mye/editor/Project.h — 프로젝트 컨텍스트 + 열린 문서 (docs/07 §2, §6)
//
// 07 §2: 런처 없이 --project 인자로 프로젝트를 연다(P0 유일 진입). 프로젝트 루트 = 04 에셋 DB
//   루트. 07 §6: 에디터 상태는 <project>/.myeditor/(layout.ini·session.json)에 저장, 씬 파일엔
//   저장 금지(.gitignore 대상 — 규약 확정).
//
// 문서(Document): 열린 씬/에셋 하나. 문서별 CommandStack·dirty를 소유. Ctrl+Z·저장은 포커스
//   문서에 적용(07 §4 스코프).
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/editor/CommandStack.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

// 열린 문서 하나(씬/애셋). v1은 씬 문서. 문서별 Undo 스택 소유.
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
    bool             IsDirty() const { return m_commands.IsDirty(); }
    std::string      TabTitle() const;                        // 파일명 + dirty '*'

private:
    DocumentId   m_id;
    Kind         m_kind;
    std::string  m_path;
    CommandStack m_commands;
};

// 프로젝트 컨텍스트 — 경로·에디터 상태 디렉터리·열린 문서 목록.
class ProjectContext {
public:
    ProjectContext();
    ~ProjectContext();
    ProjectContext(const ProjectContext&) = delete;
    ProjectContext& operator=(const ProjectContext&) = delete;

    // --project 경로로 오픈(.myeproj 또는 프로젝트 디렉터리). 실패 시 Error(런처 복귀 근거).
    Expected<void, Error> Open(std::string_view projectPath);
    bool IsOpen() const;

    std::string_view RootDir() const;       // 프로젝트 루트(에셋 DB 루트)
    std::string      EditorStateDir() const; // <root>/.myeditor
    std::string      LayoutIniPath() const;  // <root>/.myeditor/layout.ini
    std::string      SessionJsonPath() const;// <root>/.myeditor/session.json

    // ---- 문서 관리 ----
    Document* NewScene();                              // 빈 새 씬 문서(미저장)
    Document* OpenScene(std::string_view path);        // 씬 파일 열기(중복이면 기존 반환)
    void      CloseDocument(DocumentId id);
    Document* Active() const;                           // 포커스 문서(null 가능)
    void      SetActive(DocumentId id);
    std::span<Document* const> Documents() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::editor
