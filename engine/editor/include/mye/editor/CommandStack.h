// mye/editor/CommandStack.h — 문서별 Undo/Redo 스택 + 트랜잭션 (docs/07 §4)
//
// 07 §4: Undo 스택은 문서(열린 씬/애셋)별로 분리. Ctrl+Z는 포커스 문서 스택에 적용.
//   push는 execute 후 스택 적재. 드래그 병합(tryMerge)·멀티선택 트랜잭션·savePoint(dirty) 지원.
//   상한(기본 512개) 초과 시 오래된 것부터 파기.
#pragma once

#include "mye/editor/Command.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace mye::editor {

class CommandStack {
public:
    explicit CommandStack(EditorContext* ctx = nullptr, std::size_t limit = 512);
    ~CommandStack();
    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    // 컨텍스트 배선(문서 오픈 시). Execute/Undo가 이 컨텍스트를 커맨드에 전달.
    void SetContext(EditorContext* ctx) { m_ctx = ctx; }

    // 커맨드 실행 + 적재. redo 가지는 파기된다. 트랜잭션 중이면 트랜잭션에 합류.
    //   직전 커맨드와 tryMerge 성공 시 병합(연속 드래그 편집 1개로).
    void Push(CommandPtr cmd);

    // 트랜잭션(07 §4): 멀티선택·프리팹 적용 등 복합 편집을 Undo 1회로 묶는다. 중첩 카운트.
    void BeginTransaction(std::string_view label);
    void EndTransaction();

    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    std::string_view UndoLabel() const;   // 다음 Undo 대상 라벨(메뉴 표시). 없으면 빈 뷰.
    std::string_view RedoLabel() const;

    // Dirty 판정(07 §4): 마지막 저장 시점 위치와 현재 위치 비교. 탭 제목 '*' 근거.
    std::uint64_t Position() const;   // 현재 스택 커서 순번(단조 증가 아님 — 위치)
    void          MarkSaved();        // 현재 위치를 저장 지점으로 기록
    bool          IsDirty() const;    // 현재 위치 != 저장 지점

    void Clear();   // 문서 닫힘·플레이 Undo 스택 파기(07 §3) 시.

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    EditorContext*        m_ctx = nullptr;
};

} // namespace mye::editor
