#include "mye/script/ScriptClass.h"
#include "ScriptErrorParse.h"

namespace mye::script {

Expected<LuaReference, ScriptError> LoadClass(ScriptRuntime& runtime,
                                           std::string_view source,
                                           std::string_view chunkName) {
    lua_State* lua = runtime.State();
    if (!lua) return ScriptError{std::string(chunkName), 0, "Lua runtime is not initialized"};
    LuaStackGuard stack(lua);
    const std::string named = "@" + std::string(chunkName);
    int status = luaL_loadbufferx(lua, source.data(), source.size(), named.c_str(), "t");
    if (status == LUA_OK) status = ProtectedCall(lua, 0, 1);
    if (status != LUA_OK) {
        const char* message = lua_tostring(lua, -1);
        const auto error = detail::ParseLuaError(message ? message : "Lua class load failed", chunkName);
        return ScriptError{error.file, error.line, error.message};
    }
    if (!lua_istable(lua, -1))
        return ScriptError{std::string(chunkName), 0, "script must return a class table"};
    return LuaReference(lua, -1);
}

LuaReference MakeInstance(ScriptRuntime& runtime, const LuaReference& classTable,
                          ecs::Entity entity, const LuaReference& props) {
    lua_State* lua = runtime.State();
    if (!lua || !classTable.Valid()) return {};
    LuaStackGuard stack(lua);
    lua_newtable(lua);
    const int instance = lua_gettop(lua);
    lua_newtable(lua);
    classTable.Push(lua); lua_setfield(lua, -2, "__index");
    lua_setmetatable(lua, instance);
    lua_pushinteger(lua, static_cast<lua_Integer>(entity.Packed())); lua_setfield(lua, instance, "entity");
    lua_newtable(lua); lua_setfield(lua, instance, "state");
    if (props.Valid()) {
        props.Push(lua);
        const int properties = lua_gettop(lua);
        if (lua_istable(lua, properties)) {
            lua_pushnil(lua);
            while (lua_next(lua, properties)) {
                lua_pushvalue(lua, -2); lua_pushvalue(lua, -2);
                lua_rawset(lua, instance); lua_pop(lua, 1);
            }
        }
    }
    return LuaReference(lua, instance);
}

uint32_t ScanCallbacks(const LuaReference& classTable) {
    if (!classTable.Valid()) return 0;
    lua_State* lua = classTable.State();
    LuaStackGuard stack(lua);
    classTable.Push(lua);
    constexpr std::pair<std::string_view, CallbackBit> names[] = {
        {callbacks::kOnInit, CallbackBit::OnInit}, {callbacks::kOnStart, CallbackBit::OnStart},
        {callbacks::kOnUpdate, CallbackBit::OnUpdate}, {callbacks::kOnLateUpdate, CallbackBit::OnLateUpdate},
        {callbacks::kOnEvent, CallbackBit::OnEvent}, {callbacks::kOnTriggerEnter, CallbackBit::OnTriggerEnter},
        {callbacks::kOnTriggerExit, CallbackBit::OnTriggerExit}, {callbacks::kOnHotReload, CallbackBit::OnHotReload},
        {callbacks::kOnDestroy, CallbackBit::OnDestroy},
    };
    uint32_t bits = 0;
    for (const auto& [name, bit] : names) {
        lua_getfield(lua, -1, name.data());
        if (lua_isfunction(lua, -1)) bits |= static_cast<uint32_t>(bit);
        lua_pop(lua, 1);
    }
    return bits;
}
} // namespace mye::script
