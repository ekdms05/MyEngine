#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"

#include "mye/anim/AnimationTypes.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/scene/Transform.h"
#include <cmath>

#include <new>

namespace mye::script {
namespace {
constexpr const char* kEntity = "mye.Entity";
struct LuaEntity {
    ecs::Entity entity{};
    ecs::World* world = nullptr;
    ecs::CommandBuffer* commands = nullptr;
    bool Valid() const { return world && world->Valid(entity); }
};
LuaEntity& Entity(lua_State* L, int index = 1) {
    return *static_cast<LuaEntity*>(luaL_checkudata(L, index, kEntity));
}
void PushEntity(lua_State* L, ecs::Entity entity, ecs::World* world, ecs::CommandBuffer* commands) {
    new (lua_newuserdatauv(L, sizeof(LuaEntity), 0)) LuaEntity{entity, world, commands};
    luaL_setmetatable(L, kEntity);
}
anim::SpriteAnimator* Animator(const LuaEntity& e) { return e.Valid() ? e.world->TryGet<anim::SpriteAnimator>(e.entity) : nullptr; }
int Valid(lua_State* L) { lua_pushboolean(L, Entity(L).Valid()); return 1; }
int Packed(lua_State* L) { lua_pushinteger(L, static_cast<lua_Integer>(Entity(L).entity.Packed())); return 1; }
int Equal(lua_State* L) { lua_pushboolean(L, Entity(L).entity == Entity(L, 2).entity); return 1; }
int Text(lua_State* L) {
    const auto e = Entity(L).entity;
    lua_pushfstring(L, "Entity(%I:%I)", static_cast<lua_Integer>(e.index), static_cast<lua_Integer>(e.generation));
    return 1;
}
int Position(lua_State* L) {
    const auto& e = Entity(L);
    const auto* t = e.Valid() ? e.world->TryGet<scene::LocalTransform>(e.entity) : nullptr;
    PushVec2(L, t ? Vec2{t->position.x, t->position.y} : Vec2{});
    return 1;
}
int SetPosition(lua_State* L) {
    const auto& e = Entity(L); const Vec2 p = ReadVec2(L, 2);
    if (auto* t = e.Valid() ? e.world->TryGet<scene::LocalTransform>(e.entity) : nullptr) {
        t->position.x = p.x; t->position.y = p.y; t->dirty = true;
    }
    return 0;
}
int Velocity(lua_State* L) {
    const auto& e = Entity(L);
    const auto* body = e.Valid() ? e.world->TryGet<phys::KinematicBody2D>(e.entity) : nullptr;
    PushVec2(L, body ? body->velocity : Vec2{}); return 1;
}
int Position3D(lua_State* L) {
    const auto& e = Entity(L);
    const auto* t = e.Valid() ? e.world->TryGet<scene::LocalTransform>(e.entity) : nullptr;
    PushVec3(L, t ? t->position : Vec3{});
    return 1;
}
int SetPosition3D(lua_State* L) {
    const auto& e = Entity(L);
    const auto p = ReadVec3(L, 2);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        return luaL_error(L, "position3d must be finite");
    if (auto* t = e.Valid() ? e.world->TryGet<scene::LocalTransform>(e.entity) : nullptr) {
        t->position = p;
        t->dirty = true;
        if (auto* body = e.world->TryGet<phys::KinematicBody3D>(e.entity)) {
            body->state = {};
            body->initialized = false;
        }
    }
    return 0;
}
int Velocity3D(lua_State* L) {
    const auto& e = Entity(L);
    const auto* b = e.Valid() ? e.world->TryGet<phys::KinematicBody3D>(e.entity) : nullptr;
    PushVec3(L, b ? b->state.velocity : Vec3{});
    return 1;
}
int OnFloor3D(lua_State* L) {
    const auto& e = Entity(L);
    const auto* b = e.Valid() ? e.world->TryGet<phys::KinematicBody3D>(e.entity) : nullptr;
    lua_pushboolean(L, b && b->state.grounded);
    return 1;
}
int HitWall3D(lua_State* L) {
    const auto& e = Entity(L);
    const auto* b = e.Valid() ? e.world->TryGet<phys::KinematicBody3D>(e.entity) : nullptr;
    lua_pushboolean(L, b && b->state.onWall);
    return 1;
}
int SetVelocity(lua_State* L) {
    const auto& e = Entity(L); const Vec2 v = ReadVec2(L, 2);
    if (auto* body = e.Valid() ? e.world->TryGet<phys::KinematicBody2D>(e.entity) : nullptr) body->velocity = v;
    return 0;
}
int HitWall(lua_State* L) {
    const auto& e = Entity(L);
    const auto* body = e.Valid() ? e.world->TryGet<phys::KinematicBody2D>(e.entity) : nullptr;
    lua_pushboolean(L, body && body->hitWall); return 1;
}
int SetBool(lua_State* L) {
    auto* a = Animator(Entity(L)); const char* name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    if (a) a->SetBool(name, lua_toboolean(L, 3) != 0); return 0;
}
int SetFloat(lua_State* L) {
    auto* a = Animator(Entity(L)); const char* name = luaL_checkstring(L, 2); const float v = static_cast<float>(luaL_checknumber(L, 3));
    if (a) a->SetFloat(name, v); return 0;
}
int SetTrigger(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = luaL_checkstring(L, 2); if (a) a->SetTrigger(name); return 0; }
int GetFloat(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = luaL_checkstring(L, 2); lua_pushnumber(L, a ? a->GetFloat(name) : 0); return 1; }
int GetBool(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = luaL_checkstring(L, 2); lua_pushboolean(L, a && a->GetBool(name)); return 1; }
int FaceMove(lua_State* L) { auto* a = Animator(Entity(L)); const Vec2 direction = ReadVec2(L, 2); if (a) a->facing = anim::Dir8FromVector(direction, a->facing); return 0; }
int FacingVector(lua_State* L) { auto* a = Animator(Entity(L)); PushVec2(L, a ? anim::Dir8Vector(a->facing) : Vec2{0, -1}); return 1; }
int FacingIndex(lua_State* L) { auto* a = Animator(Entity(L)); lua_pushinteger(L, a ? static_cast<int>(a->facing) : 0); return 1; }
int HasAnimator(lua_State* L) { lua_pushboolean(L, Animator(Entity(L)) != nullptr); return 1; }
int HasBody(lua_State* L) { const auto& e = Entity(L); lua_pushboolean(L, e.Valid() && e.world->Has<phys::KinematicBody2D>(e.entity)); return 1; }
int Destroy(lua_State* L) {
    const auto& e = Entity(L);
    if (e.Valid()) { if (e.commands) e.commands->Destroy(e.entity); else e.world->Destroy(e.entity); }
    return 0;
}
} // namespace

EcsBindingModule::EcsBindingModule(ecs::World* world) : m_world(world) {
    if (m_world) m_commands = std::make_unique<ecs::CommandBuffer>(*m_world);
}
EcsBindingModule::~EcsBindingModule() = default;
void EcsBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    const luaL_Reg methods[] = {{"is_valid", Valid}, {"packed", Packed}, {"__eq", Equal}, {"__tostring", Text}, {"get_position", Position}, {"set_position", SetPosition}, {"get_velocity", Velocity}, {"set_velocity", SetVelocity}, {"hit_wall", HitWall}, {"set_bool", SetBool}, {"set_float", SetFloat}, {"set_trigger", SetTrigger}, {"get_float", GetFloat}, {"get_bool", GetBool}, {"face_move", FaceMove}, {"facing_vector", FacingVector}, {"facing_index", FacingIndex}, {"has_animator", HasAnimator}, {"has_body", HasBody}, {"destroy", Destroy}, {nullptr, nullptr}};
    luaL_newmetatable(L, kEntity); luaL_setfuncs(L, methods, 0);
    const luaL_Reg methods3D[] = {{"get_position3d", Position3D}, {"set_position3d", SetPosition3D},
                                  {"get_velocity3d", Velocity3D}, {"is_on_floor3d", OnFloor3D},
                                  {"hit_wall3d", HitWall3D},      {nullptr, nullptr}};
    luaL_setfuncs(L, methods3D, 0);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index"); lua_pop(L, 1);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "world");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<EcsBindingModule>(L); const auto& e = Entity(L);
        lua_pushboolean(L, e.world == self->m_world && e.Valid()); return 1;
    }, this); lua_setfield(L, -2, "valid");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<EcsBindingModule>(L);
        const auto e = self->m_commands ? self->m_commands->CreateDeferred() : ecs::Entity{};
        PushEntity(L, e, self->m_world, self->m_commands.get()); return 1;
    }, this); lua_setfield(L, -2, "spawn");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<EcsBindingModule>(L); const auto& e = Entity(L);
        if (e.world == self->m_world && self->m_commands && !e.entity.IsNull()) self->m_commands->Destroy(e.entity);
        return 0;
    }, this); lua_setfield(L, -2, "destroy");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<EcsBindingModule>(L);
        PushEntity(L, CheckEntity(L, 1), self->m_world, self->m_commands.get()); return 1;
    }, this); lua_setfield(L, -2, "entity_from_packed");
}
void EcsBindingModule::FlushDeferred() { if (m_commands) m_commands->Flush(); }
} // namespace mye::script
