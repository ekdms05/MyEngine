// Selection.cpp — 전역 선택 상태 + 이력 (M4-A 골격: 선택/다중/이력 로직 구현)
//
// 07 §5의 다중 선택(Replace/Add/Toggle)·primary·이력(Alt+←/→)·변경 이벤트 발행을 실동작으로
// 구현한다. 이벤트 버스 발행은 01 EventBus를 소비한다.
#include "mye/editor/Selection.h"
#include "mye/core/Events.h"

#include <algorithm>
#include <vector>

namespace mye::editor {

struct SelectionManager::Impl {
    std::vector<SelectableRef> current;
    SelectableRef              primary{};

    // 선택 이력(간단 스택): back/forward 커서.
    std::vector<std::vector<SelectableRef>> history;
    std::size_t historyCursor = 0;
};

SelectionManager::SelectionManager(EventBus* events)
    : m_impl(std::make_unique<Impl>()), m_events(events) {}
SelectionManager::~SelectionManager() = default;

void SelectionManager::Select(std::span<const SelectableRef> refs, SelectMode mode) {
    auto& cur = m_impl->current;

    switch (mode) {
        case SelectMode::Replace:
            cur.assign(refs.begin(), refs.end());
            break;
        case SelectMode::Add:
            for (const auto& r : refs) {
                if (std::find(cur.begin(), cur.end(), r) == cur.end()) cur.push_back(r);
            }
            break;
        case SelectMode::Toggle:
            for (const auto& r : refs) {
                auto it = std::find(cur.begin(), cur.end(), r);
                if (it != cur.end()) cur.erase(it);
                else cur.push_back(r);
            }
            break;
    }

    m_impl->primary = cur.empty() ? SelectableRef{} : cur.back();

    // 이력 push(현재 커서 이후 forward 가지 파기).
    if (m_impl->historyCursor < m_impl->history.size())
        m_impl->history.resize(m_impl->historyCursor);
    m_impl->history.push_back(cur);
    m_impl->historyCursor = m_impl->history.size();

    if (m_events) {
        SelectionChangedEvent ev;
        ev.count = static_cast<std::uint32_t>(cur.size());
        ev.primary = m_impl->primary;
        m_events->Publish(ev);
    }
}

void SelectionManager::Select(SelectableRef ref, SelectMode mode) {
    Select(std::span<const SelectableRef>(&ref, 1), mode);
}

void SelectionManager::Clear() {
    Select(std::span<const SelectableRef>{}, SelectMode::Replace);
}

std::span<const SelectableRef> SelectionManager::Current() const { return m_impl->current; }
SelectableRef SelectionManager::Primary() const { return m_impl->primary; }

bool SelectionManager::IsSelected(SelectableRef ref) const {
    const auto& cur = m_impl->current;
    return std::find(cur.begin(), cur.end(), ref) != cur.end();
}
bool SelectionManager::Empty() const { return m_impl->current.empty(); }

void SelectionManager::NavigateBack() {
    if (!CanNavigateBack()) return;
    --m_impl->historyCursor;
    m_impl->current = m_impl->history[m_impl->historyCursor - 1];
    m_impl->primary = m_impl->current.empty() ? SelectableRef{} : m_impl->current.back();
    if (m_events) {
        SelectionChangedEvent ev{};
        ev.count = static_cast<std::uint32_t>(m_impl->current.size());
        ev.primary = m_impl->primary;
        m_events->Publish(ev);
    }
}
void SelectionManager::NavigateForward() {
    if (!CanNavigateForward()) return;
    ++m_impl->historyCursor;
    m_impl->current = m_impl->history[m_impl->historyCursor - 1];
    m_impl->primary = m_impl->current.empty() ? SelectableRef{} : m_impl->current.back();
    if (m_events) {
        SelectionChangedEvent ev{};
        ev.count = static_cast<std::uint32_t>(m_impl->current.size());
        ev.primary = m_impl->primary;
        m_events->Publish(ev);
    }
}
bool SelectionManager::CanNavigateBack() const { return m_impl->historyCursor > 1; }
bool SelectionManager::CanNavigateForward() const {
    return m_impl->historyCursor < m_impl->history.size();
}

} // namespace mye::editor
