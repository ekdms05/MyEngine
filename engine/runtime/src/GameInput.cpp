#include "mye/runtime/GameInput.h"
#include <algorithm>
#include <utility>

namespace mye::runtime {
namespace {
// Persisted keys are shared by defaults, help and the fixed-tick consumer.
constexpr auto kMoveLeft = "move_left";
constexpr auto kMoveRight = "move_right";
constexpr auto kMoveDown = "move_down";
constexpr auto kMoveUp = "move_up";
constexpr auto kInteract = "interact";
constexpr auto kJump = "jump";
constexpr auto kCameraLeft = "camera_left";
constexpr auto kCameraRight = "camera_right";
constexpr auto kCameraDrag = "camera_drag";
constexpr auto kZoomIn = "zoom_in";
constexpr auto kZoomOut = "zoom_out";
constexpr auto kExitGame = "exit_game";
}
InputMap DefaultGameInputMap() {
    const auto key = [](KeyCode code) { return InputBinding{InputDevice::Key, static_cast<int>(code)}; };
    const auto button = [](GamepadButton code) { return InputBinding{InputDevice::GamepadButton, static_cast<int>(code)}; };
    const auto axis = [](int code, int direction) { return InputBinding{InputDevice::GamepadAxis, code, direction}; };
    return {{{kMoveLeft, .2f, {key(KeyCode::A), key(KeyCode::Left), button(GamepadButton::DPadLeft), axis(0,-1)}},
             {kMoveRight, .2f, {key(KeyCode::D), key(KeyCode::Right), button(GamepadButton::DPadRight), axis(0,1)}},
             {kMoveDown, .2f, {key(KeyCode::S), key(KeyCode::Down), button(GamepadButton::DPadDown), axis(1,-1)}},
             {kMoveUp, .2f, {key(KeyCode::W), key(KeyCode::Up), button(GamepadButton::DPadUp), axis(1,1)}},
             {kInteract, .2f, {key(KeyCode::E), button(GamepadButton::X)}},
             {kJump, .2f, {key(KeyCode::Space), button(GamepadButton::A)}},
             {kCameraLeft, .2f, {key(KeyCode::Q), axis(2,-1)}},
             {kCameraRight, .2f, {key(KeyCode::R), axis(2,1)}},
             {kCameraDrag, .2f, {{InputDevice::MouseButton, static_cast<int>(MouseButton::Right)}}},
             {kZoomIn, .2f, {{InputDevice::Wheel, 0,1}}},
             {kZoomOut, .2f, {{InputDevice::Wheel, 0,-1}}},
             {kExitGame, .2f, {key(KeyCode::Escape)}}}};
}
Expected<InputMap, Error> LoadGameInputMap(const json::Value* value) {
    return value ? InputMap::FromJson(*value) : Expected<InputMap, Error>{DefaultGameInputMap()};
}
std::string_view GameActionDescription(std::string_view name) {
    if (name == kMoveLeft) return "왼쪽 이동 (-X)";
    if (name == kMoveRight) return "오른쪽 이동 (+X)";
    if (name == kMoveDown) return "아래 이동 (-Y)";
    if (name == kMoveUp) return "위 이동 (+Y)";
    if (name == kInteract) return "상호작용 — 첫 누름에 한 번 실행";
    if (name == kJump) return "점프 — 3D 조작에서 사용";
    if (name == kCameraLeft) return "3D 카메라 왼쪽 회전";
    if (name == kCameraRight) return "3D 카메라 오른쪽 회전";
    if (name == kCameraDrag) return "3D 카메라 마우스 드래그";
    if (name == kZoomIn) return "2D 카메라 확대 — 휠 또는 첫 누름";
    if (name == kZoomOut) return "2D 카메라 축소 — 휠 또는 첫 누름";
    if (name == kExitGame) return "실행 종료 — Play는 편집으로 돌아감";
    return "사용자 액션 — 스크립트 연결 준비 중";
}
Expected<void, Error> GameInputBuffer::Configure(InputMap map) {
    if (auto configured = m_actions.Configure(std::move(map)); !configured) return configured.GetError();
    m_mouseX = 0;
    return {};
}
void GameInputBuffer::Clear() { m_actions.Clear(); m_mouseX = 0; }
void GameInputBuffer::Capture(const InputState& input, bool enabled) {
    m_actions.Capture(input, enabled);
    if (input.IsMouseSuppressed()) m_mouseX = 0;
    if (!enabled) { m_mouseX = 0; return; }
    // Drag is a held action, while raw mouse motion is a per-frame delta.
    if (m_actions.Held(kCameraDrag)) m_mouseX += input.MouseDelta().x;
}
GameInput GameInputBuffer::ConsumeTick() {
    m_actions.ConsumeTick();
    GameInput input;
    input.movement = m_actions.Vector(kMoveLeft, kMoveRight, kMoveDown, kMoveUp);
    input.interact = m_actions.Action(kInteract).pressed;
    input.jump = m_actions.Action(kJump).pressed;
    input.cameraAxis = m_actions.Action(kCameraRight).strength - m_actions.Action(kCameraLeft).strength;
    input.cameraMouseX = std::exchange(m_mouseX, 0.0f);
    input.cameraZoomSteps = std::clamp(m_actions.Action(kZoomIn).pulse - m_actions.Action(kZoomOut).pulse, -16.0f, 16.0f);
    input.exitGame = m_actions.Action(kExitGame).pressed;
    return input;
}
} // namespace mye::runtime
