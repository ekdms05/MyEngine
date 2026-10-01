#pragma once

#include "TestFramework.h"
#include "mye/script/LuaApi.h"

#include <string>
#include <string_view>
#include <type_traits>

namespace luatest {

// Tests use the real protected C API rather than an alternate binding layer.
template<class T = void> T Eval(lua_State* state, std::string_view source) {
    mye::script::LuaStackGuard guard(state);
    int status = luaL_loadbufferx(state, source.data(), source.size(), "@test", "t");
    if (status == LUA_OK) status = mye::script::ProtectedCall(state, 0, std::is_void_v<T> ? 0 : 1);
    if (status != LUA_OK) std::fprintf(stderr, "Lua test: %s\n", lua_tostring(state, -1));
    MYE_EXPECT(status == LUA_OK);
    if constexpr (std::is_void_v<T>) return;
    else {
        if (status != LUA_OK) return {};
        if constexpr (std::is_same_v<T, bool>) return lua_toboolean(state, -1) != 0;
        else if constexpr (std::is_integral_v<T>) return static_cast<T>(lua_tointeger(state, -1));
        else if constexpr (std::is_floating_point_v<T>) return static_cast<T>(lua_tonumber(state, -1));
        else if constexpr (std::is_same_v<T, std::string>) {
            size_t size = 0; const char* text = lua_tolstring(state, -1, &size);
            return text ? std::string(text, size) : std::string{};
        } else if constexpr (std::is_same_v<T, mye::Vec2>) return mye::script::ReadVec2(state, -1);
    }
}
inline mye::script::LuaReference Integer(lua_State* state, lua_Integer value) {
    mye::script::LuaStackGuard guard(state);
    lua_pushinteger(state, value);
    return mye::script::LuaReference(state, -1);
}
inline void SetInteger(lua_State* state, const char* name, lua_Integer value) {
    lua_pushinteger(state, value); lua_setglobal(state, name);
}
inline void SetGlobal(lua_State* state, const char* name, const mye::script::LuaReference& value) {
    value.Push(state); lua_setglobal(state, name);
}
} // namespace luatest
