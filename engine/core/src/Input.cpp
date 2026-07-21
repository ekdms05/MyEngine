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

void InputState::OnKey(KeyCode key, bool pressed) { m_current.keys[Idx(key)] = pressed; }
void InputState::OnMouseButton(MouseButton btn, bool pressed) {
    m_current.mouseButtons[Idx(btn)] = pressed;
}
void InputState::OnMouseMove(Vec2i position, Vec2 rawDelta) {
    m_mousePos = position;
    m_mouseDelta += rawDelta;
}
void InputState::OnWheel(float deltaY) { m_wheelDelta += deltaY; }

} // namespace mye
