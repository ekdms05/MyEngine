// InputTests.cpp — InputState 상태 전이 + win32 스캔코드→KeyCode 매핑 검증 (docs/01 §저수준 입력)
#include "TestFramework.h"

#include "mye/core/Input.h"
#include "mye/core/platform/Win32Input.h"
#include "mye/runtime/GameInput.h"
#include <limits>

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
    // A quick tap keeps both edges even if no fixed tick ran while it was held.
    in.OnKey(KeyCode::Space, true);
    in.OnKey(KeyCode::Space, false);
    MYE_EXPECT(!in.IsDown(KeyCode::Space));
    MYE_EXPECT(in.WasPressed(KeyCode::Space));
    MYE_EXPECT(in.WasReleased(KeyCode::Space));
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

MYE_TEST(InputActionRemapKeepsQuickTapUntilOneFixedTick) {
    auto map = runtime::DefaultGameInputMap();
    for (auto& action : map.actions) if (action.name == "interact")
        action.bindings = {{InputDevice::Key, static_cast<int>(KeyCode::F)}};
    runtime::GameInputBuffer buffer;
    MYE_EXPECT(buffer.Configure(map));
    InputState input;
    input.NewFrame(); input.OnKey(KeyCode::E, true);
    buffer.Capture(input, true);
    MYE_EXPECT(!buffer.ConsumeTick().interact);
    input.NewFrame(); input.OnKey(KeyCode::F, true); input.OnKey(KeyCode::F, false);
    buffer.Capture(input, true);
    input.NewFrame(); buffer.Capture(input, true); // Render frame without a fixed tick.
    MYE_EXPECT(buffer.ConsumeTick().interact);
    MYE_EXPECT(buffer.Actions().Action("interact").pressed && buffer.Actions().Action("interact").released);
    MYE_EXPECT(!buffer.ConsumeTick().interact); // Catch-up never repeats the tap.
    input.NewFrame(); input.OnKey(KeyCode::F, true); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().interact);
    input.NewFrame(); input.OnKey(KeyCode::F, true); buffer.Capture(input, true);
    MYE_EXPECT(!buffer.ConsumeTick().interact); // OS repeat is not another press.
    buffer.Capture(input, false); MYE_EXPECT(!buffer.ConsumeTick().interact);
    input.NewFrame(); buffer.Capture(input, true);
    MYE_EXPECT(!buffer.ConsumeTick().interact); // Held key on resume does not fire.
    input.NewFrame(); input.OnKey(KeyCode::F, false); buffer.Capture(input, true); buffer.ConsumeTick();
    input.NewFrame(); input.OnKey(KeyCode::F, true); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().interact);
    input.NewFrame(); input.OnKey(KeyCode::Escape, true); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().exitGame);
    MYE_EXPECT(!buffer.ConsumeTick().exitGame);
}

MYE_TEST(InputActionsNormalizeDirectionsConsumeMouseAndHonorCapture) {
    runtime::GameInputBuffer buffer;
    MYE_EXPECT(buffer.Configure(runtime::DefaultGameInputMap()));
    InputState input;
    input.NewFrame(); input.OnKey(KeyCode::D, true); input.OnKey(KeyCode::W, true);
    input.OnMouseButton(MouseButton::Right, true); input.OnMouseMove({}, {3, 1}); input.OnWheel(1.5f);
    buffer.Capture(input, true);
    const auto first = buffer.ConsumeTick();
    MYE_EXPECT_NEAR(first.movement.x, std::sqrt(.5f), 1e-6f);
    MYE_EXPECT_NEAR(first.movement.y, std::sqrt(.5f), 1e-6f);
    MYE_EXPECT(first.cameraMouseX == 3 && first.cameraZoomSteps == 1.5f);
    const auto second = buffer.ConsumeTick();
    MYE_EXPECT(second.movement == first.movement && second.cameraMouseX == 0 && second.cameraZoomSteps == 0);
    input.NewFrame(); input.OnWheel(-.5f); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().cameraZoomSteps == -.5f);
    input.NewFrame(); input.OnKey(KeyCode::A, true); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().movement == Vec2{0,1}); // Opposing directions cancel.
    input.SetKeyboardSuppressed(true); input.SetMouseSuppressed(true);
    input.OnKey(KeyCode::E, true); input.OnMouseButton(MouseButton::Right, true); input.OnWheel(2);
    buffer.Capture(input, true);
    const auto captured = buffer.ConsumeTick();
    MYE_EXPECT(captured.movement == Vec2{} && !captured.interact && captured.cameraMouseX == 0 && captured.cameraZoomSteps == 0);
    input.SetKeyboardSuppressed(false); input.SetMouseSuppressed(false);
    input.NewFrame(); input.OnMouseButton(MouseButton::Left, true); input.OnMouseButton(MouseButton::Left, false);
    MYE_EXPECT(input.WasPressed(MouseButton::Left) && input.WasReleased(MouseButton::Left));
    const auto invalidKey = static_cast<KeyCode>(65535);
    input.OnKey(invalidKey, true); input.OnMouseButton(static_cast<MouseButton>(255), true);
    MYE_EXPECT(!input.IsDown(invalidKey) && !input.WasPressed(invalidKey) && !input.WasReleased(invalidKey));
    MYE_EXPECT(!input.IsDown(static_cast<MouseButton>(255)) && !input.WasPressed(static_cast<GamepadButton>(255)));
    input.NewFrame(); input.OnKey(KeyCode::E, true); input.OnMouseButton(MouseButton::Right, true);
    input.OnMouseMove({}, {4, 0}); input.OnWheel(2); buffer.Capture(input, true);
    // UI captures the next render frame before the pending fixed tick.
    input.SetKeyboardSuppressed(true); input.SetMouseSuppressed(true); buffer.Capture(input, true);
    const auto cancelled = buffer.ConsumeTick();
    MYE_EXPECT(!cancelled.interact && cancelled.cameraMouseX == 0 && cancelled.cameraZoomSteps == 0);
}

MYE_TEST(InputMapRejectsMalformedDataAndPreservesConfiguredActions) {
    const auto defaults = runtime::DefaultGameInputMap();
    const auto roundTrip = InputMap::FromJson(defaults.ToJson());
    MYE_EXPECT(roundTrip && roundTrip.Value() == defaults);
    MYE_EXPECT(runtime::LoadGameInputMap(nullptr).Value() == defaults);
    InputActions actions; MYE_EXPECT(actions.Configure(defaults));
    auto malformed = defaults;
    malformed.actions[0].bindings[0].code = 65540;
    MYE_EXPECT(!actions.Configure(malformed) && actions.Map() == defaults);
    malformed = defaults; malformed.actions[0].deadzone = std::numeric_limits<float>::quiet_NaN();
    MYE_EXPECT(!malformed.Validate());
    malformed = defaults; malformed.actions[0].deadzone = 1; MYE_EXPECT(!malformed.Validate());
    malformed = defaults; malformed.actions[0].bindings[0].pad = 1; MYE_EXPECT(!malformed.Validate());
    malformed = defaults; malformed.actions.push_back(malformed.actions[0]); MYE_EXPECT(!malformed.Validate());
    malformed = defaults; malformed.actions[0].bindings.push_back(malformed.actions[0].bindings[0]); MYE_EXPECT(!malformed.Validate());
    malformed = defaults; malformed.actions[0].name = "bad name"; MYE_EXPECT(!malformed.Validate());
    for (const auto text : {
        R"({"version":2,"actions":[]})", R"({"version":1,"actions":false})",
        R"({"version":1,"actions":[{"name":"test","deadzone":0.2,"bindings":[{"device":"key","code":999999999999,"direction":1,"pad":0}]}]})",
        R"({"version":1,"actions":[{"name":"test","deadzone":0.2,"bindings":[{"device":"unknown","code":4,"direction":1,"pad":0}]}]})",
        R"({"version":1,"actions":[{"name":"test","deadzone":0.2,"bindings":[{"device":"key","code":4.0,"direction":1,"pad":0}]}]})"}) {
        const auto parsed = json::Parse(text); MYE_EXPECT(parsed && !InputMap::FromJson(parsed.Value()));
    }
    auto padMap = defaults;
    padMap.actions[0].bindings = {{InputDevice::GamepadAxis, 0,-1,3}, {InputDevice::GamepadButton, 0,1,3}};
    MYE_EXPECT(padMap.Validate() && InputMap::FromJson(padMap.ToJson()).Value() == padMap);
    MYE_EXPECT(InputBindingLabel(padMap.actions[0].bindings[0]) == "Pad 4 Left X -");
}
