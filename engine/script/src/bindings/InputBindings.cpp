#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"
#include "mye/core/Input.h"
#include "mye/core/InputActions.h"

namespace mye::script {
namespace {
KeyCode ToKey(lua_Integer key) { return key >= 0 && key < static_cast<int>(KeyCode::Count) ? static_cast<KeyCode>(key) : KeyCode::Unknown; }
int PadIndex(lua_State* L, int index) { const auto pad = luaL_optinteger(L, index, 0); return pad >= 0 && pad < kMaxGamepads ? static_cast<int>(pad) : -1; }
bool ValidPad(lua_Integer button) { return button >= 0 && button < static_cast<int>(GamepadButton::Count); }
void Constant(lua_State* L, const char* name, int value) { lua_pushinteger(L, value); lua_setfield(L, -2, name); }
std::string_view ActionName(lua_State* L, int index) {
    luaL_checktype(L, index, LUA_TSTRING);
    size_t length = 0;
    const char* name = luaL_checklstring(L, index, &length);
    luaL_argcheck(L, length > 0 && length <= 64, index, "action name requires 1..64 bytes");
    return {name, length};
}
InputActionState ReadAction(lua_State* L, const InputActions* actions) {
    const auto name = ActionName(L, 1);
    return actions ? actions->Action(name) : InputActionState{};
}
} // namespace
void InputBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "input");
    PushFunction(L, [](lua_State* L) -> int {
        lua_pushboolean(L, ReadAction(L, Context<InputBindingModule>(L)->m_actions).strength > 0); return 1;
    }, this); lua_setfield(L, -2, "is_action_pressed");
    PushFunction(L, [](lua_State* L) -> int {
        lua_pushboolean(L, ReadAction(L, Context<InputBindingModule>(L)->m_actions).pressed); return 1;
    }, this); lua_setfield(L, -2, "is_action_just_pressed");
    PushFunction(L, [](lua_State* L) -> int {
        lua_pushboolean(L, ReadAction(L, Context<InputBindingModule>(L)->m_actions).released); return 1;
    }, this); lua_setfield(L, -2, "is_action_just_released");
    PushFunction(L, [](lua_State* L) -> int {
        lua_pushnumber(L, ReadAction(L, Context<InputBindingModule>(L)->m_actions).strength); return 1;
    }, this); lua_setfield(L, -2, "get_action_strength");
    PushFunction(L, [](lua_State* L) -> int {
        lua_pushnumber(L, ReadAction(L, Context<InputBindingModule>(L)->m_actions).rawStrength); return 1;
    }, this); lua_setfield(L, -2, "get_action_raw_strength");
    PushFunction(L, [](lua_State* L) -> int {
        const auto left = ActionName(L, 1), right = ActionName(L, 2), down = ActionName(L, 3), up = ActionName(L, 4);
        const auto* actions = Context<InputBindingModule>(L)->m_actions;
        PushVec2(L, actions ? actions->Vector(left, right, down, up) : Vec2{}); return 1;
    }, this); lua_setfield(L, -2, "get_vector");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto key = ToKey(luaL_checkinteger(L, 1));
        lua_pushboolean(L, input && input->IsDown(key)); return 1;
    }, this); lua_setfield(L, -2, "is_down");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto key = ToKey(luaL_checkinteger(L, 1));
        lua_pushboolean(L, input && input->WasPressed(key)); return 1;
    }, this); lua_setfield(L, -2, "was_pressed");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto key = ToKey(luaL_checkinteger(L, 1));
        lua_pushboolean(L, input && input->WasReleased(key)); return 1;
    }, this); lua_setfield(L, -2, "was_released");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto negX = ToKey(luaL_checkinteger(L, 1)), posX = ToKey(luaL_checkinteger(L, 2));
        const auto negY = ToKey(luaL_checkinteger(L, 3)), posY = ToKey(luaL_checkinteger(L, 4));
        PushVec2(L, input ? Vec2{static_cast<float>(input->IsDown(posX)) - static_cast<float>(input->IsDown(negX)), static_cast<float>(input->IsDown(posY)) - static_cast<float>(input->IsDown(negY))} : Vec2{});
        return 1;
    }, this); lua_setfield(L, -2, "move_axis");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const int pad = PadIndex(L, 1);
        lua_pushboolean(L, input && input->IsGamepadConnected(pad)); return 1;
    }, this); lua_setfield(L, -2, "pad_connected");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto button = luaL_checkinteger(L, 1); const int pad = PadIndex(L, 2);
        lua_pushboolean(L, input && ValidPad(button) && input->IsDown(static_cast<GamepadButton>(button), pad)); return 1;
    }, this); lua_setfield(L, -2, "pad_down");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto button = luaL_checkinteger(L, 1); const int pad = PadIndex(L, 2);
        lua_pushboolean(L, input && ValidPad(button) && input->WasPressed(static_cast<GamepadButton>(button), pad)); return 1;
    }, this); lua_setfield(L, -2, "pad_pressed");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const auto button = luaL_checkinteger(L, 1); const int pad = PadIndex(L, 2);
        lua_pushboolean(L, input && ValidPad(button) && input->WasReleased(static_cast<GamepadButton>(button), pad)); return 1;
    }, this); lua_setfield(L, -2, "pad_released");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const int pad = PadIndex(L, 1);
        PushVec2(L, input ? input->LeftStick(pad) : Vec2{}); return 1;
    }, this); lua_setfield(L, -2, "left_stick");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const int pad = PadIndex(L, 1);
        PushVec2(L, input ? input->RightStick(pad) : Vec2{}); return 1;
    }, this); lua_setfield(L, -2, "right_stick");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const int pad = PadIndex(L, 1);
        lua_pushnumber(L, input ? input->LeftTrigger(pad) : 0.0f); return 1;
    }, this); lua_setfield(L, -2, "left_trigger");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* input = Context<InputBindingModule>(L)->m_input;
        const int pad = PadIndex(L, 1);
        lua_pushnumber(L, input ? input->RightTrigger(pad) : 0.0f); return 1;
    }, this); lua_setfield(L, -2, "right_trigger");
    lua_pop(L, 1); EnsureTable(L, -1, "Pad");
    Constant(L, "A", static_cast<int>(GamepadButton::A));
    Constant(L, "B", static_cast<int>(GamepadButton::B));
    Constant(L, "X", static_cast<int>(GamepadButton::X));
    Constant(L, "Y", static_cast<int>(GamepadButton::Y));
    Constant(L, "DUP", static_cast<int>(GamepadButton::DPadUp));
    Constant(L, "DDOWN", static_cast<int>(GamepadButton::DPadDown));
    Constant(L, "DLEFT", static_cast<int>(GamepadButton::DPadLeft));
    Constant(L, "DRIGHT", static_cast<int>(GamepadButton::DPadRight));
    Constant(L, "LB", static_cast<int>(GamepadButton::LeftShoulder));
    Constant(L, "RB", static_cast<int>(GamepadButton::RightShoulder));
    Constant(L, "LSTICK", static_cast<int>(GamepadButton::LeftThumb));
    Constant(L, "RSTICK", static_cast<int>(GamepadButton::RightThumb));
    Constant(L, "START", static_cast<int>(GamepadButton::Start));
    Constant(L, "BACK", static_cast<int>(GamepadButton::Back));
    lua_pop(L, 1); EnsureTable(L, -1, "Key");
    Constant(L, "A", static_cast<int>(KeyCode::A));
    Constant(L, "B", static_cast<int>(KeyCode::B));
    Constant(L, "C", static_cast<int>(KeyCode::C));
    Constant(L, "D", static_cast<int>(KeyCode::D));
    Constant(L, "E", static_cast<int>(KeyCode::E));
    Constant(L, "F", static_cast<int>(KeyCode::F));
    Constant(L, "G", static_cast<int>(KeyCode::G));
    Constant(L, "H", static_cast<int>(KeyCode::H));
    Constant(L, "I", static_cast<int>(KeyCode::I));
    Constant(L, "J", static_cast<int>(KeyCode::J));
    Constant(L, "K", static_cast<int>(KeyCode::K));
    Constant(L, "L", static_cast<int>(KeyCode::L));
    Constant(L, "M", static_cast<int>(KeyCode::M));
    Constant(L, "N", static_cast<int>(KeyCode::N));
    Constant(L, "O", static_cast<int>(KeyCode::O));
    Constant(L, "P", static_cast<int>(KeyCode::P));
    Constant(L, "Q", static_cast<int>(KeyCode::Q));
    Constant(L, "R", static_cast<int>(KeyCode::R));
    Constant(L, "S", static_cast<int>(KeyCode::S));
    Constant(L, "T", static_cast<int>(KeyCode::T));
    Constant(L, "U", static_cast<int>(KeyCode::U));
    Constant(L, "V", static_cast<int>(KeyCode::V));
    Constant(L, "W", static_cast<int>(KeyCode::W));
    Constant(L, "X", static_cast<int>(KeyCode::X));
    Constant(L, "Y", static_cast<int>(KeyCode::Y));
    Constant(L, "Z", static_cast<int>(KeyCode::Z));
    Constant(L, "NUM0", static_cast<int>(KeyCode::Num0));
    Constant(L, "NUM1", static_cast<int>(KeyCode::Num1));
    Constant(L, "NUM2", static_cast<int>(KeyCode::Num2));
    Constant(L, "NUM3", static_cast<int>(KeyCode::Num3));
    Constant(L, "NUM4", static_cast<int>(KeyCode::Num4));
    Constant(L, "NUM5", static_cast<int>(KeyCode::Num5));
    Constant(L, "NUM6", static_cast<int>(KeyCode::Num6));
    Constant(L, "NUM7", static_cast<int>(KeyCode::Num7));
    Constant(L, "NUM8", static_cast<int>(KeyCode::Num8));
    Constant(L, "NUM9", static_cast<int>(KeyCode::Num9));
    Constant(L, "SPACE", static_cast<int>(KeyCode::Space));
    Constant(L, "ENTER", static_cast<int>(KeyCode::Enter));
    Constant(L, "ESCAPE", static_cast<int>(KeyCode::Escape));
    Constant(L, "TAB", static_cast<int>(KeyCode::Tab));
    Constant(L, "LEFT", static_cast<int>(KeyCode::Left));
    Constant(L, "RIGHT", static_cast<int>(KeyCode::Right));
    Constant(L, "UP", static_cast<int>(KeyCode::Up));
    Constant(L, "DOWN", static_cast<int>(KeyCode::Down));
    Constant(L, "LSHIFT", static_cast<int>(KeyCode::LeftShift));
    Constant(L, "LCTRL", static_cast<int>(KeyCode::LeftControl));
    Constant(L, "LALT", static_cast<int>(KeyCode::LeftAlt));
}
} // namespace mye::script
