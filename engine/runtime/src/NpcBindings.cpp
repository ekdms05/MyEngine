#include "mye/runtime/NpcBindings.h"
#include "mye/runtime/NpcSystem.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/LuaApi.h"
#include "mye/core/Log.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace mye::runtime {
using namespace script;
namespace {
float NumberField(lua_State* L, int table, const char* key, float fallback) {
    lua_getfield(L, table, key); const float value = static_cast<float>(luaL_optnumber(L, -1, fallback)); lua_pop(L, 1); return value;
}
std::string StringField(lua_State* L, int table, const char* key, const char* fallback = "") {
    lua_getfield(L, table, key); std::string value = luaL_optstring(L, -1, fallback); lua_pop(L, 1); return value;
}
bool BoolField(lua_State* L, int table, const char* key, bool fallback) {
    lua_getfield(L, table, key);
    if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TBOOLEAN);
    const bool value = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1); return value;
}
std::vector<Vec2> ParseWaypoints(lua_State* L, int table) {
    std::vector<Vec2> points;
    if (lua_isnil(L, table)) return points;
    luaL_checktype(L, table, LUA_TTABLE); table = lua_absindex(L, table);
    const size_t count = lua_rawlen(L, table); points.reserve(count);
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, table, static_cast<lua_Integer>(i));
        if (lua_istable(L, -1)) {
            const int point = lua_gettop(L);
            lua_getfield(L, point, "x"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_rawgeti(L, point, 1); }
            const float x = static_cast<float>(luaL_optnumber(L, -1, 0)); lua_pop(L, 1);
            lua_getfield(L, point, "y"); if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_rawgeti(L, point, 2); }
            const float y = static_cast<float>(luaL_optnumber(L, -1, 0)); lua_pop(L, 1);
            points.push_back({x, y});
        }
        lua_pop(L, 1);
    }
    return points;
}
bool CallHandler(const LuaReference& handler, const std::string& id) {
    if (!handler.Valid()) return false;
    lua_State* L = handler.State(); LuaStackGuard stack(L);
    handler.Push(); lua_pushlstring(L, id.data(), id.size());
    if (ProtectedCall(L, 1, 1) != LUA_OK) { MYE_LOG_WARN("NpcBindings", "NPC callback failed: {}", lua_tostring(L, -1)); return false; }
    return lua_toboolean(L, -1) != 0;
}
} // namespace
NpcBindings::NpcBindings(NpcSystem* npc, script::ScriptRuntime* runtime) : m_npc(npc), m_runtime(runtime) {}
void NpcBindings::Register(lua_State* L) {
    LuaStackGuard stack(L);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "npc"); const int npc = lua_gettop(L);
    lua_newtable(L); lua_setfield(L, npc, "__handlers");
    lua_newtable(L); lua_setfield(L, npc, "__busy");
    lua_pushlightuserdata(L, this); lua_getfield(L, npc, "__handlers");
    lua_pushcclosure(L, [](lua_State* L) -> int {
        auto* sys = Context<NpcBindings>(L)->m_npc; luaL_checktype(L, 1, LUA_TTABLE);
        lua_getfield(L, 1, "entity");
        if (lua_isnil(L, -1) || !sys) { lua_pushboolean(L, false); return 1; }
        NpcDesc desc; desc.entity = CheckEntity(L, -1); lua_pop(L, 1);
        desc.id = StringField(L, 1, "id");
        const std::string mode = StringField(L, 1, "mode", "patrol");
        desc.mode = mode == "random" ? WanderMode::Random : mode == "none" ? WanderMode::None : WanderMode::Patrol;
        lua_getfield(L, 1, "waypoints"); desc.waypoints = ParseWaypoints(L, -1); lua_pop(L, 1);
        desc.loop = BoolField(L, 1, "loop", true);
        desc.facePlayerOnAlert = BoolField(L, 1, "face_player", true);
        desc.moveSpeed = NumberField(L, 1, "speed", desc.moveSpeed);
        desc.waitMin = NumberField(L, 1, "wait_min", desc.waitMin);
        desc.waitMax = NumberField(L, 1, "wait_max", desc.waitMax);
        desc.wanderRadius = NumberField(L, 1, "wander_radius", desc.wanderRadius);
        desc.alertRadius = NumberField(L, 1, "alert_radius", desc.alertRadius);
        desc.interactRadius = NumberField(L, 1, "interact_radius", desc.interactRadius);
        lua_getfield(L, 1, "on_interact");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TFUNCTION); lua_pushlstring(L, desc.id.data(), desc.id.size());
            lua_pushvalue(L, -2); lua_rawset(L, lua_upvalueindex(2));
        }
        lua_pop(L, 1); lua_pushboolean(L, sys->Register(desc)); return 1;
    }, 2); lua_setfield(L, npc, "register");
    lua_getfield(L, npc, "__handlers");
    lua_pushcclosure(L, [](lua_State* L) -> int {
        luaL_checktype(L, 1, LUA_TSTRING); luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_pushvalue(L, 1); lua_pushvalue(L, 2); lua_rawset(L, lua_upvalueindex(1)); return 0;
    }, 1); lua_setfield(L, npc, "on_interact");
    PushFunction(L, [](lua_State* L) -> int { auto* sys = Context<NpcBindings>(L)->m_npc; const auto entity = CheckEntity(L, 1); if (sys) sys->SetPlayer(entity); return 0; }, this); lua_setfield(L, npc, "set_player");
    PushFunction(L, [](lua_State* L) -> int { auto* sys = Context<NpcBindings>(L)->m_npc; const auto entity = CheckEntity(L, 1); if (sys) sys->Unregister(entity); return 0; }, this); lua_setfield(L, npc, "unregister");
    PushFunction(L, [](lua_State* L) -> int { auto* sys = Context<NpcBindings>(L)->m_npc; if (sys) sys->Clear(); return 0; }, this); lua_setfield(L, npc, "clear");
    PushFunction(L, [](lua_State* L) -> int { const auto* sys = Context<NpcBindings>(L)->m_npc; lua_pushinteger(L, sys ? static_cast<lua_Integer>(sys->Count()) : 0); return 1; }, this); lua_setfield(L, npc, "count");
    PushFunction(L, [](lua_State* L) -> int { auto* sys = Context<NpcBindings>(L)->m_npc; lua_pushinteger(L, sys ? static_cast<lua_Integer>(sys->TryInteractNearest().Packed()) : 0); return 1; }, this); lua_setfield(L, npc, "interact");
    PushFunction(L, [](lua_State* L) -> int { auto* sys = Context<NpcBindings>(L)->m_npc; const auto entity = CheckEntity(L, 1); lua_pushboolean(L, sys && sys->TryInteract(entity)); return 1; }, this); lua_setfield(L, npc, "interact_with");
    PushFunction(L, [](lua_State* L) -> int { const auto* sys = Context<NpcBindings>(L)->m_npc; lua_pushboolean(L, sys && sys->IsAnyInteracting()); return 1; }, this); lua_setfield(L, npc, "is_interacting");
    PushFunction(L, [](lua_State* L) -> int { const auto* sys = Context<NpcBindings>(L)->m_npc; lua_pushinteger(L, sys ? static_cast<lua_Integer>(sys->InteractingNpc().Packed()) : 0); return 1; }, this); lua_setfield(L, npc, "interacting_npc");
    constexpr const char* dispatch = R"LUA(
        local npc = mye.npc
        local handlers = npc.__handlers
        local busy = npc.__busy

        -- C++ 가 상호작용 개시 시 호출. on_interact 가 없으면 false(거부) → NpcSystem 배회 유지.
        --   있으면 코루틴 실행하고 true(수락) 반환.
        function npc.__begin(id)
            local fn = handlers[id]
            if not fn then return false end
            busy[id] = true
            mye.co.start(function()
                -- pcall 로 감싸 on_interact 에러가 나도 busy 를 반드시 내린다(에러 격리).
                local ok, err = pcall(fn)
                busy[id] = false
                if not ok then
                    print("[npc] on_interact 오류(" .. tostring(id) .. "): " .. tostring(err))
                end
            end)
            return true
        end

        -- C++ busy 폴링. 명시 플래그가 없으면 false(안전).
        function npc.__busy(id)
            return busy[id] == true
        end
    )LUA";
    if (luaL_loadstring(L, dispatch) != LUA_OK || ProtectedCall(L, 0, 0) != LUA_OK) throw std::runtime_error(lua_tostring(L, -1));
    if (m_npc) {
        lua_getfield(L, npc, "__begin"); LuaReference begin(L, -1); lua_pop(L, 1);
        lua_getfield(L, npc, "__busy"); LuaReference busy(L, -1); lua_pop(L, 1);
        m_npc->SetInteractHandlers(
            [begin](const std::string& id, ecs::Entity) { return CallHandler(begin, id); },
            [busy](const std::string& id, ecs::Entity) { return CallHandler(busy, id); });
    }
}
} // namespace mye::runtime
