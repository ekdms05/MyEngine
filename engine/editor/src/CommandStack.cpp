// CommandStack.cpp — 문서별 Undo/Redo 스택 + 트랜잭션 (docs/07 §4)
//
// 07 §4: push는 execute 후 스택 적재, redo 가지 파기, 연속 편집 병합(tryMerge), 멀티선택·프리팹
//   적용을 Undo 1회로 묶는 트랜잭션, 저장 지점 기준 dirty 판정, 상한(기본 512) 초과 시 오래된
//   것부터 파기. 트랜잭션은 모은 커맨드를 하나의 복합 커맨드(TransactionCommand)로 묶어 undoStack에
//   1개만 적재하고 Undo/Redo를 역/정순으로 일괄 수행한다.
#include "mye/editor/CommandStack.h"

#include <string>
#include <vector>

namespace mye::editor {

namespace {

// 여러 커맨드를 Undo 1회로 묶는 복합 커맨드. Execute=정순, Undo=역순.
//   각 하위 커맨드는 트랜잭션 수집 중 이미 Execute됨 → 최초 적재 시 재실행하지 않도록
//   CommandStack이 Push 대신 직접 적재한다(아래 EndTransaction 참조). Redo 시에는 Execute 호출.
class TransactionCommand final : public IEditorCommand {
public:
    TransactionCommand(std::string label, std::vector<CommandPtr> cmds)
        : m_label(std::move(label)), m_cmds(std::move(cmds)) {}

    void Execute(EditorContext& ctx) override {
        for (auto& c : m_cmds) c->Execute(ctx);        // Redo: 정순 재실행.
    }
    void Undo(EditorContext& ctx) override {
        for (auto it = m_cmds.rbegin(); it != m_cmds.rend(); ++it) (*it)->Undo(ctx);
    }
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::Transaction; }

private:
    std::string             m_label;
    std::vector<CommandPtr> m_cmds;
};

} // namespace

struct CommandStack::Impl {
    std::vector<CommandPtr> undoStack;
    std::vector<CommandPtr> redoStack;
    std::size_t limit = 512;

    // 트랜잭션(중첩 카운트). depth>0이면 push가 txnCmds에 모인다.
    int         txnDepth = 0;
    std::string txnLabel;
    std::vector<CommandPtr> txnCmds;

    // dirty 판정: 저장 지점 순번 vs 현재 순번. 병합은 논리 편집 1개 → position 불변.
    std::uint64_t position = 0;
    std::uint64_t savePoint = 0;
    bool          saveReachable = true;   // 저장 지점이 Undo로 도달 가능한가(트림되면 false).

    // 상한 초과 시 오래된 것부터 파기. Undo로 되돌아갈 수 있는 최소 position은
    //   (position - undoStack.size())까지다. 저장 지점이 트림 경계 아래로 밀려나면
    //   더 이상 Undo로 clean 상태에 닿을 수 없으므로 saveReachable=false로 표시해
    //   IsDirty가 clean에 갇히지 않도록(항상 dirty로) 만든다.
    void TrimToLimit() {
        while (undoStack.size() > limit) {
            undoStack.erase(undoStack.begin());
        }
        const std::uint64_t minReachable = position - undoStack.size();
        if (savePoint < minReachable) saveReachable = false;
    }
};

CommandStack::CommandStack(EditorContext* ctx, std::size_t limit)
    : m_impl(std::make_unique<Impl>()), m_ctx(ctx) {
    m_impl->limit = limit;
}
CommandStack::~CommandStack() = default;

void CommandStack::Push(CommandPtr cmd) {
    if (!cmd) return;

    if (m_ctx) cmd->Execute(*m_ctx);   // 07 §4: push 시 execute.

    if (m_impl->txnDepth > 0) {
        m_impl->txnCmds.push_back(std::move(cmd));
        return;
    }

    m_impl->redoStack.clear();   // 새 편집 → redo 가지 파기.

    // 병합 시도(연속 드래그 편집 1개로). 흡수되면 position 불변(하나의 논리 편집).
    if (!m_impl->undoStack.empty() && m_impl->undoStack.back()->TryMerge(*cmd)) {
        return;
    }

    m_impl->undoStack.push_back(std::move(cmd));
    ++m_impl->position;
    m_impl->TrimToLimit();
}

void CommandStack::BeginTransaction(std::string_view label) {
    if (m_impl->txnDepth == 0) {
        m_impl->txnLabel.assign(label);
        m_impl->txnCmds.clear();
    }
    ++m_impl->txnDepth;
}

void CommandStack::EndTransaction() {
    if (m_impl->txnDepth == 0) return;
    --m_impl->txnDepth;
    if (m_impl->txnDepth > 0) return;   // 중첩 — 아직 커밋 안 함.

    if (m_impl->txnCmds.empty()) return;

    m_impl->redoStack.clear();

    // 하위 커맨드가 하나뿐이면 굳이 래핑하지 않는다(Undo 라벨·병합 단순화).
    if (m_impl->txnCmds.size() == 1) {
        m_impl->undoStack.push_back(std::move(m_impl->txnCmds.front()));
    } else {
        // 모은 커맨드를 복합 커맨드로 묶어 Undo 1회로. 하위 커맨드는 이미 Execute됨 →
        //   여기서는 직접 적재만 하고 재실행하지 않는다(TransactionCommand::Execute는 Redo 전용).
        auto txn = std::make_unique<TransactionCommand>(m_impl->txnLabel,
                                                        std::move(m_impl->txnCmds));
        m_impl->undoStack.push_back(std::move(txn));
    }
    m_impl->txnCmds.clear();
    ++m_impl->position;
    m_impl->TrimToLimit();
}

void CommandStack::Undo() {
    if (m_impl->txnDepth > 0) return;   // 트랜잭션 진행 중 Undo 금지.
    if (m_impl->undoStack.empty()) return;
    CommandPtr cmd = std::move(m_impl->undoStack.back());
    m_impl->undoStack.pop_back();
    if (m_ctx) cmd->Undo(*m_ctx);
    m_impl->redoStack.push_back(std::move(cmd));
    --m_impl->position;
}

void CommandStack::Redo() {
    if (m_impl->txnDepth > 0) return;
    if (m_impl->redoStack.empty()) return;
    CommandPtr cmd = std::move(m_impl->redoStack.back());
    m_impl->redoStack.pop_back();
    if (m_ctx) cmd->Execute(*m_ctx);
    m_impl->undoStack.push_back(std::move(cmd));
    ++m_impl->position;
}

bool CommandStack::CanUndo() const { return !m_impl->undoStack.empty(); }
bool CommandStack::CanRedo() const { return !m_impl->redoStack.empty(); }

std::string_view CommandStack::UndoLabel() const {
    return m_impl->undoStack.empty() ? std::string_view{} : m_impl->undoStack.back()->Label();
}
std::string_view CommandStack::RedoLabel() const {
    return m_impl->redoStack.empty() ? std::string_view{} : m_impl->redoStack.back()->Label();
}

std::uint64_t CommandStack::Position() const { return m_impl->position; }
void CommandStack::MarkSaved() {
    m_impl->savePoint = m_impl->position;
    m_impl->saveReachable = true;   // 방금 저장한 지점은 당연히 현재=도달 가능.
}
bool CommandStack::IsDirty() const {
    // 저장 지점이 트림으로 유실됐으면 clean으로 돌아갈 방법이 없다 → 항상 dirty.
    if (!m_impl->saveReachable) return true;
    return m_impl->position != m_impl->savePoint;
}

void CommandStack::Clear() {
    m_impl->undoStack.clear();
    m_impl->redoStack.clear();
    m_impl->txnCmds.clear();
    m_impl->txnDepth = 0;
    m_impl->position = 0;
    m_impl->savePoint = 0;
    m_impl->saveReachable = true;
}

} // namespace mye::editor
