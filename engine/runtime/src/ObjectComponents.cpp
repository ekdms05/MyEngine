#include "mye/runtime/ObjectComponents.h"
#include "mye/ecs/World.h"
#include "mye/core/JsonFile.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/phys/Collision.h"
#include <cmath>
#include <filesystem>
#include <unordered_set>

using namespace mye::runtime;
template<> void mye::refl::Reflect(TypeBuilder<CharacterController2D>& b) {
    b.Version(1).Field("enabled", &CharacterController2D::enabled).Field("speed", &CharacterController2D::speed)
        .Field("idleAnimation", &CharacterController2D::idleAnimation).Field("walkAnimation", &CharacterController2D::walkAnimation);
}
template<> void mye::refl::Reflect(TypeBuilder<InteractionTarget>& b) {
    b.Version(1).Field("enabled", &InteractionTarget::enabled).Field("radius", &InteractionTarget::radius).Field("prompt", &InteractionTarget::prompt);
}
template<> void mye::refl::Reflect(TypeBuilder<ScenePortal>& b) {
    b.Version(1).Field("scenePath", &ScenePortal::scenePath).Field("spawnName", &ScenePortal::spawnName).Field("onInteract", &ScenePortal::onInteract);
}
template<> void mye::refl::Reflect(EnumBuilder<ObjectEvent>& b) {
    b.Value("Start", ObjectEvent::Start).Value("Interact", ObjectEvent::Interact)
        .Value("TriggerEnter", ObjectEvent::TriggerEnter).Value("TriggerExit", ObjectEvent::TriggerExit);
}
template<> void mye::refl::Reflect(EnumBuilder<ObjectAction>& b) {
    b.Value("Message", ObjectAction::Message).Value("SetVisible", ObjectAction::SetVisible)
        .Value("MoveTo", ObjectAction::MoveTo).Value("ChangeMap", ObjectAction::ChangeMap).Value("LuaCallback", ObjectAction::LuaCallback);
}
template<> void mye::refl::Reflect(TypeBuilder<ObjectConnection>& b) {
    b.Version(1).Field("event", &ObjectConnection::event).Field("action", &ObjectConnection::action)
        .Field("target", &ObjectConnection::target).Field("text", &ObjectConnection::text)
        .Field("x", &ObjectConnection::x).Field("y", &ObjectConnection::y).Field("visible", &ObjectConnection::visible);
}
template<> void mye::refl::Reflect(TypeBuilder<ObjectBehavior>& b) {
    b.Version(1).Field("connections", &ObjectBehavior::connections).Field("luaSource", &ObjectBehavior::luaSource);
}

namespace mye::runtime {
void RegisterObjectComponents(ecs::World& world) {
    (void)refl::GetType<CharacterController2D>(); (void)refl::GetType<InteractionTarget>();
    (void)refl::GetType<ScenePortal>(); (void)refl::GetType<ObjectBehavior>();
    world.RegisterComponent<CharacterController2D>("CharacterController2D");
    world.RegisterComponent<InteractionTarget>("InteractionTarget");
    world.RegisterComponent<ScenePortal>("ScenePortal");
    world.RegisterComponent<ObjectBehavior>("ObjectBehavior");
}
Expected<void, Error> ValidateObjectComponents(ecs::World& world) {
    std::string error;
    std::unordered_set<std::string> names;
    world.Query<scene::ObjectName>().Each([&](ecs::Entity, const scene::ObjectName& n) {
        if (n.value.size() > 256 || (!n.value.empty() && !names.insert(n.value).second)) error = "Object names must be unique and at most 256 bytes";
    });
    const auto pathValid = [](const std::string& text) {
        const auto path = Utf8Path(text);
        if (text.find('\0') != std::string::npos || text.find('\\') != std::string::npos || path.is_absolute() || path.extension() != ".scene" || !text.starts_with("assets/")) return false;
        for (const auto& part : path) if (part == "..") return false;
        return true;
    };
    world.Query<scene::LocalTransform>().Each([&](ecs::Entity, const scene::LocalTransform& t) {
        if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) || !std::isfinite(t.position.z)) error = "Object position must be finite";
    });
    world.Query<phys::Collider2D>().Each([&](ecs::Entity, const phys::Collider2D& c) {
        if ((c.shape.kind != phys::ShapeKind::AABB && c.shape.kind != phys::ShapeKind::Circle) ||
            !std::isfinite(c.shape.half.x) || !std::isfinite(c.shape.half.y) || c.shape.half.x <= 0 || c.shape.half.y <= 0 ||
            !std::isfinite(c.offset.x) || !std::isfinite(c.offset.y)) error = "Collider dimensions must be finite and positive";
    });
    world.Query<phys::KinematicBody2D>().Each([&](ecs::Entity, const phys::KinematicBody2D& b) {
        if (!std::isfinite(b.skin) || b.skin < 0 || b.skin > 1 || b.maxSlideIters < 1 || b.maxSlideIters > 16) error = "Invalid kinematic body settings";
    });
    int localControllers = 0;
    world.Query<CharacterController2D>().Each([&](ecs::Entity e, const CharacterController2D& c) {
        if (c.enabled) ++localControllers;
        if (const auto* parent = world.TryGet<scene::Parent>(e); parent && !parent->parent.IsNull()) error = "Character controller must be a scene root for world XY physics";
        if (!std::isfinite(c.speed) || c.speed < 0 || c.speed > 100 || !world.Has<phys::KinematicBody2D>(e) || !world.Has<phys::Collider2D>(e) || !world.Has<scene::LocalTransform>(e)) error = "Controller needs transform, body, collider and a speed in [0,100]";
    });
    if (localControllers > 1) error = "A Play scene supports one enabled local character controller";
    world.Query<InteractionTarget>().Each([&](ecs::Entity e, const InteractionTarget& t) {
        if (!std::isfinite(t.radius) || t.radius <= 0 || t.radius > 100 || !world.Has<scene::LocalTransform>(e)) error = "Interaction target needs a transform and radius in (0,100]";
    });
    world.Query<ScenePortal>().Each([&](ecs::Entity e, const ScenePortal& p) {
        if (!pathValid(p.scenePath) || p.spawnName.empty()) error = "Portal needs a project assets .scene path and named spawn";
        const auto* collider = world.TryGet<phys::Collider2D>(e);
        if (p.onInteract ? !world.Has<InteractionTarget>(e) : !collider || !collider->isTrigger)
            error = "Portal needs InteractionTarget for E or a trigger collider for automatic entry";
    });
    world.Query<ObjectBehavior>().Each([&](ecs::Entity, const ObjectBehavior& b) {
        if (b.connections.size() > 64 || b.luaSource.size() > 65536) error = "Object behavior exceeds 64 connections or 64 KiB of Lua";
        for (const auto& c : b.connections) {
            if (c.event < ObjectEvent::Start || c.event > ObjectEvent::TriggerExit || c.action < ObjectAction::Message || c.action > ObjectAction::LuaCallback || !std::isfinite(c.x) || !std::isfinite(c.y)) error = "Invalid event/action connection";
            if (!c.target.empty() && !names.contains(c.target) && c.action != ObjectAction::ChangeMap) error = "Connection target object does not exist";
            if (c.action == ObjectAction::LuaCallback && (c.text.empty() || c.text.size() > 128)) error = "Lua action needs a callback name of 1..128 bytes";
            if (c.action == ObjectAction::ChangeMap && (!pathValid(c.text) || c.target.empty())) error = "Map connection needs a scene path and spawn name";
        }
    });
    return error.empty() ? Expected<void, Error>{} : Expected<void, Error>{Error{error, 1}};
}
} // namespace mye::runtime
