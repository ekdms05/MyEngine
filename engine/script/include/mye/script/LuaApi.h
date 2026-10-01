#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <lua.hpp>
#include <memory>

namespace mye::script {

// Registry values may outlive an ECS world or an explicit VM shutdown. The token
// is invalidated before lua_close, so late destruction never touches a dead VM.
struct LuaLifetime { lua_State* state = nullptr; };
void InstallLuaLifetime(lua_State* state, const std::shared_ptr<LuaLifetime>& lifetime);

class LuaReference {
public:
    LuaReference() = default;
    LuaReference(lua_State* state, int index);
    ~LuaReference();
    LuaReference(const LuaReference& other);
    LuaReference& operator=(const LuaReference& other);
    LuaReference(LuaReference&& other) noexcept;
    LuaReference& operator=(LuaReference&& other) noexcept;

    bool Valid() const;
    lua_State* State() const;
    void Push(lua_State* target = nullptr) const;
    void Reset();

private:
    std::shared_ptr<LuaLifetime> m_lifetime;
    int m_ref = LUA_NOREF;
};

class LuaStackGuard {
public:
    explicit LuaStackGuard(lua_State* state) : m_state(state), m_top(lua_gettop(state)) {}
    ~LuaStackGuard() { lua_settop(m_state, m_top); }
    LuaStackGuard(const LuaStackGuard&) = delete;
    LuaStackGuard& operator=(const LuaStackGuard&) = delete;
private:
    lua_State* m_state;
    int m_top;
};

void EnsureTable(lua_State* state, int parent, const char* key);
void PushFunction(lua_State* state, lua_CFunction function, void* context = nullptr);
template<class T> T* Context(lua_State* state) {
    return static_cast<T*>(lua_touserdata(state, lua_upvalueindex(1)));
}
int ProtectedCall(lua_State* state, int arguments, int results);
ecs::Entity CheckEntity(lua_State* state, int index);
void PushVec2(lua_State* state, const Vec2& value);
void PushVec3(lua_State* state, const Vec3& value);
Vec2 ReadVec2(lua_State* state, int index);
Vec3 ReadVec3(lua_State* state, int index);

} // namespace mye::script
