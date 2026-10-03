#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"

#include "mye/anim/AnimationTypes.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera2D.h"
#include <cmath>
#include <limits>

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
scene::Camera2D& GameCamera(lua_State* L) {
    const auto& e = Entity(L);
    auto* camera = e.Valid() ? e.world->TryGet<scene::Camera2D>(e.entity) : nullptr;
    if (!camera) luaL_error(L, "a valid Camera2D entity is required");
    return *camera;
}
int SetCameraZoom(lua_State* L) {
    auto& camera = GameCamera(L);
    const float zoom = static_cast<float>(luaL_checknumber(L, 2));
    if (!std::isfinite(zoom) || zoom < .25f || zoom > 8)
        return luaL_error(L, "camera zoom must be finite and in [0.25,8]");
    camera.zoom = zoom;
    return 0;
}
int ShakeCamera(lua_State* L) {
    auto& camera = GameCamera(L);
    const float amplitude = static_cast<float>(luaL_checknumber(L, 2));
    const float duration = static_cast<float>(luaL_checknumber(L, 3));
    if (!std::isfinite(amplitude) || !std::isfinite(duration) || amplitude < 0 || amplitude > 10 ||
        duration < 0 || duration > 60)
        return luaL_error(L, "camera shake requires amplitude in [0,10] units and duration in [0,60] seconds");
    camera.view.AddShake(amplitude, duration);
    return 0;
}
int ConvertCameraPoint(lua_State* L, bool toScreen) {
    GameCamera(L);
    const auto& e = Entity(L);
    const Vec2 point = ReadVec2(L, 2);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return luaL_error(L, "camera point must be finite");
    bool failed = false;
    {
        auto resolved = scene::ResolveGameCamera2D(*e.world, e.entity);
        failed = !resolved;
        if (failed) {
            const auto& message = resolved.GetError().message;
            lua_pushlstring(L, message.data(), message.size());
        } else {
            const auto result = toScreen ? resolved.Value().WorldToScreen(point) : resolved.Value().ScreenToWorld(point);
            if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
                lua_pushliteral(L, "camera conversion overflow");
                failed = true;
            } else PushVec2(L, result);
        }
    }
    // Expected/Error must be destroyed before Lua's error unwinds the C boundary.
    if (failed) return lua_error(L);
    return 1;
}
int WorldToScreen(lua_State* L) { return ConvertCameraPoint(L, true); }
int ScreenToWorld(lua_State* L) { return ConvertCameraPoint(L, false); }
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
const char* AnimationParameter(lua_State* L, anim::SpriteAnimator* animator, anim::ParamType type, bool readBool = false) {
    luaL_checktype(L, 2, LUA_TSTRING);
    size_t length = 0;
    const char* name = luaL_checklstring(L, 2, &length);
    if (!length || length > 64) luaL_error(L, "animation parameter name must have 1..64 bytes");
    for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(name[i]) < 32 || name[i] == 127)
            luaL_error(L, "animation parameter name contains a control character");
    if (animator && animator->stateMachine.guid.IsValid()) {
        const auto* parameter = animator->FindParam(name);
        if (!parameter || (parameter->type != type && !(readBool && parameter->type == anim::ParamType::Trigger)))
            luaL_error(L, "animation parameter is undeclared or has a different type: %s", name);
    }
    return name;
}
int SetBool(lua_State* L) {
    auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Bool);
    luaL_checktype(L, 3, LUA_TBOOLEAN);
    if (a) a->SetBool(name, lua_toboolean(L, 3) != 0); return 0;
}
int SetFloat(lua_State* L) {
    auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Float);
    const auto value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return luaL_error(L, "animation float must be finite and fit in a float");
    const float v = static_cast<float>(value);
    if (a) a->SetFloat(name, v); return 0;
}
int SetTrigger(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Trigger); if (a) a->SetTrigger(name); return 0; }
int ResetTrigger(lua_State* L) {
    auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Trigger);
    if (a) if (auto* parameter = a->FindParam(name); parameter && parameter->type == anim::ParamType::Trigger)
        parameter->value = 0;
    return 0;
}
int AnimationState(lua_State* L) {
    const auto* a = Animator(Entity(L));
    if (!a || !a->machine || a->currentState < 0 || a->currentState >= static_cast<int>(a->machine->states.size())) {
        lua_pushnil(L); return 1;
    }
    const auto& name = a->machine->states[static_cast<size_t>(a->currentState)].name;
    lua_pushlstring(L, name.data(), name.size()); return 1;
}
int GetFloat(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Float); lua_pushnumber(L, a ? a->GetFloat(name) : 0); return 1; }
int GetBool(lua_State* L) { auto* a = Animator(Entity(L)); const char* name = AnimationParameter(L, a, anim::ParamType::Bool, true); lua_pushboolean(L, a && a->GetBool(name)); return 1; }
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
    const luaL_Reg cameraMethods[] = {{"set_camera_zoom", SetCameraZoom}, {"shake_camera", ShakeCamera},
                                     {"world_to_screen", WorldToScreen}, {"screen_to_world", ScreenToWorld},
                                     {nullptr, nullptr}};
    luaL_setfuncs(L, cameraMethods, 0);
    const luaL_Reg actionMethods[] = {{"reset_trigger", ResetTrigger}, {"get_animation_state", AnimationState},
                                     {nullptr, nullptr}};
    luaL_setfuncs(L, actionMethods, 0);
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
