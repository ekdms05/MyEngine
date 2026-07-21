// InputTests.cpp — InputState 상태 전이 + win32 스캔코드→KeyCode 매핑 검증 (docs/01 §저수준 입력)
#include "TestFramework.h"

#include "mye/core/Input.h"
#include "mye/core/platform/Win32Input.h"

using namespace mye;

// ---- InputState 폴링 상태 전이 (프레임 경계 시뮬) ----

MYE_TEST(InputKeyPressReleaseEdges) {
    InputState in;
    // 프레임 0: 아무것도 안 눌림.
    in.NewFrame();
    MYE_EXPECT(!in.IsDown(KeyCode::W));
    MYE_EXPECT(!in.WasPressed(KeyCode::W));

    // 프레임 1: W down 이벤트 도착 → 이번 프레임 press 엣지.
    in.NewFrame();
    in.OnKey(KeyCode::W, true);
    MYE_EXPECT(in.IsDown(KeyCode::W));
    MYE_EXPECT(in.WasPressed(KeyCode::W));
    MYE_EXPECT(!in.WasReleased(KeyCode::W));

    // 프레임 2: 계속 눌린 상태(새 이벤트 없음) → held, 엣지 아님.
    in.NewFrame();
    MYE_EXPECT(in.IsDown(KeyCode::W));
    MYE_EXPECT(!in.WasPressed(KeyCode::W));
    MYE_EXPECT(!in.WasReleased(KeyCode::W));

    // 프레임 3: W up → release 엣지.
    in.NewFrame();
    in.OnKey(KeyCode::W, false);
    MYE_EXPECT(!in.IsDown(KeyCode::W));
    MYE_EXPECT(!in.WasPressed(KeyCode::W));
    MYE_EXPECT(in.WasReleased(KeyCode::W));

    // 프레임 4: 완전히 뗀 상태 → 엣지 없음.
    in.NewFrame();
    MYE_EXPECT(!in.IsDown(KeyCode::W));
    MYE_EXPECT(!in.WasReleased(KeyCode::W));
}

MYE_TEST(InputPressAndReleaseSameFrame) {
    InputState in;
    in.NewFrame();
    // 같은 프레임 안에서 down 후 up(빠른 탭) — 마지막 상태는 up, 이전 프레임은 not-down이므로
    // 현재 프레임 릴리즈 엣지 판정(WasReleased)은 이전=false, 현재=false → false.
    // (이 정책은 프레임 경계 스냅샷 기반 — 프레임 내 순간 토글은 IsDown으로 포착 불가.)
    in.OnKey(KeyCode::Space, true);
    in.OnKey(KeyCode::Space, false);
    MYE_EXPECT(!in.IsDown(KeyCode::Space));
    MYE_EXPECT(!in.WasPressed(KeyCode::Space));
    MYE_EXPECT(!in.WasReleased(KeyCode::Space));
}

MYE_TEST(InputMouseButtonEdges) {
    InputState in;
    in.NewFrame();
    in.OnMouseButton(MouseButton::Left, true);
    MYE_EXPECT(in.IsDown(MouseButton::Left));
    MYE_EXPECT(in.WasPressed(MouseButton::Left));

    in.NewFrame();
    MYE_EXPECT(in.IsDown(MouseButton::Left));
    MYE_EXPECT(!in.WasPressed(MouseButton::Left));

    in.NewFrame();
    in.OnMouseButton(MouseButton::Left, false);
    MYE_EXPECT(!in.IsDown(MouseButton::Left));
    MYE_EXPECT(in.WasReleased(MouseButton::Left));
}

MYE_TEST(InputMouseDeltaAccumulatesAndResets) {
    InputState in;
    in.NewFrame();
    in.OnMouseMove(Vec2i{10, 20}, Vec2{3.0f, -2.0f});
    in.OnMouseMove(Vec2i{12, 18}, Vec2{1.0f, 1.0f});
    MYE_EXPECT(in.MousePosition() == (Vec2i{12, 18}));   // 최신 위치
    MYE_EXPECT(in.MouseDelta().x == 4.0f);               // 누적 델타 (3+1)
    MYE_EXPECT(in.MouseDelta().y == -1.0f);              // (-2+1)

    // 다음 프레임 경계 → 델타 리셋(위치는 유지).
    in.NewFrame();
    MYE_EXPECT(in.MouseDelta().x == 0.0f);
    MYE_EXPECT(in.MouseDelta().y == 0.0f);
    MYE_EXPECT(in.MousePosition() == (Vec2i{12, 18}));
}

MYE_TEST(InputWheelAccumulatesAndResets) {
    InputState in;
    in.NewFrame();
    in.OnWheel(1.0f);
    in.OnWheel(0.5f);
    MYE_EXPECT(in.WheelDelta() == 1.5f);
    in.NewFrame();
    MYE_EXPECT(in.WheelDelta() == 0.0f);
}

// ---- win32 스캔코드(세트1) → 물리 KeyCode 매핑 ----

MYE_TEST(ScanCodeLettersAndWasd) {
    using namespace mye::win32;
    // 세트1 make 코드: W=0x11, A=0x1E, S=0x1F, D=0x20 (E0 아님).
    MYE_EXPECT(ScanCodeToKeyCode(0x11, false, false, 'W') == KeyCode::W);
    MYE_EXPECT(ScanCodeToKeyCode(0x1E, false, false, 'A') == KeyCode::A);
    MYE_EXPECT(ScanCodeToKeyCode(0x1F, false, false, 'S') == KeyCode::S);
    MYE_EXPECT(ScanCodeToKeyCode(0x20, false, false, 'D') == KeyCode::D);
}

MYE_TEST(ScanCodeArrowsAreExtended) {
    using namespace mye::win32;
    // 화살표는 E0 프리픽스: Up=E0 48, Down=E0 50, Left=E0 4B, Right=E0 4D.
    MYE_EXPECT(ScanCodeToKeyCode(0x48, true, false, 0) == KeyCode::Up);
    MYE_EXPECT(ScanCodeToKeyCode(0x50, true, false, 0) == KeyCode::Down);
    MYE_EXPECT(ScanCodeToKeyCode(0x4B, true, false, 0) == KeyCode::Left);
    MYE_EXPECT(ScanCodeToKeyCode(0x4D, true, false, 0) == KeyCode::Right);
    // 같은 make 코드라도 E0가 없으면 넘패드/다른 키 → 화살표가 아니다.
    MYE_EXPECT(ScanCodeToKeyCode(0x48, false, false, 0) != KeyCode::Up);
}

MYE_TEST(ScanCodeModifiersLeftRight) {
    using namespace mye::win32;
    MYE_EXPECT(ScanCodeToKeyCode(0x1D, false, false, 0) == KeyCode::LeftControl);
    MYE_EXPECT(ScanCodeToKeyCode(0x1D, true,  false, 0) == KeyCode::RightControl);  // E0 1D
    MYE_EXPECT(ScanCodeToKeyCode(0x2A, false, false, 0) == KeyCode::LeftShift);
    MYE_EXPECT(ScanCodeToKeyCode(0x38, false, false, 0) == KeyCode::LeftAlt);
    MYE_EXPECT(ScanCodeToKeyCode(0x38, true,  false, 0) == KeyCode::RightAlt);      // E0 38 (AltGr)
}

MYE_TEST(ScanCodeVirtualKeyFallback) {
    using namespace mye::win32;
    // make 코드 0 → 가상 키 폴백. 'Q' 가상키는 KeyCode::Q로.
    MYE_EXPECT(ScanCodeToKeyCode(0, false, false, 'Q') == KeyCode::Q);
    // 알 수 없는 조합(make 0, vkey 0) → Unknown.
    MYE_EXPECT(ScanCodeToKeyCode(0, false, false, 0) == KeyCode::Unknown);
}
