#include "mye/script/LuaApi.h"

#include <new>
#include <utility>

namespace mye::script {
namespace {
char g_lifetimeKey;

int Traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    luaL_traceback(state, state, message ? message : "Lua error (non-string value)", 1);
    return 1;
}
}

void InstallLuaLifetime(lua_State* state, const std::shared_ptr<LuaLifetime>& lifetime) {
    auto* storage = static_cast<std::shared_ptr<LuaLifetime>*>(
        lua_newuserdatauv(state, sizeof(std::shared_ptr<LuaLifetime>), 0));
    new (storage) std::shared_ptr<LuaLifetime>(lifetime);
    if (luaL_newmetatable(state, "mye.vm_lifetime")) {
        lua_pushcfunction(state, [](lua_State* s) -> int {
            static_cast<std::shared_ptr<LuaLifetime>*>(lua_touserdata(s, 1))->~shared_ptr();
            return 0;
        });
        lua_setfield(state, -2, "__gc");
    }
    lua_setmetatable(state, -2);
    lua_rawsetp(state, LUA_REGISTRYINDEX, &g_lifetimeKey);
}

LuaReference::LuaReference(lua_State* state, int index) {
    if (!state || lua_isnoneornil(state, index)) return;
    index = lua_absindex(state, index);
    lua_rawgetp(state, LUA_REGISTRYINDEX, &g_lifetimeKey);
    auto* lifetime = static_cast<std::shared_ptr<LuaLifetime>*>(lua_touserdata(state, -1));
    if (lifetime) m_lifetime = *lifetime;
    lua_pop(state, 1);
    if (!m_lifetime || !m_lifetime->state) return;
    lua_pushvalue(state, index);
    m_ref = luaL_ref(state, LUA_REGISTRYINDEX);
}

LuaReference::~LuaReference() { Reset(); }
LuaReference::LuaReference(const LuaReference& other) {
    if (other.Valid()) {
        m_lifetime = other.m_lifetime;
        other.Push();
        m_ref = luaL_ref(State(), LUA_REGISTRYINDEX);
    }
}
LuaReference& LuaReference::operator=(const LuaReference& other) {
    if (this != &other) { LuaReference copy(other); *this = std::move(copy); }
    return *this;
}
LuaReference::LuaReference(LuaReference&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_ref(std::exchange(other.m_ref, LUA_NOREF)) {}
LuaReference& LuaReference::operator=(LuaReference&& other) noexcept {
    if (this != &other) {
        Reset();
        m_lifetime = std::move(other.m_lifetime);
        m_ref = std::exchange(other.m_ref, LUA_NOREF);
    }
    return *this;
}
bool LuaReference::Valid() const { return State() && m_ref >= 0; }
lua_State* LuaReference::State() const { return m_lifetime ? m_lifetime->state : nullptr; }
void LuaReference::Push(lua_State* target) const {
    if (!target) target = State();
    if (!target) return;
    if (!Valid()) { lua_pushnil(target); return; }
    // All coroutine stacks share their VM registry; foreign VMs must not see an
    // unrelated value with the same registry integer.
    lua_rawgetp(target, LUA_REGISTRYINDEX, &g_lifetimeKey);
    auto* lifetime = static_cast<std::shared_ptr<LuaLifetime>*>(lua_touserdata(target, -1));
    const bool sameVm = lifetime && lifetime->get() == m_lifetime.get();
    lua_pop(target, 1);
    if (sameVm) lua_rawgeti(target, LUA_REGISTRYINDEX, m_ref);
    else lua_pushnil(target);
}
void LuaReference::Reset() {
    if (Valid()) luaL_unref(State(), LUA_REGISTRYINDEX, m_ref);
    m_ref = LUA_NOREF;
    m_lifetime.reset();
}
void EnsureTable(lua_State* state, int parent, const char* key) {
    parent = lua_absindex(state, parent);
    lua_getfield(state, parent, key);
    if (lua_istable(state, -1)) return;
    lua_pop(state, 1);
    lua_newtable(state);
    lua_pushvalue(state, -1);
    lua_setfield(state, parent, key);
}
void PushFunction(lua_State* state, lua_CFunction function, void* context) {
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, function, 1);
}
int ProtectedCall(lua_State* state, int arguments, int results) {
    const int function = lua_gettop(state) - arguments;
    lua_pushcfunction(state, Traceback);
    lua_insert(state, function);
    const int status = lua_pcall(state, arguments, results, function);
    lua_remove(state, function);
    return status;
}
ecs::Entity CheckEntity(lua_State* state, int index) {
    const auto packed = luaL_checkinteger(state, index);
    luaL_argcheck(state, packed >= 0, index, "entity handle must be nonnegative");
    return ecs::Entity::FromPacked(static_cast<uint64_t>(packed));
}
} // namespace mye::script
