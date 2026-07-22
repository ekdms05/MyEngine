// mye/ui/Widget.cpp — 위젯 베이스 (앵커 RectTransform 레이아웃 + 히트테스트)
//
// 앵커 규약(UiTypes.h AnchorRect): anchorMin/Max 는 부모 사각형 대비 정규화(0..1), UI +Y 아래.
//   - 점 앵커(anchorMin==anchorMax): offsetMin = pivot 기준 위치, sizeDelta = 크기.
//   - 스트레치(anchorMin!=anchorMax): 앵커 사각형에서 offsetMin(좌·상)·offsetMax(우·하) 픽셀 마진.
//   결과 computedRect 는 정수 픽셀로 스냅한다(도트 정합).
#include "mye/ui/Widget.h"
#include "mye/ui/UiDrawContext.h"

#include <cmath>

namespace mye::ui {

Widget* Widget::addChild(WidgetPtr child) {
    if (!child) return nullptr;
    child->m_parent = this;
    Widget* raw = child.get();
    m_children.push_back(std::move(child));
    return raw;
}

WidgetPtr Widget::removeChild(Widget* child) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (it->get() == child) {
            WidgetPtr owned = std::move(*it);
            m_children.erase(it);
            owned->m_parent = nullptr;
            return owned;
        }
    }
    return nullptr;
}

Widget* Widget::findByName(std::string_view n) {
    if (name == n) return this;
    for (auto& c : m_children)
        if (Widget* found = c->findByName(n)) return found;
    return nullptr;
}

Vec2 Widget::measure(Vec2 avail) {
    // 기본: 자식 measure 호출(컨테이너가 아닌 위젯은 앵커 크기 그대로).
    for (auto& c : m_children)
        if (c->visibility != Visibility::Collapsed) c->measure(avail);
    return avail;
}

void Widget::arrange(const UiRect& finalRect) {
    computedRect = finalRect;
    arrangeChildrenByAnchors();
}

bool Widget::hitTest(Vec2 uiPos) const {
    if (!interactive || visibility != Visibility::Visible) return false;
    return computedRect.Contains(uiPos);
}

void Widget::drawChildren(UiDrawContext& ctx) {
    for (auto& c : m_children)
        if (c->visibility == Visibility::Visible) c->draw(ctx);
}

// AnchorRect + 부모 사각형 → 자식 사각형(스크린 픽셀, 정수 스냅).
//   정본 구현: UiSystem/레이아웃 컨테이너·테스트가 공유하도록 자유 함수로도 노출.
UiRect ComputeAnchoredRect(const AnchorRect& a, const UiRect& parent) {
    const bool stretchX = a.anchorMin.x != a.anchorMax.x;
    const bool stretchY = a.anchorMin.y != a.anchorMax.y;

    // 부모 사각형 안에서 앵커 지점(픽셀). +Y 아래.
    const float axMin = parent.x + parent.w * a.anchorMin.x;
    const float axMax = parent.x + parent.w * a.anchorMax.x;
    const float ayMin = parent.y + parent.h * a.anchorMin.y;
    const float ayMax = parent.y + parent.h * a.anchorMax.y;

    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    if (stretchX) {
        // 앵커 밴드에서 좌(offsetMin.x)·우(offsetMax.x) 픽셀 마진.
        x = axMin + a.offsetMin.x;
        w = (axMax - a.offsetMax.x) - x;
    } else {
        // 점 앵커: 크기 = sizeDelta, 위치 = 앵커점 + offsetMin, pivot 로 정렬.
        w = a.sizeDelta.x;
        x = axMin + a.offsetMin.x - a.pivot.x * w;
    }

    if (stretchY) {
        y = ayMin + a.offsetMin.y;
        h = (ayMax - a.offsetMax.y) - y;
    } else {
        h = a.sizeDelta.y;
        y = ayMin + a.offsetMin.y - a.pivot.y * h;
    }

    // 정수 픽셀 스냅(도트 정합). 크기는 음수 방지.
    UiRect r;
    r.x = std::round(x);
    r.y = std::round(y);
    r.w = std::round(w);
    r.h = std::round(h);
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

void Widget::arrangeChildrenByAnchors() {
    for (auto& c : m_children) {
        if (c->visibility == Visibility::Collapsed) continue;
        c->arrange(ComputeAnchoredRect(c->anchors, computedRect));
    }
}

} // namespace mye::ui
