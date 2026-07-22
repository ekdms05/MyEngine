// mye/ui/Widget.h — 리테인드 위젯 트리 베이스 (docs/06 §1 위젯 트리)
//
// 소유: 06 (M5-A). UiRoot → UiCanvas → Widget 트리. measure/arrange 2패스 레이아웃, 라우티드
//   onEvent, draw(UiDrawContext). dynamic_cast 금지 규약 → 타입 식별은 WidgetTypeId(가상 함수).
//   부모/자식 소유: 부모가 자식을 unique_ptr 로 소유(트리 = 소유 구조). parent 는 raw back-ptr.
#pragma once

#include "mye/ui/UiTypes.h"
#include "mye/ui/UiEvent.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::ui {

class UiDrawContext;
class Widget;
using WidgetPtr = std::unique_ptr<Widget>;

// dynamic_cast 금지 → 위젯 타입 식별자(FNV 해시). 파생 타입이 kWidgetType 을 노출.
using WidgetTypeId = uint64_t;
#define MYE_WIDGET(TypeName)                                                    \
    static constexpr ::mye::ui::WidgetTypeId kWidgetType =                      \
        ::mye::HashFnv1a64(#TypeName);                                          \
    ::mye::ui::WidgetTypeId typeId() const override { return kWidgetType; }

// AnchorRect + 부모 사각형 → 자식 사각형(스크린 픽셀, 정수 스냅). 레이아웃·테스트 공용.
//   점 앵커(anchorMin==anchorMax): sizeDelta 크기 + offsetMin 위치(pivot 정렬).
//   스트레치: 앵커 밴드에서 offsetMin(좌·상)·offsetMax(우·하) 픽셀 마진.
UiRect ComputeAnchoredRect(const AnchorRect& anchors, const UiRect& parent);

class Widget {
public:
    Widget() = default;
    virtual ~Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- 타입 식별(dynamic_cast 대체) ---
    virtual WidgetTypeId typeId() const = 0;
    template <typename T> T* As() {
        return typeId() == T::kWidgetType ? static_cast<T*>(this) : nullptr;
    }
    template <typename T> const T* As() const {
        return typeId() == T::kWidgetType ? static_cast<const T*>(this) : nullptr;
    }

    // --- 트리 ---
    Widget* parent() const { return m_parent; }
    std::span<const WidgetPtr> children() const { return m_children; }
    Widget* addChild(WidgetPtr child);                 // 소유 이전, 반환 = raw 편의 포인터
    WidgetPtr removeChild(Widget* child);              // 소유 반환(분리)
    Widget* findByName(std::string_view name);         // 서브트리 이름 검색(UiDocument::find)

    // --- 식별·레이아웃 상태 ---
    std::string  name;                                  // UiDocument 조회 키
    AnchorRect   anchors{};
    StyleClassId styleClass{};
    Visibility   visibility = Visibility::Visible;
    UiRect       computedRect{};                        // arrange 결과(스크린 픽셀)

    // --- 레이아웃 2패스 ---
    // measure: 원하는 크기 계산(avail = 부모가 준 가용 공간). 컨테이너는 자식 measure 취합.
    virtual Vec2 measure(Vec2 avail);
    // arrange: 최종 사각형 확정 + 자식 배치. computedRect 세팅.
    virtual void arrange(const UiRect& finalRect);

    // --- 이벤트(라우티드) ---
    // handled=true 반환 시 전파 중단. phase 로 tunnel/target/bubble 구분.
    virtual bool onEvent(UiEvent& e) { (void)e; return false; }

    // --- 드로우 ---
    virtual void draw(UiDrawContext& ctx) { (void)ctx; }   // 배처에 커맨드 제출(기본: 자식만)

    // --- 상호작용 질의 ---
    virtual bool hitTest(Vec2 uiPos) const;             // 기본: computedRect 포함 검사
    bool         interactive = true;                    // false 면 히트 테스트 통과(아래로 전달)

protected:
    // 자식들을 draw/measure/arrange 순회하는 기본 헬퍼(파생이 재사용).
    void drawChildren(UiDrawContext& ctx);
    void arrangeChildrenByAnchors();                    // 앵커 기반 자식 배치(computedRect 기준)

    Widget*                m_parent = nullptr;
    std::vector<WidgetPtr> m_children;
};

} // namespace mye::ui
