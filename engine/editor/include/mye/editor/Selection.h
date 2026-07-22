// mye/editor/Selection.h — 전역 선택 상태 + 변경 이벤트 (docs/07 §5)
//
// 07 §5: SelectionManager가 전역 단일 진실 공급원. 다중 선택(Ctrl 토글·Shift 범위), 주 선택
//   (primary) 유지. 선택 변경은 01 이벤트 버스로 브로드캐스트 → 하이어라키·인스펙터·뷰포트가
//   구독(패널 간 직접 참조 없음). 선택 이력(Alt+←/→) 유지.
#pragma once

#include "mye/editor/EditorTypes.h"

#include <memory>
#include <span>
#include <vector>

namespace mye::editor {

// 선택 변경 브로드캐스트(01 이벤트 버스). 하이어라키·인스펙터·뷰포트 아웃라인이 구독.
struct SelectionChangedEvent {
    MYE_EVENT(SelectionChangedEvent);
    // 페이로드는 경량(개수만) — 구독자는 SelectionManager::Current()로 조회한다.
    std::uint32_t count = 0;
    SelectableRef primary{};
};

class SelectionManager {
public:
    explicit SelectionManager(EventBus* events = nullptr);
    ~SelectionManager();
    SelectionManager(const SelectionManager&) = delete;
    SelectionManager& operator=(const SelectionManager&) = delete;

    void SetEventBus(EventBus* events) { m_events = events; }

    // 선택 설정. Replace/Add/Toggle. 변경이 있으면 SelectionChangedEvent 발행.
    void Select(std::span<const SelectableRef> refs, SelectMode mode = SelectMode::Replace);
    void Select(SelectableRef ref, SelectMode mode = SelectMode::Replace);
    void Clear();

    // 현재 선택(선택 순서 유지). primary=마지막에 추가/설정된 주 선택.
    std::span<const SelectableRef> Current() const;
    SelectableRef Primary() const;
    bool IsSelected(SelectableRef ref) const;
    bool Empty() const;

    // 선택 이력(07 §5, Alt+←/→). Select가 자동으로 이력을 push한다.
    void NavigateBack();
    void NavigateForward();
    bool CanNavigateBack() const;
    bool CanNavigateForward() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    EventBus*             m_events = nullptr;
};

} // namespace mye::editor
