// mye/ui/Widgets.cpp — MVP 위젯 (드로우·레이아웃·이벤트 구현)
#include "mye/ui/Widgets.h"

#include "mye/text/TextRenderer.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/TextLayout.h"

#include <algorithm>
#include <cmath>

namespace mye::ui {

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------
void Panel::draw(UiDrawContext& ctx) {
    if (drawBackground && background.texture.IsValid()) {
        if (slice.Enabled())
            ctx.DrawNineSlice(computedRect, background, slice, tint);
        else
            ctx.DrawSprite(computedRect, background, tint);
    } else if (drawBackground) {
        // 텍스처 없을 때는 단색 rect(스킨 미배선 프리뷰·헤드리스 덤프).
        ctx.DrawRect(computedRect, tint);
    }
    drawChildren(ctx);
}

Vec2 Panel::measure(Vec2 avail) { return Widget::measure(avail); }
void Panel::arrange(const UiRect& finalRect) { Widget::arrange(finalRect); }

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------
void Label::setText(std::string utf8) { text = std::move(utf8); m_dirty = true; }

void Label::draw(UiDrawContext& ctx) {
    text::LayoutParams p{};
    p.maxWidth = computedRect.w;
    p.align = align;
    p.wrap = wrap;
    ctx.DrawText(computedRect, text, style, p);
}

Vec2 Label::measure(Vec2 avail) {
    m_lastAvail = avail;
    if (!autoSize) return avail;
    // autoSize: 폭 제한 하에 텍스트 크기를 반환(아틀라스 업로드 없는 측정 경로는 렌더러가 소유 —
    //   여기서는 스타일 기반 근사: 폭=avail.x, 높이=줄 높이). 실제 폭/줄수 측정은 DrawContext 의
    //   폰트 provider 로 하나, measure 패스는 provider 접근이 없으므로 근사(한 줄 높이 최소 보장).
    const float lineH = static_cast<float>(style.size) + 2.0f;
    return Vec2{avail.x, lineH};
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------
void Image::draw(UiDrawContext& ctx) {
    if (!sprite.texture.IsValid()) return;
    if (slice.Enabled())
        ctx.DrawNineSlice(computedRect, sprite, slice, tint);
    else
        ctx.DrawSprite(computedRect, sprite, tint);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
bool Button::onEvent(UiEvent& e) {
    if (state == State::Disabled) return false;
    switch (e.type) {
        case UiEventType::PointerEnter:
            if (state == State::Normal) state = State::Hover;
            return false;
        case UiEventType::PointerLeave:
            if (state == State::Hover) state = State::Normal;
            return false;
        case UiEventType::PointerDown:
            state = State::Pressed;
            return true;
        case UiEventType::PointerUp:
            if (state == State::Pressed) state = State::Hover;
            return true;
        case UiEventType::PointerClick:
            if (onClick) { onClick(); return true; }
            return false;
        default: return false;
    }
}

void Button::draw(UiDrawContext& ctx) {
    const UiSprite* s = &normalSprite;
    if (state == State::Hover   && hoverSprite.texture.IsValid())   s = &hoverSprite;
    if (state == State::Pressed && pressedSprite.texture.IsValid()) s = &pressedSprite;
    if (s->texture.IsValid()) {
        if (slice.Enabled()) ctx.DrawNineSlice(computedRect, *s, slice);
        else                 ctx.DrawSprite(computedRect, *s);
    } else {
        // 텍스처 미배선: 상태별 단색(헤드리스 덤프·프리뷰).
        Color c{0.30f, 0.30f, 0.34f, 1.0f};
        if (state == State::Hover)   c = Color{0.40f, 0.40f, 0.46f, 1.0f};
        if (state == State::Pressed) c = Color{0.20f, 0.20f, 0.24f, 1.0f};
        ctx.DrawRect(computedRect, c);
    }
    drawChildren(ctx);
}

// ---------------------------------------------------------------------------
// StackLayout — 자식 순차 배치(수평/수직). 자식 앵커의 sizeDelta 를 주 축 크기로 사용,
//   교차 축은 (컨테이너 - 패딩) 으로 스트레치. 앵커 무시(레이아웃이 위치 소유).
// ---------------------------------------------------------------------------
Vec2 StackLayout::measure(Vec2 avail) {
    for (auto& c : children())
        if (c->visibility != Visibility::Collapsed) c->measure(avail);
    return avail;
}

void StackLayout::arrange(const UiRect& finalRect) {
    computedRect = finalRect;
    const float padX = padding.x, padY = padding.y;
    float cursor = (orientation == Orientation::Vertical ? finalRect.y + padY
                                                         : finalRect.x + padX);
    const float crossPos  = (orientation == Orientation::Vertical ? finalRect.x + padX
                                                                  : finalRect.y + padY);
    const float crossSize = (orientation == Orientation::Vertical ? finalRect.w - 2 * padX
                                                                  : finalRect.h - 2 * padY);
    bool first = true;
    for (auto& c : children()) {
        if (c->visibility == Visibility::Collapsed) continue;
        if (!first) cursor += spacing;
        first = false;

        UiRect r;
        if (orientation == Orientation::Vertical) {
            r.x = std::round(crossPos);
            r.y = std::round(cursor);
            r.w = std::round(crossSize);
            r.h = std::round(c->anchors.sizeDelta.y);
            cursor += r.h;
        } else {
            r.x = std::round(cursor);
            r.y = std::round(crossPos);
            r.w = std::round(c->anchors.sizeDelta.x);
            r.h = std::round(crossSize);
            cursor += r.w;
        }
        if (r.w < 0.0f) r.w = 0.0f;
        if (r.h < 0.0f) r.h = 0.0f;
        c->arrange(r);
    }
}

// ---------------------------------------------------------------------------
// GridLayout — columns×cellSize 격자 순차 배치(좌→우, 위→아래). 인벤토리·퀵슬롯 기반.
// ---------------------------------------------------------------------------
Vec2 GridLayout::measure(Vec2 avail) {
    for (auto& c : children())
        if (c->visibility != Visibility::Collapsed) c->measure(avail);
    return avail;
}

void GridLayout::arrange(const UiRect& finalRect) {
    computedRect = finalRect;
    const int cols = columns > 0 ? columns : 1;
    int i = 0;
    for (auto& c : children()) {
        if (c->visibility == Visibility::Collapsed) continue;
        const int col = i % cols;
        const int row = i / cols;
        UiRect r;
        r.x = std::round(finalRect.x + col * (cellSize.x + spacing));
        r.y = std::round(finalRect.y + row * (cellSize.y + spacing));
        r.w = static_cast<float>(cellSize.x);
        r.h = static_cast<float>(cellSize.y);
        c->arrange(r);
        ++i;
    }
}

// ---------------------------------------------------------------------------
// Window — 드래그 가능한 창(타이틀바 드래그·닫기 버튼). Panel(9slice 프레임) 상속.
//   드래그는 앵커 offsetMin(점 앵커 위치)을 갱신해 다음 arrange 에 반영한다.
// ---------------------------------------------------------------------------
static UiRect TitleBarRect(const UiRect& frame, float h) {
    return UiRect{frame.x, frame.y, frame.w, h};
}
static UiRect CloseButtonRect(const UiRect& frame, float barH) {
    const float sz = std::max(0.0f, barH - 6.0f);
    return UiRect{frame.x + frame.w - sz - 3.0f, frame.y + 3.0f, sz, sz};
}

bool Window::onEvent(UiEvent& e) {
    switch (e.type) {
        case UiEventType::PointerDown: {
            // 닫기 버튼?
            if (closable && CloseButtonRect(computedRect, titleBarHeight).Contains(e.pointerPos)) {
                if (onClose) onClose();
                e.handled = true;
                return true;
            }
            // 타이틀바 → 드래그 시작.
            if (draggable && TitleBarRect(computedRect, titleBarHeight).Contains(e.pointerPos)) {
                m_dragging = true;
                m_dragOffset = e.pointerPos - Vec2{computedRect.x, computedRect.y};
                return true;
            }
            return false;
        }
        case UiEventType::DragStart:
            if (draggable && TitleBarRect(computedRect, titleBarHeight).Contains(e.pointerPos)) {
                m_dragging = true;
                m_dragOffset = e.pointerPos - Vec2{computedRect.x, computedRect.y};
                return true;
            }
            return false;
        case UiEventType::Drag:
            if (m_dragging) {
                // 새 좌상단 = 포인터 - 잡은 오프셋. 점 앵커 위치(offsetMin, pivot=0 가정)로 반영.
                const Vec2 newTL = e.pointerPos - m_dragOffset;
                // 앵커를 좌상단 점앵커로 강제(드래그 창은 절대 배치).
                anchors.anchorMin = anchors.anchorMax = Vec2{0, 0};
                anchors.pivot = Vec2{0, 0};
                anchors.sizeDelta = Vec2{computedRect.w, computedRect.h};
                anchors.offsetMin = newTL;
                anchors.offsetMax = Vec2{0, 0};
                // 즉시 반영(다음 Update 의 arrange 전에 draw 되어도 위치가 맞도록).
                if (m_parent) computedRect = ComputeAnchoredRect(anchors, m_parent->computedRect);
                else { computedRect.x = std::round(newTL.x); computedRect.y = std::round(newTL.y); }
                arrangeChildrenByAnchors();
                return true;
            }
            return false;
        case UiEventType::PointerUp:
        case UiEventType::Drop:
            if (m_dragging) { m_dragging = false; return true; }
            return false;
        default:
            return Panel::onEvent(e);
    }
}

void Window::draw(UiDrawContext& ctx) {
    Panel::draw(ctx);   // 프레임 배경(9slice 또는 단색)

    // 타이틀바.
    const UiRect bar = TitleBarRect(computedRect, titleBarHeight);
    ctx.DrawRect(bar, Color{0.15f, 0.17f, 0.24f, 1.0f});

    // 타이틀 텍스트.
    if (!title.empty()) {
        text::TextStyle ts{};
        ts.size = static_cast<uint16_t>(std::max(8.0f, titleBarHeight - 8.0f));
        ts.color = Color::White();
        text::LayoutParams p{};
        p.maxWidth = bar.w;
        p.wrap = text::WrapMode::None;
        const UiRect titleRect{bar.x + 6.0f, bar.y + 3.0f, bar.w - 6.0f, bar.h};
        ctx.DrawText(titleRect, title, ts, p);
    }

    // 닫기 버튼.
    if (closable) {
        const UiRect cb = CloseButtonRect(computedRect, titleBarHeight);
        ctx.DrawRect(cb, Color{0.55f, 0.18f, 0.18f, 1.0f});
    }

    // 자식은 Panel::draw 가 이미 그림(drawChildren) — 중복 방지 위해 여기서 다시 그리지 않음.
}

// ---------------------------------------------------------------------------
// Phase 2 위젯: TextInput / ScrollView / ListView
// ---------------------------------------------------------------------------
namespace {
// UTF-8 코드포인트 경계 유틸(커서 이동/삭제가 멀티바이트 문자를 쪼개지 않게).
bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }
uint32_t PrevCp(const std::string& s, uint32_t i) {
    if (i == 0 || i > s.size()) return 0;
    uint32_t j = i - 1;
    while (j > 0 && IsCont(static_cast<unsigned char>(s[j]))) --j;
    return j;
}
uint32_t NextCp(const std::string& s, uint32_t i) {
    if (i >= s.size()) return static_cast<uint32_t>(s.size());
    uint32_t j = i + 1;
    while (j < s.size() && IsCont(static_cast<unsigned char>(s[j]))) ++j;
    return j;
}
} // namespace

// ---- TextInput ----
Vec2 TextInput::measure(Vec2 avail) { return Vec2{avail.x, height}; }

bool TextInput::onEvent(UiEvent& e) {
    switch (e.type) {
        case UiEventType::FocusGained: focused = true;  return true;
        case UiEventType::FocusLost:   focused = false; return false;
        case UiEventType::PointerDown: focused = true;  return true;
        case UiEventType::TextInput: {
            uint32_t n = 0;
            while (n < sizeof(e.utf8) && e.utf8[n] != '\0') ++n;
            if (n > 0) {
                if (cursor > text.size()) cursor = static_cast<uint32_t>(text.size());
                text.insert(text.begin() + cursor, e.utf8, e.utf8 + n);
                cursor += n;
            }
            return true;
        }
        case UiEventType::KeyDown:
            if (e.key == KeyCode::Backspace) {
                if (cursor > 0) { uint32_t p = PrevCp(text, cursor); text.erase(p, cursor - p); cursor = p; }
                return true;
            }
            if (e.key == KeyCode::Left)  { cursor = PrevCp(text, cursor); return true; }
            if (e.key == KeyCode::Right) { cursor = NextCp(text, cursor); return true; }
            if (e.key == KeyCode::Enter) { if (onSubmit) onSubmit(text); return true; }
            return false;
        default: return false;
    }
}

void TextInput::draw(UiDrawContext& ctx) {
    ctx.DrawRect(computedRect, background);
    if (!text.empty()) {
        text::LayoutParams lp; lp.maxWidth = computedRect.w; lp.wrap = text::WrapMode::None;
        ctx.DrawText(computedRect, text, style, lp);
    }
    if (focused) {
        // 캐럿(좌측 근사 위치의 얇은 세로선 — 정밀 x는 텍스트 measure 후속).
        UiRect caret{computedRect.x + 2.0f, computedRect.y + 2.0f, 1.5f, computedRect.h - 4.0f};
        ctx.DrawRect(caret, textColor);
    }
}

// ---- ScrollView ----
Vec2 ScrollView::measure(Vec2 avail) {
    for (auto& c : children())
        if (c->visibility != Visibility::Collapsed) c->measure(avail);
    return avail;
}

void ScrollView::arrange(const UiRect& finalRect) {
    computedRect = finalRect;
    // 콘텐츠 높이 = 보이는 자식 sizeDelta.y 합(세로 콘텐츠 컬럼).
    contentHeight = 0.0f;
    for (auto& c : children())
        if (c->visibility != Visibility::Collapsed) contentHeight += c->anchors.sizeDelta.y;
    // 오프셋 클램프.
    const float maxS = MaxScroll();
    if (scrollOffset.y < 0.0f) scrollOffset.y = 0.0f;
    if (scrollOffset.y > maxS) scrollOffset.y = maxS;
    // 자식 배치(오프셋만큼 위로).
    float cursor = finalRect.y - scrollOffset.y;
    for (auto& c : children()) {
        if (c->visibility == Visibility::Collapsed) continue;
        UiRect r;
        r.x = std::round(finalRect.x - scrollOffset.x);
        r.y = std::round(cursor);
        r.w = std::round(finalRect.w);
        r.h = std::round(c->anchors.sizeDelta.y);
        if (r.h < 0.0f) r.h = 0.0f;
        c->arrange(r);
        cursor += c->anchors.sizeDelta.y;
    }
}

bool ScrollView::onEvent(UiEvent& e) {
    if (e.type == UiEventType::Scroll) {
        scrollOffset.y += e.scrollDelta * wheelSensitivity;
        const float maxS = MaxScroll();
        if (scrollOffset.y < 0.0f) scrollOffset.y = 0.0f;
        if (scrollOffset.y > maxS) scrollOffset.y = maxS;
        return true;
    }
    return false;
}

void ScrollView::draw(UiDrawContext& ctx) {
    ctx.PushScissor(computedRect);
    drawChildren(ctx);
    ctx.PopScissor();
}

// ---- ListView ----
void ListView::addLine(std::string_view utf8) {
    items.emplace_back(utf8);
    if (followTail) scrollOffset = MaxScroll();   // 하단 고정(채팅창)
}

float ListView::MaxScroll() const {
    const float over = ContentHeight() - computedRect.h;
    return over > 0.0f ? over : 0.0f;
}

int ListView::FirstVisible() const {
    if (lineHeight <= 0.0f) return 0;
    int f = static_cast<int>(std::floor(scrollOffset / lineHeight));
    if (f < 0) f = 0;
    const int n = static_cast<int>(items.size());
    return f > n ? n : f;
}

int ListView::LastVisible() const {
    const int n = static_cast<int>(items.size());
    if (lineHeight <= 0.0f) return n;
    int l = static_cast<int>(std::ceil((scrollOffset + computedRect.h) / lineHeight));
    if (l > n) l = n;
    const int f = FirstVisible();
    return l < f ? f : l;
}

Vec2 ListView::measure(Vec2 avail) { return avail; }

bool ListView::onEvent(UiEvent& e) {
    if (e.type == UiEventType::Scroll) {
        scrollOffset += e.scrollDelta * wheelSensitivity;
        const float maxS = MaxScroll();
        if (scrollOffset < 0.0f) scrollOffset = 0.0f;
        if (scrollOffset > maxS) scrollOffset = maxS;
        return true;
    }
    if (e.type == UiEventType::PointerDown || e.type == UiEventType::PointerClick) {
        if (lineHeight > 0.0f && computedRect.Contains(e.pointerPos)) {
            const int idx = static_cast<int>(std::floor((e.pointerPos.y - computedRect.y + scrollOffset) / lineHeight));
            if (idx >= 0 && idx < static_cast<int>(items.size())) { selected = idx; return true; }
        }
        return false;
    }
    return false;
}

void ListView::draw(UiDrawContext& ctx) {
    ctx.PushScissor(computedRect);
    const int first = FirstVisible(), last = LastVisible();
    text::LayoutParams lp; lp.maxWidth = computedRect.w; lp.wrap = text::WrapMode::None;
    for (int i = first; i < last; ++i) {
        UiRect line{computedRect.x, computedRect.y + static_cast<float>(i) * lineHeight - scrollOffset,
                    computedRect.w, lineHeight};
        if (i == selected) ctx.DrawRect(line, selectedColor);
        ctx.DrawText(line, items[static_cast<size_t>(i)], style, lp);
    }
    ctx.PopScissor();
}

} // namespace mye::ui
