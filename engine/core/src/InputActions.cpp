#include "mye/core/InputActions.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace mye {
namespace {
constexpr std::array<std::string_view, 5> kDevices{"key", "mouse", "pad_button", "pad_axis", "wheel"};
bool ValidBinding(const InputBinding& binding) {
    if (InputCodeName(binding.device, binding.code).empty()) return false;
    const bool directed = binding.device == InputDevice::GamepadAxis || binding.device == InputDevice::Wheel;
    const bool gamepad = binding.device == InputDevice::GamepadAxis || binding.device == InputDevice::GamepadButton;
    return (directed ? binding.direction == -1 || binding.direction == 1 : binding.direction == 1) &&
           (gamepad ? binding.pad >= 0 && binding.pad < kMaxGamepads : binding.pad == 0);
}
struct Sample { float value = 0; bool pressed = false, released = false; bool wheel = false; };
Sample ReadBinding(const InputState& input, const InputBinding& binding) {
    switch (binding.device) {
    case InputDevice::Key: {
        const auto code = static_cast<KeyCode>(binding.code);
        return {float(input.IsDown(code)), input.WasPressed(code), input.WasReleased(code)};
    }
    case InputDevice::MouseButton: {
        const auto code = static_cast<MouseButton>(binding.code);
        return {float(input.IsDown(code)), input.WasPressed(code), input.WasReleased(code)};
    }
    case InputDevice::GamepadButton: {
        if (input.IsKeyboardSuppressed()) return {};
        const auto code = static_cast<GamepadButton>(binding.code);
        return {float(input.IsDown(code, binding.pad)), input.WasPressed(code, binding.pad), input.WasReleased(code, binding.pad)};
    }
    case InputDevice::GamepadAxis: {
        if (input.IsKeyboardSuppressed()) return {};
        return {std::max(0.0f, input.RawGamepadAxis(binding.code, binding.pad) * binding.direction)};
    }
    case InputDevice::Wheel: return {std::max(0.0f, input.WheelDelta() * binding.direction), false, false, true};
    }
    return {};
}
}

std::string_view InputCodeName(InputDevice device, int code) {
    static constexpr std::array<std::string_view, 26> letters{
        "A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};
    static constexpr std::array<std::string_view, 10> digits{"1","2","3","4","5","6","7","8","9","0"};
    static constexpr std::array<std::string_view, 12> functions{"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    static constexpr std::array<std::string_view, 8> modifiers{"Left Ctrl","Left Shift","Left Alt","Left Win","Right Ctrl","Right Shift","Right Alt","Right Win"};
    static constexpr std::array<std::string_view, 5> mouse{"Left","Right","Middle","X1","X2"};
    static constexpr std::array<std::string_view, 14> buttons{"A","B","X","Y","D-pad Up","D-pad Down","D-pad Left","D-pad Right","LB","RB","Left Thumb","Right Thumb","Start","Back"};
    static constexpr std::array<std::string_view, 6> axes{"Left X","Left Y","Right X","Right Y","Left Trigger","Right Trigger"};
    if (device == InputDevice::Key) {
        if (code < 0 || code >= static_cast<int>(KeyCode::Count)) return {};
        if (code >= 4 && code <= 29) return letters[code - 4];
        if (code >= 30 && code <= 39) return digits[code - 30];
        if (code >= 58 && code <= 69) return functions[code - 58];
        if (code >= 224 && code <= 231) return modifiers[code - 224];
        switch (static_cast<KeyCode>(code)) {
        case KeyCode::Enter: return "Enter"; case KeyCode::Escape: return "Escape";
        case KeyCode::Backspace: return "Backspace"; case KeyCode::Tab: return "Tab"; case KeyCode::Space: return "Space";
        case KeyCode::Home: return "Home"; case KeyCode::End: return "End"; case KeyCode::Delete: return "Delete";
        case KeyCode::Minus: return "-"; case KeyCode::Equals: return "=";
        case KeyCode::LeftBracket: return "["; case KeyCode::RightBracket: return "]"; case KeyCode::Backslash: return "\\";
        case KeyCode::Semicolon: return ";"; case KeyCode::Apostrophe: return "'"; case KeyCode::Grave: return "`";
        case KeyCode::Comma: return ","; case KeyCode::Period: return "."; case KeyCode::Slash: return "/";
        case KeyCode::CapsLock: return "Caps Lock"; case KeyCode::Right: return "Right"; case KeyCode::Left: return "Left";
        case KeyCode::Down: return "Down"; case KeyCode::Up: return "Up"; default: return {};
        }
    }
    if (code < 0) return {};
    const auto index = static_cast<size_t>(code);
    if (device == InputDevice::MouseButton && index < mouse.size()) return mouse[index];
    if (device == InputDevice::GamepadButton && index < buttons.size()) return buttons[index];
    if (device == InputDevice::GamepadAxis && index < axes.size()) return axes[index];
    if (device == InputDevice::Wheel && code == 0) return "Wheel";
    return {};
}
std::string InputBindingLabel(const InputBinding& binding) {
    if (!ValidBinding(binding)) return "Invalid binding";
    std::string label;
    if (binding.device == InputDevice::MouseButton) label = "Mouse ";
    if (binding.device == InputDevice::GamepadButton || binding.device == InputDevice::GamepadAxis)
        label = "Pad " + std::to_string(binding.pad + 1) + " ";
    label += InputCodeName(binding.device, binding.code);
    if (binding.device == InputDevice::GamepadAxis || binding.device == InputDevice::Wheel)
        label += binding.direction > 0 ? " +" : " -";
    return label;
}

Expected<void, Error> InputMap::Validate() const {
    if (actions.size() > 64) return Error{"Input map supports at most 64 actions", 1};
    for (size_t i = 0; i < actions.size(); ++i) {
        const auto& action = actions[i];
        if (action.name.empty() || action.name.size() > 64 ||
            action.name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
            (action.name.front() >= '0' && action.name.front() <= '9'))
            return Error{"Input action names require 1..64 letters, digits or underscores and cannot start with a digit", 1};
        if (!std::isfinite(action.deadzone) || action.deadzone < 0 || action.deadzone >= 1 || action.bindings.size() > 16)
            return Error{"Input action " + action.name + " requires deadzone in [0,1) and at most 16 bindings", 1};
        for (size_t j = 0; j < i; ++j) if (actions[j].name == action.name)
            return Error{"Duplicate input action: " + action.name, 1};
        for (size_t j = 0; j < action.bindings.size(); ++j) {
            if (!ValidBinding(action.bindings[j])) return Error{"Invalid input binding in " + action.name, 1};
            for (size_t k = 0; k < j; ++k) if (action.bindings[j] == action.bindings[k])
                return Error{"Duplicate input binding in " + action.name, 1};
        }
    }
    return {};
}
Expected<InputMap, Error> InputMap::FromJson(const json::Value& value) {
    const auto* version = value.Find("version"), *list = value.Find("actions");
    if (!value.IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
        !list || !list->IsArray() || list->AsArray().size() > 64)
        return Error{"Input map requires version 1 and up to 64 actions", 1};
    InputMap map;
    for (const auto& item : list->AsArray()) {
        const auto* name = item.Find("name"), *deadzone = item.Find("deadzone"), *bindings = item.Find("bindings");
        if (!item.IsObject() || !name || !name->IsString() || !deadzone || !deadzone->IsNumber() ||
            !std::isfinite(deadzone->AsDouble()) || deadzone->AsDouble() < 0 || deadzone->AsDouble() >= 1 ||
            !bindings || !bindings->IsArray() || bindings->AsArray().size() > 16)
            return Error{"Invalid input action fields", 1};
        InputAction action{std::string(name->AsString()), static_cast<float>(deadzone->AsDouble()), {}};
        for (const auto& field : bindings->AsArray()) {
            const auto* device = field.Find("device"), *code = field.Find("code"), *direction = field.Find("direction"), *pad = field.Find("pad");
            if (!field.IsObject() || !device || !device->IsString() || !code || !code->IsInteger() || code->AsInt() < 0 || code->AsInt() > 255 ||
                !direction || !direction->IsInteger() || (direction->AsInt() != -1 && direction->AsInt() != 1) ||
                !pad || !pad->IsInteger() || pad->AsInt() < 0 || pad->AsInt() >= kMaxGamepads)
                return Error{"Invalid input binding fields", 1};
            const auto kind = std::find(kDevices.begin(), kDevices.end(), device->AsString());
            if (kind == kDevices.end()) return Error{"Unknown input binding device", 1};
            action.bindings.push_back({static_cast<InputDevice>(kind - kDevices.begin()), static_cast<int>(code->AsInt()),
                                       static_cast<int>(direction->AsInt()), static_cast<int>(pad->AsInt())});
        }
        map.actions.push_back(std::move(action));
    }
    if (auto valid = map.Validate(); !valid) return valid.GetError();
    return map;
}
json::Value InputMap::ToJson() const {
    json::Value::Array actionsJson;
    for (const auto& action : actions) {
        json::Value::Array bindings;
        for (const auto& binding : action.bindings) {
            bindings.emplace_back(json::Value::Object{{"device", json::Value(std::string(kDevices[static_cast<size_t>(binding.device)]))},
                {"code", json::Value(int64_t{binding.code})}, {"direction", json::Value(int64_t{binding.direction})}, {"pad", json::Value(int64_t{binding.pad})}});
        }
        actionsJson.emplace_back(json::Value::Object{{"name", json::Value(action.name)}, {"deadzone", json::Value(double{action.deadzone})},
                                                     {"bindings", json::Value(std::move(bindings))}});
    }
    return json::Value(json::Value::Object{{"version", json::Value(int64_t{1})}, {"actions", json::Value(std::move(actionsJson))}});
}

Expected<void, Error> InputActions::Configure(InputMap map) {
    if (auto valid = map.Validate(); !valid) return valid.GetError();
    m_map = std::move(map);
    m_pending.assign(m_map.actions.size(), {});
    m_tick.assign(m_map.actions.size(), {});
    m_wasEnabled = true;
    m_keyboardSuppressed = m_mouseSuppressed = false;
    return {};
}
void InputActions::Clear() {
    std::fill(m_pending.begin(), m_pending.end(), InputActionState{});
    std::fill(m_tick.begin(), m_tick.end(), InputActionState{});
    m_wasEnabled = false;
}
void InputActions::Capture(const InputState& input, bool enabled) {
    const bool resumingGamepad = m_keyboardSuppressed && !input.IsKeyboardSuppressed();
    if ((input.IsKeyboardSuppressed() && !m_keyboardSuppressed) || (input.IsMouseSuppressed() && !m_mouseSuppressed)) Clear();
    m_keyboardSuppressed = input.IsKeyboardSuppressed();
    m_mouseSuppressed = input.IsMouseSuppressed();
    if (!enabled) { Clear(); return; }
    for (size_t i = 0; i < m_map.actions.size(); ++i) {
        const auto& action = m_map.actions[i];
        auto& state = m_pending[i];
        float raw = 0, wheel = 0, heldRaw = 0, pressRaw = 0;
        bool pressed = false, released = false;
        for (const auto& binding : action.bindings) {
            const auto sample = ReadBinding(input, binding);
            raw = std::max(raw, std::min(1.0f, sample.value));
            // Held pads resume movement without reactivating one-shot actions.
            if (!resumingGamepad || (binding.device != InputDevice::GamepadAxis && binding.device != InputDevice::GamepadButton))
                pressRaw = std::max(pressRaw, sample.value);
            if (sample.wheel) wheel = std::max(wheel, sample.value);
            else { heldRaw = std::max(heldRaw, sample.value); pressed |= sample.pressed; released |= sample.released; }
        }
        const bool wasDown = state.rawStrength > action.deadzone, down = raw > action.deadzone;
        const bool firstPress = !wasDown && (pressRaw > action.deadzone || pressed) && (m_wasEnabled || pressed);
        state.pressed |= firstPress;
        state.released |= (wasDown && !down) || (!wasDown && !down && pressed && released);
        state.rawStrength = raw;
        state.strength = std::clamp((raw - action.deadzone) / (1 - action.deadzone), 0.0f, 1.0f);
        state.pulse += std::max(wheel, float(firstPress && (pressed || heldRaw > action.deadzone)));
    }
    m_wasEnabled = true;
}
void InputActions::ConsumeTick() {
    std::copy(m_pending.begin(), m_pending.end(), m_tick.begin());
    for (auto& state : m_pending) { state.pressed = state.released = false; state.pulse = 0; }
}
InputActionState InputActions::Action(std::string_view name) const {
    // ponytail: at most 64 authored actions; index only if profiling shows a bottleneck.
    for (size_t i = 0; i < m_map.actions.size(); ++i) if (m_map.actions[i].name == name) return m_tick[i];
    return {};
}
Vec2 InputActions::Vector(std::string_view left, std::string_view right, std::string_view down, std::string_view up) const {
    const Vec2 axes{Action(right).rawStrength - Action(left).rawStrength, Action(up).rawStrength - Action(down).rawStrength};
    float deadzone = 0;
    for (const auto name : {left, right, down, up})
        for (const auto& action : m_map.actions) if (action.name == name) { deadzone += action.deadzone * .25f; break; }
    const float magnitude = std::hypot(axes.x, axes.y);
    if (magnitude <= deadzone) return {};
    const float scale = std::min(1.0f, (magnitude - deadzone) / (1 - deadzone)) / magnitude;
    return axes * scale;
}
bool InputActions::Held(std::string_view name) const {
    for (size_t i = 0; i < m_map.actions.size(); ++i) if (m_map.actions[i].name == name) return m_pending[i].strength > 0;
    return false;
}
} // namespace mye
