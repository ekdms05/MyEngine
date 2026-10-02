#pragma once

#include "mye/core/Input.h"
#include "mye/core/Json.h"
#include <string>
#include <vector>

namespace mye {
enum class InputDevice : uint8_t { Key, MouseButton, GamepadButton, GamepadAxis, Wheel };
// Gamepad axes: left X/Y, right X/Y, left/right trigger. Y is positive up.
struct InputBinding {
    InputDevice device = InputDevice::Key;
    int code = static_cast<int>(KeyCode::Space);
    int direction = 1;
    int pad = 0;
    bool operator==(const InputBinding&) const = default;
};
struct InputAction {
    std::string name;
    float deadzone = .2f;
    std::vector<InputBinding> bindings;
    bool operator==(const InputAction&) const = default;
};
struct InputMap {
    std::vector<InputAction> actions;
    Expected<void, Error> Validate() const;
    static Expected<InputMap, Error> FromJson(const json::Value& value);
    // Serialize a validated map; writers validate before changing stored data.
    json::Value ToJson() const;
    bool operator==(const InputMap&) const = default;
};

std::string_view InputCodeName(InputDevice device, int code);
std::string InputBindingLabel(const InputBinding& binding);

struct InputActionState {
    float rawStrength = 0, strength = 0, pulse = 0;
    bool pressed = false, released = false;
};
// Capture once after pumping messages; consume once per fixed tick. Disabled input
// discards pending edges/deltas instead of replaying them on focus/pause recovery.
class InputActions {
public:
    Expected<void, Error> Configure(InputMap map);
    void Capture(const InputState& input, bool enabled = true);
    void ConsumeTick();
    void Clear();
    InputActionState Action(std::string_view name) const;
    bool Held(std::string_view name) const;
    Vec2 Vector(std::string_view left, std::string_view right,
                std::string_view down, std::string_view up) const;
    const InputMap& Map() const { return m_map; }
private:
    InputMap m_map;
    std::vector<InputActionState> m_pending, m_tick;
    bool m_wasEnabled = true;
    bool m_keyboardSuppressed = false, m_mouseSuppressed = false;
};
} // namespace mye
