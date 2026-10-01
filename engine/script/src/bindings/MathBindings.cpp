#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"

#include <new>
#include <string_view>

namespace mye::script {
namespace {
int Method(lua_State* L, const char* type) {
    luaL_getmetatable(L, type);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}
constexpr const char* kVec2 = "mye.Vec2";
Vec2* Vec2Value(lua_State* L, int index) { return static_cast<Vec2*>(luaL_checkudata(L, index, kVec2)); }
int Vec2New(lua_State* L) {
    const int first = lua_istable(L, 1) ? 2 : 1;
    PushVec2(L, {static_cast<float>(luaL_optnumber(L, first + 0, 0)), static_cast<float>(luaL_optnumber(L, first + 1, 0))});
    return 1;
}
int Vec2Index(lua_State* L) {
    const auto& value = *Vec2Value(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    if (key == "x") lua_pushnumber(L, value.x);
    else if (key == "y") lua_pushnumber(L, value.y);
    else return Method(L, kVec2);
    return 1;
}
int Vec2Write(lua_State* L) {
    auto& value = *Vec2Value(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    const float number = static_cast<float>(luaL_checknumber(L, 3));
    if (key == "x") value.x = number;
    else if (key == "y") value.y = number;
    else return luaL_error(L, "Vec2 has no writable field '%s'", key.data());
    return 0;
}
int Vec2Equal(lua_State* L) { lua_pushboolean(L, *Vec2Value(L, 1) == *Vec2Value(L, 2)); return 1; }
constexpr const char* kVec3 = "mye.Vec3";
Vec3* Vec3Value(lua_State* L, int index) { return static_cast<Vec3*>(luaL_checkudata(L, index, kVec3)); }
int Vec3New(lua_State* L) {
    const int first = lua_istable(L, 1) ? 2 : 1;
    PushVec3(L, {static_cast<float>(luaL_optnumber(L, first + 0, 0)), static_cast<float>(luaL_optnumber(L, first + 1, 0)), static_cast<float>(luaL_optnumber(L, first + 2, 0))});
    return 1;
}
int Vec3Index(lua_State* L) {
    const auto& value = *Vec3Value(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    if (key == "x") lua_pushnumber(L, value.x);
    else if (key == "y") lua_pushnumber(L, value.y);
    else if (key == "z") lua_pushnumber(L, value.z);
    else return Method(L, kVec3);
    return 1;
}
int Vec3Write(lua_State* L) {
    auto& value = *Vec3Value(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    const float number = static_cast<float>(luaL_checknumber(L, 3));
    if (key == "x") value.x = number;
    else if (key == "y") value.y = number;
    else if (key == "z") value.z = number;
    else return luaL_error(L, "Vec3 has no writable field '%s'", key.data());
    return 0;
}
int Vec3Equal(lua_State* L) { lua_pushboolean(L, *Vec3Value(L, 1) == *Vec3Value(L, 2)); return 1; }
constexpr const char* kColor = "mye.Color";
Color* ColorValue(lua_State* L, int index) { return static_cast<Color*>(luaL_checkudata(L, index, kColor)); }
void PushColor(lua_State* L, const Color& value) {
    new (lua_newuserdatauv(L, sizeof(Color), 0)) Color(value);
    luaL_setmetatable(L, kColor);
}
int ColorNew(lua_State* L) {
    const int first = lua_istable(L, 1) ? 2 : 1;
    PushColor(L, {static_cast<float>(luaL_optnumber(L, first + 0, 0)), static_cast<float>(luaL_optnumber(L, first + 1, 0)), static_cast<float>(luaL_optnumber(L, first + 2, 0)), static_cast<float>(luaL_optnumber(L, first + 3, 1))});
    return 1;
}
int ColorIndex(lua_State* L) {
    const auto& value = *ColorValue(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    if (key == "r") lua_pushnumber(L, value.r);
    else if (key == "g") lua_pushnumber(L, value.g);
    else if (key == "b") lua_pushnumber(L, value.b);
    else if (key == "a") lua_pushnumber(L, value.a);
    else return Method(L, kColor);
    return 1;
}
int ColorWrite(lua_State* L) {
    auto& value = *ColorValue(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    const float number = static_cast<float>(luaL_checknumber(L, 3));
    if (key == "r") value.r = number;
    else if (key == "g") value.g = number;
    else if (key == "b") value.b = number;
    else if (key == "a") value.a = number;
    else return luaL_error(L, "Color has no writable field '%s'", key.data());
    return 0;
}
int ColorEqual(lua_State* L) { lua_pushboolean(L, *ColorValue(L, 1) == *ColorValue(L, 2)); return 1; }
constexpr const char* kRect = "mye.Rect";
Rect* RectValue(lua_State* L, int index) { return static_cast<Rect*>(luaL_checkudata(L, index, kRect)); }
void PushRect(lua_State* L, const Rect& value) {
    new (lua_newuserdatauv(L, sizeof(Rect), 0)) Rect(value);
    luaL_setmetatable(L, kRect);
}
int RectNew(lua_State* L) {
    const int first = lua_istable(L, 1) ? 2 : 1;
    PushRect(L, {static_cast<float>(luaL_optnumber(L, first + 0, 0)), static_cast<float>(luaL_optnumber(L, first + 1, 0)), static_cast<float>(luaL_optnumber(L, first + 2, 0)), static_cast<float>(luaL_optnumber(L, first + 3, 0))});
    return 1;
}
int RectIndex(lua_State* L) {
    const auto& value = *RectValue(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    if (key == "x") lua_pushnumber(L, value.x);
    else if (key == "y") lua_pushnumber(L, value.y);
    else if (key == "w") lua_pushnumber(L, value.w);
    else if (key == "h") lua_pushnumber(L, value.h);
    else return Method(L, kRect);
    return 1;
}
int RectWrite(lua_State* L) {
    auto& value = *RectValue(L, 1);
    const std::string_view key = luaL_checkstring(L, 2);
    const float number = static_cast<float>(luaL_checknumber(L, 3));
    if (key == "x") value.x = number;
    else if (key == "y") value.y = number;
    else if (key == "w") value.w = number;
    else if (key == "h") value.h = number;
    else return luaL_error(L, "Rect has no writable field '%s'", key.data());
    return 0;
}
int RectEqual(lua_State* L) { lua_pushboolean(L, *RectValue(L, 1) == *RectValue(L, 2)); return 1; }

int Vec2Add(lua_State* L) { PushVec2(L, ReadVec2(L, 1) + ReadVec2(L, 2)); return 1; }
int Vec2Sub(lua_State* L) { PushVec2(L, ReadVec2(L, 1) - ReadVec2(L, 2)); return 1; }
int Vec2Mul(lua_State* L) {
    if (lua_isnumber(L, 1)) PushVec2(L, ReadVec2(L, 2) * static_cast<float>(lua_tonumber(L, 1)));
    else if (lua_isnumber(L, 2)) PushVec2(L, ReadVec2(L, 1) * static_cast<float>(lua_tonumber(L, 2)));
    else { const Vec2 a = ReadVec2(L, 1), b = ReadVec2(L, 2); PushVec2(L, {a.x * b.x, a.y * b.y}); }
    return 1;
}
int Vec2Div(lua_State* L) { PushVec2(L, ReadVec2(L, 1) / static_cast<float>(luaL_checknumber(L, 2))); return 1; }
int Vec2Neg(lua_State* L) { const Vec2 v = ReadVec2(L, 1); PushVec2(L, {-v.x, -v.y}); return 1; }
int Vec2Text(lua_State* L) { const Vec2 v = ReadVec2(L, 1); lua_pushfstring(L, "Vec2(%f, %f)", static_cast<double>(v.x), static_cast<double>(v.y)); return 1; }
int Vec2Length(lua_State* L) { lua_pushnumber(L, ReadVec2(L, 1).Length()); return 1; }
int Vec2LengthSq(lua_State* L) { const Vec2 v = ReadVec2(L, 1); lua_pushnumber(L, Vec2::Dot(v, v)); return 1; }
int Vec2Normalize(lua_State* L) { PushVec2(L, ReadVec2(L, 1).Normalized()); return 1; }
int Vec2Dot(lua_State* L) { lua_pushnumber(L, Vec2::Dot(ReadVec2(L, 1), ReadVec2(L, 2))); return 1; }
int Vec2Distance(lua_State* L) { lua_pushnumber(L, (ReadVec2(L, 1) - ReadVec2(L, 2)).Length()); return 1; }
int Vec2Lerp(lua_State* L) { PushVec2(L, Vec2::Lerp(ReadVec2(L, 1), ReadVec2(L, 2), static_cast<float>(luaL_checknumber(L, 3)))); return 1; }
int ColorWhite(lua_State* L) { PushColor(L, Color::White()); return 1; }
int ColorBlack(lua_State* L) { PushColor(L, Color::Black()); return 1; }
int ColorTransparent(lua_State* L) { PushColor(L, Color::Transparent()); return 1; }
int RectContains(lua_State* L) { lua_pushboolean(L, RectValue(L, 1)->Contains(ReadVec2(L, 2))); return 1; }
int RectOverlaps(lua_State* L) { lua_pushboolean(L, RectValue(L, 1)->Overlaps(*RectValue(L, 2))); return 1; }

void Constructor(lua_State* L, const char* name, lua_CFunction create, const luaL_Reg* members = nullptr) {
    lua_newtable(L);
    if (members) luaL_setfuncs(L, members, 0);
    lua_pushcfunction(L, create); lua_setfield(L, -2, "new");
    lua_newtable(L);
    lua_pushcfunction(L, create); lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, name);
}
} // namespace

void PushVec2(lua_State* L, const Vec2& value) {
    new (lua_newuserdatauv(L, sizeof(Vec2), 0)) Vec2(value);
    luaL_setmetatable(L, kVec2);
}
void PushVec3(lua_State* L, const Vec3& value) {
    new (lua_newuserdatauv(L, sizeof(Vec3), 0)) Vec3(value);
    luaL_setmetatable(L, kVec3);
}
Vec2 ReadVec2(lua_State* L, int index) { return *Vec2Value(L, index); }
Vec3 ReadVec3(lua_State* L, int index) { return *Vec3Value(L, index); }

void MathBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    const luaL_Reg vec2[] = {{"__index", Vec2Index}, {"__newindex", Vec2Write}, {"__add", Vec2Add}, {"__sub", Vec2Sub}, {"__mul", Vec2Mul}, {"__div", Vec2Div}, {"__unm", Vec2Neg}, {"__eq", Vec2Equal}, {"__tostring", Vec2Text}, {"length", Vec2Length}, {"length_sq", Vec2LengthSq}, {"normalized", Vec2Normalize}, {"dot", Vec2Dot}, {"lerp", Vec2Lerp}, {nullptr, nullptr}};
    const luaL_Reg vec3[] = {{"__index", Vec3Index}, {"__newindex", Vec3Write}, {"__eq", Vec3Equal}, {nullptr, nullptr}};
    const luaL_Reg color[] = {{"__index", ColorIndex}, {"__newindex", ColorWrite}, {"__eq", ColorEqual}, {nullptr, nullptr}};
    const luaL_Reg presets[] = {{"white", ColorWhite}, {"black", ColorBlack}, {"transparent", ColorTransparent}, {nullptr, nullptr}};
    const luaL_Reg rect[] = {{"__index", RectIndex}, {"__newindex", RectWrite}, {"__eq", RectEqual}, {"contains", RectContains}, {"overlaps", RectOverlaps}, {nullptr, nullptr}};
    luaL_newmetatable(L, kVec2); luaL_setfuncs(L, vec2, 0); lua_pop(L, 1);
    luaL_newmetatable(L, kVec3); luaL_setfuncs(L, vec3, 0); lua_pop(L, 1);
    luaL_newmetatable(L, kColor); luaL_setfuncs(L, color, 0); luaL_setfuncs(L, presets, 0); lua_pop(L, 1);
    luaL_newmetatable(L, kRect); luaL_setfuncs(L, rect, 0); lua_pop(L, 1);
    lua_getglobal(L, "mye");
    Constructor(L, "Vec2", Vec2New);
    Constructor(L, "Vec3", Vec3New);
    Constructor(L, "Color", ColorNew, presets);
    Constructor(L, "Rect", RectNew);
    lua_pushcfunction(L, Vec2Dot); lua_setfield(L, -2, "vec2_dot");
    lua_pushcfunction(L, Vec2Distance); lua_setfield(L, -2, "vec2_distance");
}
} // namespace mye::script
