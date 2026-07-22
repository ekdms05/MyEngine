// mye/script/src/bindings/MathBindings.cpp — mye.Vec2 / mye.Color / mye.Rect (docs/05, M3-C)
//
// 핫패스 값 타입을 sol2 usertype 으로 노출. 값 시맨틱(복사) — Lua 에서 표현식 결과가 새 값이다.
//   연산자 오버로드(+, -, *, ==)와 length/normalize 등 자주 쓰는 메서드만 좁게 노출한다.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/core/Math.h"

#include <sol/sol.hpp>

namespace mye::script {

void MathBindingModule::Register(sol::state& lua) {
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- Vec2 ----------------------------------------------------------
    // 생성: mye.Vec2(), mye.Vec2(x, y). 필드 x/y 읽기·쓰기. 연산자·헬퍼.
    mye.new_usertype<Vec2>(
        "Vec2",
        sol::call_constructor,
        sol::constructors<Vec2(), Vec2(float, float)>(),
        "x", &Vec2::x,
        "y", &Vec2::y,
        // 산술 연산자(값 반환).
        sol::meta_function::addition, [](const Vec2& a, const Vec2& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vec2& a, const Vec2& b) { return a - b; },
        // 스칼라 곱/나눗셈(Vec2 * number). number * Vec2 는 __mul 대칭 처리.
        sol::meta_function::multiplication,
        sol::overload(
            [](const Vec2& a, float s) { return a * s; },
            [](float s, const Vec2& a) { return a * s; },
            [](const Vec2& a, const Vec2& b) { return Vec2{a.x * b.x, a.y * b.y}; }),
        sol::meta_function::division, [](const Vec2& a, float s) { return a / s; },
        sol::meta_function::unary_minus, [](const Vec2& a) { return Vec2{-a.x, -a.y}; },
        sol::meta_function::equal_to, [](const Vec2& a, const Vec2& b) { return a == b; },
        sol::meta_function::to_string,
        [](const Vec2& v) { return "Vec2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")"; },
        // 메서드(snake_case).
        "length", [](const Vec2& v) { return v.Length(); },
        "length_sq", [](const Vec2& v) { return Vec2::Dot(v, v); },
        "normalized", [](const Vec2& v) { return v.Normalized(); },
        "dot", [](const Vec2& a, const Vec2& b) { return Vec2::Dot(a, b); },
        "lerp", [](const Vec2& a, const Vec2& b, float t) { return Vec2::Lerp(a, b, t); });

    // 자유 함수 헬퍼: mye.vec2_dot / mye.vec2_distance (연산이 잦은 게임 로직 편의).
    mye.set_function("vec2_dot", [](const Vec2& a, const Vec2& b) { return Vec2::Dot(a, b); });
    mye.set_function("vec2_distance",
                     [](const Vec2& a, const Vec2& b) { return (a - b).Length(); });

    // ---- Color (float RGBA linear) -------------------------------------
    mye.new_usertype<Color>(
        "Color",
        sol::call_constructor,
        sol::constructors<Color(), Color(float, float, float, float)>(),
        "r", &Color::r,
        "g", &Color::g,
        "b", &Color::b,
        "a", &Color::a,
        sol::meta_function::equal_to, [](const Color& a, const Color& b) { return a == b; },
        // 자주 쓰는 프리셋(정적 팩토리 스타일 — 함수로 노출).
        "white", [] { return Color::White(); },
        "black", [] { return Color::Black(); },
        "transparent", [] { return Color::Transparent(); });

    // ---- Rect ----------------------------------------------------------
    mye.new_usertype<Rect>(
        "Rect",
        sol::call_constructor,
        sol::constructors<Rect(), Rect(float, float, float, float)>(),
        "x", &Rect::x,
        "y", &Rect::y,
        "w", &Rect::w,
        "h", &Rect::h,
        "contains", [](const Rect& r, const Vec2& p) { return r.Contains(p); },
        "overlaps", [](const Rect& a, const Rect& b) { return a.Overlaps(b); },
        sol::meta_function::equal_to, [](const Rect& a, const Rect& b) { return a == b; });
}

} // namespace mye::script
