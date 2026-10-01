#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <string>
#include <vector>

namespace mye::script {
void EventBindingModule::Register(lua_State* lua) {
    LuaStackGuard guard(lua);
    lua_getglobal(lua, "mye");
    const int mye = lua_gettop(lua);
    EnsureTable(lua, mye, "_event_handlers"); lua_pop(lua, 1);
    lua_getfield(lua, mye, "_event_next_id");
    if (!lua_isinteger(lua, -1)) { lua_pop(lua, 1); lua_pushinteger(lua, 1); }
    lua_setfield(lua, mye, "_event_next_id");
    EnsureTable(lua, mye, "events");
    PushFunction(lua, [](lua_State* state) -> int {
        const char* name = luaL_checkstring(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);
        lua_getglobal(state, "mye");
        const int root = lua_gettop(state);
        lua_getfield(state, root, "_event_next_id");
        const auto id = lua_tointeger(state, -1); lua_pop(state, 1);
        lua_pushinteger(state, id + 1); lua_setfield(state, root, "_event_next_id");
        lua_getfield(state, root, "_event_handlers");
        EnsureTable(state, -1, name);
        lua_pushvalue(state, 2); lua_rawseti(state, -2, id);
        lua_pushinteger(state, id);
        return 1;
    }); lua_setfield(lua, -2, "on");
    PushFunction(lua, [](lua_State* state) -> int {
        const char* name = luaL_checkstring(state, 1);
        const auto id = luaL_checkinteger(state, 2);
        lua_getglobal(state, "mye"); lua_getfield(state, -1, "_event_handlers");
        lua_getfield(state, -1, name);
        if (lua_istable(state, -1)) { lua_pushnil(state); lua_rawseti(state, -2, id); }
        return 0;
    }); lua_setfield(lua, -2, "off");
    PushFunction(lua, [](lua_State* state) -> int {
        const std::string name = luaL_checkstring(state, 1);
        const int arguments = lua_gettop(state) - 1;
        lua_getglobal(state, "mye"); lua_getfield(state, -1, "_event_handlers");
        lua_getfield(state, -1, name.c_str());
        if (!lua_istable(state, -1)) return 0;
        std::vector<std::pair<lua_Integer, LuaReference>> snapshot;
        lua_pushnil(state);
        while (lua_next(state, -2)) {
            if (lua_isinteger(state, -2) && lua_isfunction(state, -1))
                snapshot.emplace_back(lua_tointeger(state, -2), LuaReference(state, -1));
            lua_pop(state, 1);
        }
        std::sort(snapshot.begin(), snapshot.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [id, function] : snapshot) {
            function.Push(state);
            for (int i = 0; i < arguments; ++i) lua_pushvalue(state, 2 + i);
            if (ProtectedCall(state, arguments, 0) != LUA_OK) {
                MYE_LOG_ERROR("Script", "event handler '{}' failed: {}", name, lua_tostring(state, -1));
                lua_pop(state, 1);
            }
        }
        return 0;
    }); lua_setfield(lua, -2, "emit");
}
} // namespace mye::script
