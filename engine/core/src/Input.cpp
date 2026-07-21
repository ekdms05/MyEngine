// mye/core/Input.cpp — InputState 폴링 스텁 (M1-B에서 구현)
#include "mye/core/Input.h"

namespace mye {

static size_t Idx(KeyCode k) { return static_cast<size_t>(k); }
static size_t Idx(MouseButton b) { return static_cast<size_t>(b); }

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
