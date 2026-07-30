// mye/ui/Widgets.h — MVP 위젯 집합 (docs/06 §1 위젯 + MVP 범위)
//
// 소유: 06 (M5-A). MVP: Panel(컨테이너/9-slice), Label(한글 텍스트), Image(9-slice), Button.
//   + 레이아웃 컨테이너(StackLayout 수평/수직, GridLayout), Window(드래그·닫기).
//   Phase 2 위젯(GridView·ScrollView·TextInput·ListView)은 계약만 전방선언 수준으로 남기고
//   구현은 후속 — 여기서는 MVP 위젯을 동결한다.
#pragma once

#include "mye/ui/Widget.h"
#include "mye/ui/UiDrawContext.h"
#include "mye/text/TextTypes.h"
#include "mye/text/TextLayout.h"

#include <functional>
#include <string>

namespace mye::ui {

// ---------------------------------------------------------------------------
// Panel — 배경 스프라이트(옵션 9-slice) + 자식 컨테이너. 창 프레임·HUD 패널 기본.
// ---------------------------------------------------------------------------
class Panel : public Widget {
public:
    MYE_WIDGET(mye::ui::Panel);
    UiSprite  background{};
    NineSlice slice{};
    Color     tint = Color::White();
    bool      drawBackground = false;   // background.texture 유효 시 그림

    void draw(UiDrawContext& ctx) override;
    Vec2 measure(Vec2 avail) override;
    void arrange(const UiRect& finalRect) override;
};

// ---------------------------------------------------------------------------
// Label — 한글 리치 텍스트 표시. 내부에 TextLayout 캐시(dirty 시 재레이아웃).
// ---------------------------------------------------------------------------
class Label : public Widget {
public:
    MYE_WIDGET(mye::ui::Label);
    std::string       text;             // UTF-8 리치 텍스트({color} 등)
    text::TextStyle   style{};
    text::TextAlign   align = text::TextAlign::Left;
    text::WrapMode    wrap = text::WrapMode::WordChar;
    bool              autoSize = false; // true 면 measure 가 텍스트 크기를 반환

    void setText(std::string utf8);     // dirty 표시
    void draw(UiDrawContext& ctx) override;
    Vec2 measure(Vec2 avail) override;

private:
    text::TextLayout m_layout;
    bool m_dirty = true;
    Vec2 m_lastAvail{};
};

// ---------------------------------------------------------------------------
// Image — 스프라이트(9-slice 지원). 정수 배율 옵션(픽셀아트 모서리).
// ---------------------------------------------------------------------------
class Image : public Widget {
public:
    MYE_WIDGET(mye::ui::Image);
    UiSprite  sprite{};
    NineSlice slice{};
    Color     tint = Color::White();

    void draw(UiDrawContext& ctx) override;
};

// ---------------------------------------------------------------------------
// Button — Pressed/Hover 상태 → 스킨 상태 스타일. onClick 콜백(→ Lua 컨트롤러 라우팅).
// ---------------------------------------------------------------------------
class Button : public Widget {
public:
    MYE_WIDGET(mye::ui::Button);
    enum class State : uint8_t { Normal, Hover, Pressed, Disabled };

    UiSprite  normalSprite{}, hoverSprite{}, pressedSprite{};
    NineSlice slice{};
    Label*    label = nullptr;           // 자식으로 추가된 라벨(옵션, 캡션)
    std::function<void()> onClick;       // C++ 핸들러. Lua 는 UiSystem 이 라우팅.
    State     state = State::Normal;

    bool onEvent(UiEvent& e) override;
    void draw(UiDrawContext& ctx) override;
};

// ---------------------------------------------------------------------------
// StackLayout — 자식을 수평/수직으로 자동 배치. spacing·padding.
// ---------------------------------------------------------------------------
class StackLayout : public Widget {
public:
    MYE_WIDGET(mye::ui::StackLayout);
    enum class Orientation : uint8_t { Horizontal, Vertical };
    Orientation orientation = Orientation::Vertical;
    float spacing = 0.0f;
    Vec2  padding{};                     // (좌우, 상하) — 대칭 패딩

    Vec2 measure(Vec2 avail) override;
    void arrange(const UiRect& finalRect) override;
    void draw(UiDrawContext& ctx) override { drawChildren(ctx); }
};

// ---------------------------------------------------------------------------
// GridLayout — 셀 격자 자동 배치(인벤토리·퀵슬롯 레이아웃 기반).
// ---------------------------------------------------------------------------
class GridLayout : public Widget {
public:
    MYE_WIDGET(mye::ui::GridLayout);
    Vec2i cellSize{32, 32};
    int   columns = 4;
    float spacing = 2.0f;

    Vec2 measure(Vec2 avail) override;
    void arrange(const UiRect& finalRect) override;
    void draw(UiDrawContext& ctx) override { drawChildren(ctx); }
};

// ---------------------------------------------------------------------------
// Window — 드래그 가능한 창(타이틀바 드래그·닫기 버튼·z-order). MVP 슬라이스 대화창 프레임.
// ---------------------------------------------------------------------------
class Window : public Panel {
public:
    MYE_WIDGET(mye::ui::Window);
    std::string title;
    bool  draggable = true;
    bool  closable  = true;
    float titleBarHeight = 24.0f;
    std::function<void()> onClose;

    bool onEvent(UiEvent& e) override;
    void draw(UiDrawContext& ctx) override;

private:
    bool m_dragging = false;
    Vec2 m_dragOffset{};
};

// ---------------------------------------------------------------------------
// TextInput — 한 줄 텍스트 편집(UTF-8·커서·백스페이스·화살표·Enter 제출). IME 확정 문자 수신.
//   상태 머신은 순수 로직(GPU 무관) — 이벤트 주입으로 헤드리스 테스트.
// ---------------------------------------------------------------------------
class TextInput : public Widget {
public:
    MYE_WIDGET(mye::ui::TextInput);
    std::string     text;                 // UTF-8 확정 버퍼
    uint32_t        cursor = 0;           // 바이트 오프셋(코드포인트 경계)
    bool            focused = false;
    text::TextStyle style{};
    Color           background = {0.12f, 0.12f, 0.14f, 1.0f};
    Color           textColor  = Color::White();
    float           height = 24.0f;
    std::function<void(const std::string&)> onSubmit;   // Enter 시 호출

    bool onEvent(UiEvent& e) override;
    void draw(UiDrawContext& ctx) override;
    Vec2 measure(Vec2 avail) override;
};

// ---------------------------------------------------------------------------
// ScrollView — 자식을 세로로 쌓고 스크롤 오프셋만큼 이동, 시저로 클리핑. 휠 스크롤.
//   자식은 AnchorRect.sizeDelta.y 높이로 세로 배치(콘텐츠 컬럼).
// ---------------------------------------------------------------------------
class ScrollView : public Widget {
public:
    MYE_WIDGET(mye::ui::ScrollView);
    Vec2  scrollOffset{0.0f, 0.0f};
    float wheelSensitivity = 30.0f;
    float contentHeight = 0.0f;           // arrange 가 계산(자식 높이 합)

    float MaxScroll() const { return contentHeight > computedRect.h ? contentHeight - computedRect.h : 0.0f; }

    bool onEvent(UiEvent& e) override;
    void draw(UiDrawContext& ctx) override;
    Vec2 measure(Vec2 avail) override;
    void arrange(const UiRect& finalRect) override;
};

// ---------------------------------------------------------------------------
// ListView — 텍스트 라인 목록(채팅·로그·인벤 리스트). 가상화(보이는 라인만 그림)·선택·하단고정.
// ---------------------------------------------------------------------------
class ListView : public Widget {
public:
    MYE_WIDGET(mye::ui::ListView);
    std::vector<std::string> items;
    float           lineHeight = 24.0f;
    int             selected = -1;        // 선택 인덱스(-1=없음)
    float           scrollOffset = 0.0f;  // 세로 스크롤(px)
    bool            followTail = false;   // 추가 시 하단 자동 스크롤(채팅창)
    float           wheelSensitivity = 30.0f;
    text::TextStyle style{};
    Color           selectedColor = {0.25f, 0.35f, 0.55f, 1.0f};

    void  addLine(std::string_view utf8);
    float ContentHeight() const { return static_cast<float>(items.size()) * lineHeight; }
    float MaxScroll() const;
    // 가상화: 현재 스크롤에서 보이는 라인 [First, Last) 반개구간.
    int   FirstVisible() const;
    int   LastVisible() const;

    bool onEvent(UiEvent& e) override;
    void draw(UiDrawContext& ctx) override;
    Vec2 measure(Vec2 avail) override;
};

} // namespace mye::ui
