// mye/core/Input.cpp — InputState 폴링 (키보드/마우스 + XInput 게임패드)
#include "mye/core/Input.h"

#include <Windows.h>
#include <Xinput.h>
#include <cmath>

namespace mye {

static size_t Idx(KeyCode k) { return static_cast<size_t>(k); }
static size_t Idx(MouseButton b) { return static_cast<size_t>(b); }
static size_t Idx(GamepadButton b) { return static_cast<size_t>(b); }

bool InputState::IsDown(KeyCode key) const { return m_current.keys[Idx(key)]; }
bool InputState::WasPressed(KeyCode key) const {
    return m_current.keys[Idx(key)] && !m_previous.keys[Idx(key)];
}
bool InputState::WasReleased(KeyCode key) const {
    return !m_current.keys[Idx(key)] && m_previous.keys[Idx(key)];
}

bool InputState::IsDown(MouseButton btn) const { return m_current.mouseButtons[Idx(btn)]; }
bool InputState::WasPressed(MouseButton btn) const {
    return m_current.mouseButtons[Idx(btn)] && !m_previous.mouseButtons[Idx(btn)];
}
bool InputState::WasReleased(MouseButton btn) const {
    return !m_current.mouseButtons[Idx(btn)] && m_previous.mouseButtons[Idx(btn)];
}

Vec2i InputState::MousePosition() const { return m_mousePos; }
Vec2  InputState::MouseDelta() const { return m_mouseDelta; }
float InputState::WheelDelta() const { return m_wheelDelta; }

void InputState::NewFrame() {
    m_previous = m_current;
    m_mouseDelta = {};
    m_wheelDelta = 0.0f;
}

// ---- 게임패드(XInput) ----
namespace {
// 방사형 데드존: 크기 dz 미만은 0, 그 이상은 방향 유지하며 0..1로 재정규화.
Vec2 ApplyDeadzone(short x, short y, short dz) {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const float mag = std::sqrt(fx * fx + fy * fy);
    if (mag <= static_cast<float>(dz)) return Vec2{0.0f, 0.0f};
    float norm = (mag - static_cast<float>(dz)) / (32767.0f - static_cast<float>(dz));
    if (norm > 1.0f) norm = 1.0f;
    const float inv = norm / mag;
    return Vec2{fx * inv, fy * inv};
}
bool InRange(int pad) { return pad >= 0 && pad < kMaxGamepads; }
} // namespace

void InputState::PollGamepads() {
    for (int i = 0; i < kMaxGamepads; ++i) m_padsPrev[i] = m_pads[i];
    ++m_gamepadPoll;

    for (int i = 0; i < kMaxGamepads; ++i) {
        // 미연결 슬롯을 매 프레임 XInputGetState 하면 지연이 크다(알려진 스톨). 연결됐던 슬롯은
        // 매 프레임 폴링하고, 미연결 슬롯은 슬롯별로 엇갈려 주기적으로만(≈90프레임) 재검색한다.
        if (!m_pads[i].connected && (m_gamepadPoll % 90u) != static_cast<uint32_t>(i)) {
            m_pads[i] = GamepadSnapshot{};
            continue;
        }

        XINPUT_STATE st{};
        GamepadSnapshot s{};
        if (XInputGetState(static_cast<DWORD>(i), &st) == ERROR_SUCCESS) {
            s.connected = true;
            const XINPUT_GAMEPAD& g = st.Gamepad;
            const WORD w = g.wButtons;
            auto set = [&](GamepadButton b, WORD mask) { s.buttons[Idx(b)] = (w & mask) != 0; };
            set(GamepadButton::A, XINPUT_GAMEPAD_A);
            set(GamepadButton::B, XINPUT_GAMEPAD_B);
            set(GamepadButton::X, XINPUT_GAMEPAD_X);
            set(GamepadButton::Y, XINPUT_GAMEPAD_Y);
            set(GamepadButton::DPadUp, XINPUT_GAMEPAD_DPAD_UP);
            set(GamepadButton::DPadDown, XINPUT_GAMEPAD_DPAD_DOWN);
            set(GamepadButton::DPadLeft, XINPUT_GAMEPAD_DPAD_LEFT);
            set(GamepadButton::DPadRight, XINPUT_GAMEPAD_DPAD_RIGHT);
            set(GamepadButton::LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER);
            set(GamepadButton::RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER);
            set(GamepadButton::LeftThumb, XINPUT_GAMEPAD_LEFT_THUMB);
            set(GamepadButton::RightThumb, XINPUT_GAMEPAD_RIGHT_THUMB);
            set(GamepadButton::Start, XINPUT_GAMEPAD_START);
            set(GamepadButton::Back, XINPUT_GAMEPAD_BACK);

            s.leftStick  = ApplyDeadzone(g.sThumbLX, g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            s.rightStick = ApplyDeadzone(g.sThumbRX, g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            s.leftTrigger  = g.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
                             ? static_cast<float>(g.bLeftTrigger) / 255.0f : 0.0f;
            s.rightTrigger = g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD
                             ? static_cast<float>(g.bRightTrigger) / 255.0f : 0.0f;
        }
        m_pads[i] = s;
    }
}

bool InputState::IsGamepadConnected(int pad) const {
    return InRange(pad) && m_pads[pad].connected;
}
bool InputState::IsDown(GamepadButton btn, int pad) const {
    return InRange(pad) && m_pads[pad].buttons[Idx(btn)];
}
bool InputState::WasPressed(GamepadButton btn, int pad) const {
    return InRange(pad) && m_pads[pad].buttons[Idx(btn)] && !m_padsPrev[pad].buttons[Idx(btn)];
}
bool InputState::WasReleased(GamepadButton btn, int pad) const {
    return InRange(pad) && !m_pads[pad].buttons[Idx(btn)] && m_padsPrev[pad].buttons[Idx(btn)];
}
Vec2  InputState::LeftStick(int pad) const  { return InRange(pad) ? m_pads[pad].leftStick  : Vec2{}; }
Vec2  InputState::RightStick(int pad) const { return InRange(pad) ? m_pads[pad].rightStick : Vec2{}; }
float InputState::LeftTrigger(int pad) const  { return InRange(pad) ? m_pads[pad].leftTrigger  : 0.0f; }
float InputState::RightTrigger(int pad) const { return InRange(pad) ? m_pads[pad].rightTrigger : 0.0f; }

void InputState::OnKey(KeyCode key, bool pressed) {
    if (m_keyboardSuppressed) return;   // UI 캡처 중 — 게임 키 상태 갱신 억제
    m_current.keys[Idx(key)] = pressed;
}
void InputState::OnMouseButton(MouseButton btn, bool pressed) {
    if (m_mouseSuppressed) return;      // UI 캡처 중 — 게임 마우스 버튼 갱신 억제
    m_current.mouseButtons[Idx(btn)] = pressed;
}
void InputState::OnMouseMove(Vec2i position, Vec2 rawDelta) {
    m_mousePos = position;              // 위치는 UI 히트테스트용이라 항상 추적
    if (m_mouseSuppressed) return;      // 델타는 게임 카메라 등 소비 — 캡처 중 억제
    m_mouseDelta += rawDelta;
}
void InputState::OnWheel(float deltaY) {
    if (m_mouseSuppressed) return;
    m_wheelDelta += deltaY;
}

void InputState::SetKeyboardSuppressed(bool suppressed) {
    // 억제 진입 시 눌린 키를 released로 강제해 stuck 방지(엣지도 이번 프레임에 관측 가능).
    if (suppressed && !m_keyboardSuppressed) {
        for (bool& k : m_current.keys) k = false;
    }
    m_keyboardSuppressed = suppressed;
}
void InputState::SetMouseSuppressed(bool suppressed) {
    if (suppressed && !m_mouseSuppressed) {
        for (bool& b : m_current.mouseButtons) b = false;
        m_mouseDelta = {};
        m_wheelDelta = 0.0f;
    }
    m_mouseSuppressed = suppressed;
}

} // namespace mye
