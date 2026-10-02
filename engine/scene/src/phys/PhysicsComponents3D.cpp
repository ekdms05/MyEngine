#include "mye/phys/PhysicsComponents3D.h"
#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"
#include <cmath>

namespace mye::phys {
Expected<void, Error> GatherPhysicsWorld3D(ecs::World& world, PhysicsWorld3D& physics) {
    physics.Clear();
    std::string error;
    world.Query<Collider3D, scene::WorldTransform>().Each(
        [&](ecs::Entity e, const auto& c, const auto& transform) {
            if (!c.enabled || world.Has<KinematicBody3D>(e)) return;
            const auto& m = transform.matrix;
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    if (!std::isfinite(m.m[row][col]) || (row != col && std::abs(m.m[row][col]) > 1e-5f))
                        error = "3D box/ramp colliders require axis-aligned transforms";
            const Vec3 scale{m.m[0][0], m.m[1][1], m.m[2][2]};
            if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0) error = "3D collider scale must be positive";
            Solid3D solid;
            solid.id = e.Packed();
            solid.shape = c.shape;
            solid.trigger = c.isTrigger;
            solid.box.center = {m.m[3][0] + c.offset.x * scale.x, m.m[3][1] + c.offset.y * scale.y,
                                m.m[3][2] + c.offset.z * scale.z};
            solid.box.half = {c.half.x * scale.x, c.half.y * scale.y, c.half.z * scale.z};
            if (auto added = physics.Add(solid); !added) error = added.GetError().message;
        });
    if (!error.empty()) {
        physics.Clear();
        return Error{error, 1};
    }
    return {};
}
Expected<MotionSettings3D, Error> CharacterSettings3D(ecs::World& world, ecs::Entity e) {
    const auto* collider = world.TryGet<Collider3D>(e);
    const auto* body = world.TryGet<KinematicBody3D>(e);
    const auto* pose = world.TryGet<scene::LocalTransform>(e);
    const auto* parent = world.TryGet<scene::Parent>(e);
    if (!collider || !body || !pose || !collider->enabled || collider->isTrigger ||
        collider->shape != Shape3D::Box || (parent && !parent->parent.IsNull()) ||
        !std::isfinite(pose->rotation.x) || !std::isfinite(pose->rotation.y) ||
        !std::isfinite(pose->rotation.z) || !std::isfinite(pose->rotation.w) ||
        std::abs(pose->rotation.x) > 1e-5f || std::abs(pose->rotation.y) > 1e-5f ||
        std::abs(pose->rotation.z) > 1e-5f || std::abs(std::abs(pose->rotation.w) - 1) > 1e-5f ||
        !std::isfinite(pose->scale.x) || !std::isfinite(pose->scale.y) || !std::isfinite(pose->scale.z) ||
        pose->scale.x <= 0 || pose->scale.y <= 0 || pose->scale.z <= 0)
        return Error{
            "3D character needs an unrotated root, positive scale, enabled box collider and kinematic body",
            1};
    auto settings = body->settings;
    settings.half = {collider->half.x * pose->scale.x, collider->half.y * pose->scale.y,
                     collider->half.z * pose->scale.z};
    settings.offset = {collider->offset.x * pose->scale.x, collider->offset.y * pose->scale.y,
                       collider->offset.z * pose->scale.z};
    if (auto valid = ValidateMotionSettings3D(settings); !valid) return valid.GetError();
    return settings;
}
} // namespace mye::phys
