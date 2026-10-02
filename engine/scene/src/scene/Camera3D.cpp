#include "mye/scene/Camera3D.h"
#include "mye/ecs/World.h"
#include "mye/phys/PhysicsWorld3D.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include <algorithm>
#include <cmath>

namespace mye::scene {
namespace {
bool ValidOrbit(const Camera3D& c) {
    return std::isfinite(c.yawDegrees) && std::isfinite(c.pitchDegrees) && std::abs(c.pitchDegrees) < 89 &&
           std::isfinite(c.distance) && c.distance > .2f && c.distance <= 100 &&
           std::isfinite(c.rotationSpeed) && c.rotationSpeed >= 0 && c.rotationSpeed <= 720 &&
           std::isfinite(c.mouseSensitivity) && c.mouseSensitivity >= 0 && c.mouseSensitivity <= 10 &&
           std::isfinite(c.collisionMargin) && c.collisionMargin > 0 && c.collisionMargin <= 1;
}
Vec3 OrbitDirection(const Camera3D& c) {
    const float yaw = c.yawDegrees * kPi / 180, pitch = c.pitchDegrees * kPi / 180;
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}
} // namespace
Expected<void, Error> UpdateGameCamera(ecs::World& world, const phys::PhysicsWorld3D& physics, float dt,
                                       float axis, float mouseX) {
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(axis) || !std::isfinite(mouseX))
        return Error{"Camera input must be finite with positive dt", 1};
    std::string error;
    world.Query<Camera3D>().Each([&](ecs::Entity, const auto& c) {
        if (c.current && c.orbitEnabled && !ValidOrbit(c)) error = "Invalid orbit camera settings";
    });
    if (!error.empty()) return Error{error, 1};
    world.Query<Camera3D>().Each([&](ecs::Entity, Camera3D& c) {
        if (!c.current || !c.orbitEnabled) return;
        const double yaw = c.yawDegrees + double(std::clamp(axis, -1.0f, 1.0f)) * c.rotationSpeed * dt +
                           double(mouseX) * c.mouseSensitivity;
        c.yawDegrees = static_cast<float>(std::fmod(yaw, 360.0));
        if (c.yawDegrees < 0) c.yawDegrees += 360;
        Vec3 target = c.target;
        world.Query<ObjectName, WorldTransform>().Each([&](ecs::Entity, const auto& name, const auto& pose) {
            if (!c.followTarget.empty() && name.value == c.followTarget)
                target = target + Vec3{pose.matrix.m[3][0], pose.matrix.m[3][1], pose.matrix.m[3][2]};
        });
        // Godot SpringArm3D's cast-and-shorten behavior, using the shared collision world.
        const Vec3 motion = OrbitDirection(c) * c.distance;
        const auto hit =
            physics.Cast({target, {c.collisionMargin, c.collisionMargin, c.collisionMargin}}, motion);
        c.resolvedDistance = hit ? std::max(.05f, c.distance * hit->fraction - .01f) : c.distance;
    });
    return {};
}
Vec2 CameraRelativeMovement(ecs::World& world, Vec2 movement) {
    float yaw = 0;
    world.Query<Camera3D, WorldTransform>().Each([&](ecs::Entity, const auto& c, const auto& pose) {
        if (!c.current) return;
        if (c.orbitEnabled) yaw = c.yawDegrees * kPi / 180;
        else {
            // Follow offsets cancel out: target and eye share the same followed origin.
            const Vec3 direction =
                c.target - Vec3{pose.matrix.m[3][0], pose.matrix.m[3][1], pose.matrix.m[3][2]};
            yaw = std::atan2(-direction.x, direction.z);
        }
    });
    return {movement.x * std::cos(yaw) - movement.y * std::sin(yaw),
            movement.x * std::sin(yaw) + movement.y * std::cos(yaw)};
}
Expected<render::HybridViewInfo, Error> BuildGameView(ecs::World& world,
    const render::Camera2D& fallback, uint32_t width, uint32_t height) {
    const Camera3D* active = nullptr;
    ecs::Entity entity{};
    unsigned count = 0;
    world.Query<Camera3D>().Each([&](ecs::Entity e, const Camera3D& camera) {
        if (camera.current) { active = &camera; entity = e; ++count; }
    });
    if (count == 0) return render::HybridRenderer::MakeViewInfo(fallback);
    if (count != 1) return Error{"Camera3D: exactly one current camera is allowed", 1};
    const auto fail = [&](std::string reason) -> Expected<render::HybridViewInfo, Error> {
        const auto* name = world.TryGet<ObjectName>(entity);
        return Error{"Camera3D '" + (name ? name->value : std::to_string(entity.index)) + "': " + reason, 1};
    };
    if (width == 0 || height == 0 || !std::isfinite(active->fovDegrees) ||
        active->fovDegrees < 1 || active->fovDegrees > 179 ||
        !std::isfinite(active->nearPlane) || !std::isfinite(active->farPlane) ||
        active->nearPlane <= 0 || active->farPlane <= active->nearPlane)
        return fail("require FOV 1..179 degrees and 0 < near < far");
    const auto* transform = world.TryGet<WorldTransform>(entity);
    if (!transform) return fail("a world transform is required");
    Vec3 eye{transform->matrix.m[3][0], transform->matrix.m[3][1], transform->matrix.m[3][2]};
    Vec3 target = active->target;
    if (!active->followTarget.empty()) {
        unsigned matches = 0;
        Vec3 origin{};
        world.Query<ObjectName, WorldTransform>().Each([&](ecs::Entity, const auto& name, const auto& pose) {
            if (name.value == active->followTarget) {
                ++matches; origin = {pose.matrix.m[3][0], pose.matrix.m[3][1], pose.matrix.m[3][2]};
            }
        });
        if (matches != 1) return fail("followTarget must name exactly one transformed object: " + active->followTarget);
        eye = eye + origin; target = target + origin;
    }
    if (active->orbitEnabled) {
        if (!ValidOrbit(*active) || !std::isfinite(active->resolvedDistance) || active->resolvedDistance < 0)
            return fail("invalid orbit camera settings");
        eye = target + OrbitDirection(*active) *
                           (active->resolvedDistance > 0 ? active->resolvedDistance : active->distance);
    }
    const auto finite = [](Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const Vec3 direction = target - eye;
    const float lengthSquared = Vec3::Dot(direction, direction);
    if (!finite(eye) || !finite(target) || !finite(direction) || !std::isfinite(lengthSquared) || lengthSquared < 1e-8f ||
        direction.x * direction.x + direction.z * direction.z < 1e-8f)
        return fail("eye and target must be finite, distinct and not parallel to +Y");
    auto view = render::HybridRenderer::MakeViewInfo(fallback);
    view.geometryDepth = true;
    view.viewportWidth = width; view.viewportHeight = height;
    view.view = Mat4::LookAtLH(eye, target, {0, 1, 0});
    view.proj = Mat4::PerspectiveLH(active->fovDegrees * (3.14159265358979323846f / 180.0f),
        static_cast<float>(width) / height, active->nearPlane, active->farPlane);
    view.viewProj = view.view * view.proj;
    return view;
}
} // namespace mye::scene
