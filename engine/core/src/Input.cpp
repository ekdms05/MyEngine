// mye/core/Input.cpp — InputState 폴링 (키보드/마우스 + XInput 게임패드)
#include "mye/core/Input.h"

#include <Windows.h>
#include <Xinput.h>
#include <cmath>

namespace mye {

Expected<void, Error> ValidateTextInput(std::string_view text) {
    if (text.size() > 4096) return Error{"Text input exceeds 4096 UTF-8 bytes", 1};
    for (const unsigned char c : text)
        if (c < 32 || c == 127) return Error{"Text input requires one line without control characters", 1};
    if (!text.empty() && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0)) return Error{"Text input contains invalid UTF-8", 1};
    return {};
}
void InputState::SetTextInputFocus(uint64_t id) {
    if (id == m_textFocusId && id != 0) return;
    m_textFocusId = id; m_textComposing = false;
    m_textEdits.clear(); m_textBytes = 0; m_textError.reset();
}
Expected<void, Error> InputState::OnTextEdit(TextEdit edit) {
    if (!m_textFocusId || m_keyboardSuppressed) return {};
    auto valid = ValidateTextInput(edit.text); if (!valid) return valid.GetError();
    if (edit.cursorBytes > edit.text.size() || (edit.cursorBytes < edit.text.size() &&
        (static_cast<unsigned char>(edit.text[edit.cursorBytes]) & 0xc0) == 0x80))
        return Error{"Text composition cursor must be a UTF-8 boundary", 1};
    if (m_textEdits.size() >= 256 || m_textBytes + edit.text.size() > 16384)
        return Error{"Text input frame exceeds 256 edits or 16384 bytes", 1};
    if (edit.kind == TextEdit::Kind::Composition) m_textComposing = edit.composing;
    m_textBytes += edit.text.size(); m_textEdits.push_back(std::move(edit));
    return {};
}

static size_t Idx(KeyCode k) { return static_cast<size_t>(k); }
static size_t Idx(MouseButton b) { return static_cast<size_t>(b); }
static size_t Idx(GamepadButton b) { return static_cast<size_t>(b); }

bool InputState::IsDown(KeyCode key) const { return Idx(key) < Idx(KeyCode::Count) && m_current.keys[Idx(key)]; }
bool InputState::WasPressed(KeyCode key) const {
    return Idx(key) < Idx(KeyCode::Count) && m_pressed.keys[Idx(key)];
}
bool InputState::WasReleased(KeyCode key) const {
    return Idx(key) < Idx(KeyCode::Count) && m_released.keys[Idx(key)];
}

bool InputState::IsDown(MouseButton btn) const { return Idx(btn) < Idx(MouseButton::Count) && m_current.mouseButtons[Idx(btn)]; }
bool InputState::WasPressed(MouseButton btn) const {
    return Idx(btn) < Idx(MouseButton::Count) && m_pressed.mouseButtons[Idx(btn)];
}
bool InputState::WasReleased(MouseButton btn) const {
    return Idx(btn) < Idx(MouseButton::Count) && m_released.mouseButtons[Idx(btn)];
}

Vec2i InputState::MousePosition() const { return m_mousePos; }
Vec2  InputState::MouseDelta() const { return m_mouseDelta; }
float InputState::WheelDelta() const { return m_wheelDelta; }

void InputState::NewFrame() {
    m_pressed = {}; m_released = {};
    m_mouseDelta = {};
    m_wheelDelta = 0.0f;
    m_textEdits.clear(); m_textBytes = 0;
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
    ++m_gamepadPoll;

    for (int i = 0; i < kMaxGamepads; ++i) {
        // 미연결 슬롯을 매 프레임 XInputGetState 하면 지연이 크다(알려진 스톨). 연결됐던 슬롯은
        // 매 프레임 폴링하고, 미연결 슬롯은 슬롯별로 엇갈려 주기적으로만(≈90프레임) 재검색한다.
        if (!m_pads[i].connected && (m_gamepadPoll % 90u) != static_cast<uint32_t>(i)) {
            UpdateGamepad(i, {});
            continue;
        }

        XINPUT_STATE st{};
        GamepadSample s{};
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

            s.leftX = g.sThumbLX; s.leftY = g.sThumbLY;
            s.rightX = g.sThumbRX; s.rightY = g.sThumbRY;
            s.leftTrigger = g.bLeftTrigger; s.rightTrigger = g.bRightTrigger;
        }
        UpdateGamepad(i, s);
    }
}

void InputState::UpdateGamepad(int pad, GamepadSample sample) {
    if (!InRange(pad)) return;
    if (!sample.connected) sample = {};
    m_padsPrev[pad] = m_pads[pad];
    m_pads[pad] = sample;
}

bool InputState::IsGamepadConnected(int pad) const {
    return InRange(pad) && m_pads[pad].connected;
}
bool InputState::IsDown(GamepadButton btn, int pad) const {
    return InRange(pad) && Idx(btn) < Idx(GamepadButton::Count) && m_pads[pad].buttons[Idx(btn)];
}
bool InputState::WasPressed(GamepadButton btn, int pad) const {
    return InRange(pad) && Idx(btn) < Idx(GamepadButton::Count) && m_pads[pad].buttons[Idx(btn)] && !m_padsPrev[pad].buttons[Idx(btn)];
}
bool InputState::WasReleased(GamepadButton btn, int pad) const {
    return InRange(pad) && Idx(btn) < Idx(GamepadButton::Count) && !m_pads[pad].buttons[Idx(btn)] && m_padsPrev[pad].buttons[Idx(btn)];
}
Vec2 InputState::LeftStick(int pad) const {
    return InRange(pad) ? ApplyDeadzone(m_pads[pad].leftX, m_pads[pad].leftY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) : Vec2{};
}
Vec2 InputState::RightStick(int pad) const {
    return InRange(pad) ? ApplyDeadzone(m_pads[pad].rightX, m_pads[pad].rightY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) : Vec2{};
}
float InputState::LeftTrigger(int pad) const {
    return InRange(pad) && m_pads[pad].leftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? m_pads[pad].leftTrigger / 255.0f : 0.0f;
}
float InputState::RightTrigger(int pad) const {
    return InRange(pad) && m_pads[pad].rightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? m_pads[pad].rightTrigger / 255.0f : 0.0f;
}
float InputState::RawGamepadAxis(int axis, int pad) const {
    if (!IsGamepadConnected(pad)) return 0;
    const auto& sample = m_pads[pad];
    if (axis == 4) return sample.leftTrigger / 255.0f;
    if (axis == 5) return sample.rightTrigger / 255.0f;
    int16_t value = 0;
    switch (axis) {
    case 0: value = sample.leftX; break; case 1: value = sample.leftY; break;
    case 2: value = sample.rightX; break; case 3: value = sample.rightY; break;
    default: return 0;
    }
    return value / (value < 0 ? 32768.0f : 32767.0f);
}

void InputState::OnKey(KeyCode key, bool pressed) {
    if (m_keyboardSuppressed || Idx(key) >= Idx(KeyCode::Count)) return;
    if (pressed && m_textFocusId && !m_textComposing &&
        (key == KeyCode::Backspace || key == KeyCode::Delete || key == KeyCode::Left || key == KeyCode::Right ||
         key == KeyCode::Home || key == KeyCode::End || (key == KeyCode::Enter && !m_current.keys[Idx(key)]))) {
        TextEdit edit; edit.kind = TextEdit::Kind::Key; edit.key = key;
        auto accepted = OnTextEdit(std::move(edit));
        if (!accepted) ReportTextInputError(accepted.GetError());
    }
    if (pressed != m_current.keys[Idx(key)]) {
        if (pressed) m_pressed.keys[Idx(key)] = true;
        else m_released.keys[Idx(key)] = true;
    }
    m_current.keys[Idx(key)] = pressed;
}
void InputState::OnMouseButton(MouseButton btn, bool pressed) {
    if (m_mouseSuppressed || Idx(btn) >= Idx(MouseButton::Count)) return;
    if (pressed != m_current.mouseButtons[Idx(btn)]) {
        if (pressed) m_pressed.mouseButtons[Idx(btn)] = true;
        else m_released.mouseButtons[Idx(btn)] = true;
    }
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
    if (suppressed) SetTextInputFocus(0);
    // 억제 진입 시 눌린 키를 released로 강제해 stuck 방지(엣지도 이번 프레임에 관측 가능).
    if (suppressed && !m_keyboardSuppressed) {
        for (size_t i = 0; i < Idx(KeyCode::Count); ++i) {
            m_released.keys[i] |= m_current.keys[i];
            m_current.keys[i] = m_pressed.keys[i] = false;
        }
    }
    m_keyboardSuppressed = suppressed;
}
void InputState::SetMouseSuppressed(bool suppressed) {
    if (suppressed && !m_mouseSuppressed) {
        for (size_t i = 0; i < Idx(MouseButton::Count); ++i) {
            m_released.mouseButtons[i] |= m_current.mouseButtons[i];
            m_current.mouseButtons[i] = m_pressed.mouseButtons[i] = false;
        }
        m_mouseDelta = {};
        m_wheelDelta = 0.0f;
    }
    m_mouseSuppressed = suppressed;
}

} // namespace mye
