// UiPhase2Tests.cpp — Phase 2 위젯(TextInput/ScrollView/ListView) 로직 (docs/06, M7 렌더 잔여)
//
// 상호작용 로직은 순수(GPU 무관): 이벤트 주입 + measure/arrange 호출 후 상태/레이아웃 검증.
#include "TestFramework.h"

#include "mye/ui/Widgets.h"
#include "mye/core/Input.h"

#include <memory>
#include <string>

using namespace mye;
using namespace mye::ui;

namespace {
UiEvent TextEvent(const char* s) {
    UiEvent e; e.type = UiEventType::TextInput;
    int i = 0; for (; s[i] && i < 7; ++i) e.utf8[i] = s[i]; e.utf8[i] = '\0';
    return e;
}
UiEvent KeyEvent(KeyCode k) { UiEvent e; e.type = UiEventType::KeyDown; e.key = k; return e; }
UiEvent ScrollEvent(float d) { UiEvent e; e.type = UiEventType::Scroll; e.scrollDelta = d; return e; }
// onEvent 는 UiEvent&(lvalue) 를 받으므로 값으로 받아 lvalue 로 전달.
bool Send(Widget& w, UiEvent e) { return w.onEvent(e); }
}

MYE_TEST(UiTextInputEditing) {
    TextInput input;
    input.computedRect = UiRect{0, 0, 200, 24};

    // 문자 입력 → 버퍼·커서 전진.
    MYE_EXPECT(Send(input, TextEvent("H")));
    Send(input, TextEvent("i"));
    MYE_EXPECT(input.text == "Hi" && input.cursor == 2);

    // 백스페이스 → 마지막 문자 삭제.
    MYE_EXPECT(Send(input, KeyEvent(KeyCode::Backspace)));
    MYE_EXPECT(input.text == "H" && input.cursor == 1);

    // 커서 왼쪽 이동 후 삽입(중간 삽입).
    Send(input, KeyEvent(KeyCode::Left)); MYE_EXPECT(input.cursor == 0);
    Send(input, TextEvent("O"));
    MYE_EXPECT(input.text == "OH" && input.cursor == 1);

    // 포커스 상태.
    UiEvent fg; fg.type = UiEventType::FocusGained;
    Send(input, fg);
    MYE_EXPECT(input.focused);
}

MYE_TEST(UiTextInputUtf8AndSubmit) {
    TextInput input;
    input.computedRect = UiRect{0, 0, 200, 24};

    // 멀티바이트(한글 '가' = 3바이트) 입력 후 백스페이스 → 코드포인트 단위 삭제(쪼개짐 없음).
    Send(input, TextEvent("가"));
    MYE_EXPECT(input.text == "가" && input.cursor == 3);
    Send(input, KeyEvent(KeyCode::Backspace));
    MYE_EXPECT(input.text.empty() && input.cursor == 0);

    // Enter → onSubmit 콜백.
    std::string submitted;
    input.onSubmit = [&](const std::string& t) { submitted = t; };
    Send(input, TextEvent("k"));
    Send(input, KeyEvent(KeyCode::Enter));
    MYE_EXPECT(submitted == "k");
}

MYE_TEST(UiScrollViewOffsetAndClamp) {
    ScrollView sv;
    // 자식: 높이 300(부모 100보다 큼 → 스크롤 필요).
    auto childPtr = std::make_unique<Panel>();
    childPtr->anchors.sizeDelta = Vec2{100, 300};
    Panel* child = static_cast<Panel*>(sv.addChild(std::move(childPtr)));

    sv.arrange(UiRect{0, 0, 100, 100});
    MYE_EXPECT(sv.contentHeight == 300.0f);
    MYE_EXPECT(sv.MaxScroll() == 200.0f);

    // 휠 스크롤 → 오프셋 증가 + 재배치 시 자식이 위로 이동.
    MYE_EXPECT(Send(sv, ScrollEvent(1.0f)));   // +30
    MYE_EXPECT(sv.scrollOffset.y == 30.0f);
    sv.arrange(UiRect{0, 0, 100, 100});
    MYE_EXPECT(child->computedRect.y == -30.0f);   // 콘텐츠가 위로

    // 과도 스크롤 → MaxScroll 로 클램프.
    for (int i = 0; i < 100; ++i) Send(sv, ScrollEvent(1.0f));
    MYE_EXPECT(sv.scrollOffset.y == 200.0f);
    // 음수 방향 클램프.
    for (int i = 0; i < 100; ++i) Send(sv, ScrollEvent(-1.0f));
    MYE_EXPECT(sv.scrollOffset.y == 0.0f);
}

MYE_TEST(UiListViewVirtualizationAndSelect) {
    ListView list;
    list.computedRect = UiRect{0, 0, 200, 100};
    list.lineHeight = 20.0f;

    for (int i = 0; i < 10; ++i) list.addLine("Line " + std::to_string(i));
    MYE_EXPECT(list.items.size() == 10);
    MYE_EXPECT(list.ContentHeight() == 200.0f && list.MaxScroll() == 100.0f);

    // 스크롤 0 → 보이는 라인 [0, 5)(100/20 = 5줄).
    MYE_EXPECT(list.FirstVisible() == 0 && list.LastVisible() == 5);

    // 스크롤 30(1.5줄) → 보이는 라인 [1, 7)(부분 포함).
    Send(list, ScrollEvent(1.0f));   // +30 → 30
    MYE_EXPECT(list.scrollOffset == 30.0f);
    MYE_EXPECT(list.FirstVisible() == 1 && list.LastVisible() == 7);

    // 하단까지 스크롤 → MaxScroll 클램프, 마지막 라인 보임.
    for (int i = 0; i < 20; ++i) Send(list, ScrollEvent(1.0f));
    MYE_EXPECT(list.scrollOffset == 100.0f);
    MYE_EXPECT(list.LastVisible() == 10);

    // 클릭 선택: 스크롤 0으로 되돌리고 y=50 클릭 → 라인 2 선택.
    list.scrollOffset = 0.0f;
    UiEvent click; click.type = UiEventType::PointerDown; click.pointerPos = Vec2{10, 50};
    MYE_EXPECT(Send(list, click));
    MYE_EXPECT(list.selected == 2);   // floor(50/20)=2
}

MYE_TEST(UiListViewFollowTail) {
    ListView list;
    list.computedRect = UiRect{0, 0, 200, 100};
    list.lineHeight = 20.0f;
    list.followTail = true;

    // 채팅창처럼 라인 추가 시 하단 고정.
    for (int i = 0; i < 3; ++i) list.addLine("msg");   // 3줄 < 5줄 가시 → 스크롤 0
    MYE_EXPECT(list.scrollOffset == 0.0f);
    for (int i = 0; i < 7; ++i) list.addLine("msg");   // 총 10줄 → 하단 고정
    MYE_EXPECT(list.scrollOffset == list.MaxScroll());
    MYE_EXPECT(list.LastVisible() == 10);   // 마지막 라인 보임
}
