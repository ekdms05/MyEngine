#include "mye/scene/Camera3D.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/ecs/World.h"
#include <cmath>

namespace mye::scene {
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
