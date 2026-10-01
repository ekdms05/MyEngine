#include "mye/runtime/ObjectSystem.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/script/ScriptComponent.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/ScriptSystem.h"
#include "mye/script/bindings/EngineBindings.h"
#include <cmath>
#include <utility>

namespace mye::runtime {
struct ObjectSystem::Impl {
    ecs::World& world;
    phys::PhysicsWorld2D physics;
    script::ScriptRuntime lua;
    std::unique_ptr<script::EcsBindingModule> bindings;
    std::unique_ptr<script::ScriptSystem> scripts;
    std::vector<ScopedSubscription> subscriptions;
    std::vector<std::pair<ecs::Entity, ObjectEvent>> events;
    MapRequest request;
    std::string message, prompt;
    bool started = false;
    explicit Impl(ecs::World& w) : world(w) {}

    // ponytail: names are scanned only for actions; add an index if measured action cost warrants it.
    ecs::Entity Find(std::string_view name, ecs::Entity fallback) {
        if (name.empty()) return fallback;
        auto result = ecs::Entity::Null();
        world.Query<scene::ObjectName>().Each([&](ecs::Entity e, const scene::ObjectName& n) {
            if (n.value == name) result = e;
        });
        return result;
    }
    Vec2 Position(ecs::Entity e) const {
        if (auto* t = world.TryGet<scene::WorldTransform>(e)) return {t->matrix.m[3][0], t->matrix.m[3][1]};
        return {};
    }
    int Floor(ecs::Entity e) const {
        const auto* floor = world.TryGet<scene::FloorLevel>(e);
        return floor ? floor->level : 0;
    }
};

ObjectSystem::ObjectSystem(ecs::World& world) : m_impl(std::make_unique<Impl>(world)) {}
ObjectSystem::~ObjectSystem() {
    auto& s = *m_impl;
    s.subscriptions.clear();
    std::vector<ecs::Entity> scripts;
    s.world.Query<script::ScriptComponent>().Each([&](ecs::Entity e, script::ScriptComponent&) { scripts.push_back(e); });
    if (s.scripts) for (auto e : scripts) s.scripts->CallOnEntity(e, "on_destroy", script::LuaReference{});
    s.scripts.reset(); // releases tracked Lua references while the VM is alive
    for (auto e : scripts) s.world.Remove<script::ScriptComponent>(e);
    s.lua.Shutdown();
}
Expected<void, Error> ObjectSystem::Initialize() {
    auto& s = *m_impl;
    auto valid = ValidateObjectComponents(s.world);
    if (!valid) return valid.GetError();
    s.lua.Initialize({}, s.world.Events(), nullptr);
    s.bindings = std::make_unique<script::EcsBindingModule>(&s.world);
    s.lua.AddBindingModule(std::make_unique<script::MathBindingModule>());
    s.lua.AddBindingModule(s.bindings.get());
    s.scripts = std::make_unique<script::ScriptSystem>(s.lua, s.world, s.world.Events(), nullptr);
    s.scripts->RegisterComponent();
    std::vector<ecs::Entity> scriptedObjects;
    s.world.Query<ObjectBehavior>().Each([&](ecs::Entity e, const ObjectBehavior& b) {
        if (!b.luaSource.empty()) scriptedObjects.push_back(e);
    });
    for (auto e : scriptedObjects) s.world.Add<script::ScriptComponent>(e).inlineSource = s.world.TryGet<ObjectBehavior>(e)->luaSource;
    if (auto* bus = s.world.Events()) {
        s.subscriptions.emplace_back(*bus, bus->Subscribe<phys::TriggerEnterEvent>([this](const auto& e) {
            if (const auto* controller = m_impl->world.TryGet<CharacterController2D>(e.other); controller && controller->enabled) m_impl->events.emplace_back(e.trigger, ObjectEvent::TriggerEnter);
            return false;
        }));
        s.subscriptions.emplace_back(*bus, bus->Subscribe<phys::TriggerExitEvent>([this](const auto& e) {
            if (const auto* controller = m_impl->world.TryGet<CharacterController2D>(e.other); controller && controller->enabled) m_impl->events.emplace_back(e.trigger, ObjectEvent::TriggerExit);
            return false;
        }));
        s.subscriptions.emplace_back(*bus, bus->Subscribe<script::ScriptErrorEvent>([this](const auto& e) {
            m_impl->message = "Lua: " + e.message; return false;
        }));
    }
    s.scripts->EnsureInstances();
    return {};
}

void ObjectSystem::Dispatch(ecs::Entity object, ObjectEvent event) {
    auto& s = *m_impl;
    if (!s.world.Valid(object)) return;
    if (const auto* portal = s.world.TryGet<ScenePortal>(object)) {
        if ((portal->onInteract && event == ObjectEvent::Interact) || (!portal->onInteract && event == ObjectEvent::TriggerEnter))
            s.request = {portal->scenePath, portal->spawnName};
    }
    const auto* behavior = s.world.TryGet<ObjectBehavior>(object);
    if (!behavior) return;
    for (const auto& connection : behavior->connections) {
        if (connection.event != event) continue;
        const auto target = s.Find(connection.target, object);
        switch (connection.action) {
        case ObjectAction::Message: s.message = connection.text; MYE_LOG_INFO("Objects", "{}", s.message); break;
        case ObjectAction::SetVisible:
            if (auto* sprite = s.world.TryGet<scene::SpriteRenderer>(target)) sprite->visible = connection.visible;
            break;
        case ObjectAction::MoveTo:
            if (auto* transform = s.world.TryGet<scene::LocalTransform>(target)) {
                // Explicit position action; ordinary movement still goes through the kinematic body.
                transform->position.x = connection.x; transform->position.y = connection.y; transform->dirty = true;
            }
            break;
        case ObjectAction::ChangeMap: s.request = {connection.text, connection.target}; break;
        case ObjectAction::LuaCallback:
            s.scripts->CallOnEntity(target, connection.text, script::LuaReference{}); break;
        }
    }
    if (event == ObjectEvent::Interact) s.scripts->CallOnEntity(object, "on_interact", script::LuaReference{});
}

void ObjectSystem::Tick(float dt, Vec2 movement, bool interact) {
    auto& s = *m_impl;
    if (!s.scripts || !std::isfinite(dt) || dt <= 0) return;
    if (!std::isfinite(movement.x) || !std::isfinite(movement.y)) movement = {};
    const float length = std::sqrt(movement.x * movement.x + movement.y * movement.y);
    if (length > 1) movement = movement / length;
    s.scripts->EnsureInstances();
    s.scripts->Update(dt);
    s.bindings->FlushDeferred();
    if (!s.started) {
        s.started = true;
        std::vector<ecs::Entity> objects;
        s.world.Query<ObjectBehavior>().Each([&](ecs::Entity e, ObjectBehavior&) { objects.push_back(e); });
        for (auto e : objects) Dispatch(e, ObjectEvent::Start);
    }
    s.world.Query<CharacterController2D, phys::KinematicBody2D>().Each([&](ecs::Entity, const CharacterController2D& c, phys::KinematicBody2D& b) {
        b.velocity = c.enabled && std::isfinite(c.speed) && c.speed >= 0 && c.speed <= 100 ? movement * c.speed : Vec2{};
    });
    scene::UpdateWorldTransforms(s.world);
    s.physics.Step(s.world, s.world.Events(), dt);
    for (const auto& [e, event] : s.events) Dispatch(e, event);
    s.events.clear();
    scene::UpdateWorldTransforms(s.world);
    s.prompt.clear();
    auto nearest = ecs::Entity::Null();
    float nearestDistance = 10000.0f;
    s.world.Query<CharacterController2D, phys::KinematicBody2D>().Each([&](ecs::Entity player, CharacterController2D& c, const phys::KinematicBody2D& body) {
        if (!c.enabled) return;
        if (auto* animator = s.world.TryGet<anim::SpriteAnimator>(player)) {
            const bool moving = body.lastMove.x * body.lastMove.x + body.lastMove.y * body.lastMove.y > 0.000001f;
            const auto& ref = moving ? c.walkAnimation : c.idleAnimation;
            if (ref.guid.IsValid() && c.requestedMotion != ref.guid) {
                c.requestedMotion = ref.guid;
                animator->animation = ref; animator->sheet = nullptr; animator->directClip = nullptr; animator->cursor = {}; animator->started = false;
            }
        }
        const auto position = s.Position(player);
        s.world.Query<InteractionTarget>().Each([&](ecs::Entity object, const InteractionTarget& target) {
            if (!target.enabled || object == player || s.Floor(object) != s.Floor(player)) return;
            const auto delta = s.Position(object) - position;
            const float distance = delta.x * delta.x + delta.y * delta.y;
            if (distance <= target.radius * target.radius && (distance < nearestDistance || (distance == nearestDistance && object.index < nearest.index))) {
                nearestDistance = distance; nearest = object; s.prompt = target.prompt;
            }
        });
    });
    if (interact && s.world.Valid(nearest)) Dispatch(nearest, ObjectEvent::Interact);
    s.bindings->FlushDeferred();
}
MapRequest ObjectSystem::TakeMapRequest() { return std::exchange(m_impl->request, {}); }
std::string_view ObjectSystem::Message() const { return m_impl->message; }
std::string_view ObjectSystem::Prompt() const { return m_impl->prompt; }
} // namespace mye::runtime
