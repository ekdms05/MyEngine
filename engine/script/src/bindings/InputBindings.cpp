// mye/script/src/bindings/InputBindings.cpp — 입력 폴링·키 상수·이동축 헬퍼 (docs/05, M3-C)
//
// mye.input.is_down / was_pressed / was_released(key) — InputState 폴링을 노출.
// mye.Key.* — 물리 KeyCode 상수(레이아웃 무관). 상수는 정수로 노출(Lua enum 관용).
// mye.input.move_axis(neg_x, pos_x, neg_y, pos_y) → Vec2 방향(각 축 -1/0/+1). +Y=위.
//   WASD/화살표 조합은 게임 코드에서 mye.Key 상수로 조립한다(엔진은 매핑 비소유 — 06).
//
// 안전: InputState 미주입(nullptr) 시 모든 질의는 false/영벡터(폴백). 잘못된 키 정수는 Unknown.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/core/Input.h"
#include "mye/core/Math.h"

#include <sol/sol.hpp>

#include <cstdint>

namespace mye::script {

namespace {

KeyCode ToKey(int raw) {
    if (raw < 0 || raw >= static_cast<int>(KeyCode::Count)) return KeyCode::Unknown;
    return static_cast<KeyCode>(raw);
}

bool ValidPadButton(int raw) {
    return raw >= 0 && raw < static_cast<int>(GamepadButton::Count);
}
GamepadButton ToPad(int raw) { return static_cast<GamepadButton>(raw); }

} // namespace

void InputBindingModule::Register(sol::state& lua) {
    const InputState* input = m_input;   // 캡처용(비소유, nullptr 허용).
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- mye.input 폴링 ------------------------------------------------
    sol::table in = mye["input"].get_or_create<sol::table>();

    in.set_function("is_down", [input](int key) {
        return input ? input->IsDown(ToKey(key)) : false;
    });
    in.set_function("was_pressed", [input](int key) {
        return input ? input->WasPressed(ToKey(key)) : false;
    });
    in.set_function("was_released", [input](int key) {
        return input ? input->WasReleased(ToKey(key)) : false;
    });

    // 이동축 헬퍼: 네 방향 키를 받아 정규화 전 방향 벡터(각 성분 -1/0/+1) 반환. +Y=위.
    in.set_function("move_axis",
                    [input](int negX, int posX, int negY, int posY) -> Vec2 {
        if (!input) return Vec2{};
        float x = 0.0f, y = 0.0f;
        if (input->IsDown(ToKey(negX))) x -= 1.0f;
        if (input->IsDown(ToKey(posX))) x += 1.0f;
        if (input->IsDown(ToKey(negY))) y -= 1.0f;
        if (input->IsDown(ToKey(posY))) y += 1.0f;
        return Vec2{x, y};
    });

    // ---- mye.input 게임패드(XInput) 폴링 -------------------------------
    // pad 기본 0. 버튼은 mye.Pad.* 상수. 스틱은 Vec2(각 축 -1..1, +Y=위), 트리거는 0..1.
    in.set_function("pad_connected", [input](sol::optional<int> pad) {
        return input ? input->IsGamepadConnected(pad.value_or(0)) : false;
    });
    in.set_function("pad_down", [input](int btn, sol::optional<int> pad) {
        return (input && ValidPadButton(btn)) ? input->IsDown(ToPad(btn), pad.value_or(0)) : false;
    });
    in.set_function("pad_pressed", [input](int btn, sol::optional<int> pad) {
        return (input && ValidPadButton(btn)) ? input->WasPressed(ToPad(btn), pad.value_or(0)) : false;
    });
    in.set_function("pad_released", [input](int btn, sol::optional<int> pad) {
        return (input && ValidPadButton(btn)) ? input->WasReleased(ToPad(btn), pad.value_or(0)) : false;
    });
    in.set_function("left_stick", [input](sol::optional<int> pad) -> Vec2 {
        return input ? input->LeftStick(pad.value_or(0)) : Vec2{};
    });
    in.set_function("right_stick", [input](sol::optional<int> pad) -> Vec2 {
        return input ? input->RightStick(pad.value_or(0)) : Vec2{};
    });
    in.set_function("left_trigger", [input](sol::optional<int> pad) {
        return input ? input->LeftTrigger(pad.value_or(0)) : 0.0f;
    });
    in.set_function("right_trigger", [input](sol::optional<int> pad) {
        return input ? input->RightTrigger(pad.value_or(0)) : 0.0f;
    });

    // ---- mye.Pad 상수(게임패드 버튼) ----
    sol::table pad = mye["Pad"].get_or_create<sol::table>();
    auto pb = [](GamepadButton c) { return static_cast<int>(c); };
    pad["A"] = pb(GamepadButton::A); pad["B"] = pb(GamepadButton::B);
    pad["X"] = pb(GamepadButton::X); pad["Y"] = pb(GamepadButton::Y);
    pad["DUP"] = pb(GamepadButton::DPadUp);     pad["DDOWN"] = pb(GamepadButton::DPadDown);
    pad["DLEFT"] = pb(GamepadButton::DPadLeft); pad["DRIGHT"] = pb(GamepadButton::DPadRight);
    pad["LB"] = pb(GamepadButton::LeftShoulder); pad["RB"] = pb(GamepadButton::RightShoulder);
    pad["LSTICK"] = pb(GamepadButton::LeftThumb); pad["RSTICK"] = pb(GamepadButton::RightThumb);
    pad["START"] = pb(GamepadButton::Start); pad["BACK"] = pb(GamepadButton::Back);

    // ---- mye.Key 상수 --------------------------------------------------
    // 물리 스캔코드 기반(QWERTY/AZERTY 무관). 자주 쓰는 키만 노출.
    sol::table key = mye["Key"].get_or_create<sol::table>();
    auto k = [](KeyCode c) { return static_cast<int>(c); };
    key["A"] = k(KeyCode::A); key["B"] = k(KeyCode::B); key["C"] = k(KeyCode::C);
    key["D"] = k(KeyCode::D); key["E"] = k(KeyCode::E); key["F"] = k(KeyCode::F);
    key["G"] = k(KeyCode::G); key["H"] = k(KeyCode::H); key["I"] = k(KeyCode::I);
    key["J"] = k(KeyCode::J); key["K"] = k(KeyCode::K); key["L"] = k(KeyCode::L);
    key["M"] = k(KeyCode::M); key["N"] = k(KeyCode::N); key["O"] = k(KeyCode::O);
    key["P"] = k(KeyCode::P); key["Q"] = k(KeyCode::Q); key["R"] = k(KeyCode::R);
    key["S"] = k(KeyCode::S); key["T"] = k(KeyCode::T); key["U"] = k(KeyCode::U);
    key["V"] = k(KeyCode::V); key["W"] = k(KeyCode::W); key["X"] = k(KeyCode::X);
    key["Y"] = k(KeyCode::Y); key["Z"] = k(KeyCode::Z);
    key["NUM0"] = k(KeyCode::Num0); key["NUM1"] = k(KeyCode::Num1);
    key["NUM2"] = k(KeyCode::Num2); key["NUM3"] = k(KeyCode::Num3);
    key["NUM4"] = k(KeyCode::Num4); key["NUM5"] = k(KeyCode::Num5);
    key["NUM6"] = k(KeyCode::Num6); key["NUM7"] = k(KeyCode::Num7);
    key["NUM8"] = k(KeyCode::Num8); key["NUM9"] = k(KeyCode::Num9);
    key["SPACE"]  = k(KeyCode::Space);
    key["ENTER"]  = k(KeyCode::Enter);
    key["ESCAPE"] = k(KeyCode::Escape);
    key["TAB"]    = k(KeyCode::Tab);
    key["LEFT"]   = k(KeyCode::Left);
    key["RIGHT"]  = k(KeyCode::Right);
    key["UP"]     = k(KeyCode::Up);
    key["DOWN"]   = k(KeyCode::Down);
    key["LSHIFT"] = k(KeyCode::LeftShift);
    key["LCTRL"]  = k(KeyCode::LeftControl);
    key["LALT"]   = k(KeyCode::LeftAlt);
}

} // namespace mye::script
