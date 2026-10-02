#include "mye/scene/Camera2D.h"
#include "mye/scene/Camera3D.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include <algorithm>
#include <cmath>

namespace mye::scene {
namespace {
bool FiniteView(const render::Camera2D& view) {
    for (const auto value : {view.Position(), view.SnappedPosition(), view.SubpixelResidual()})
        if (!std::isfinite(value.x) || !std::isfinite(value.y)) return false;
    return true;
}
Expected<render::Camera2D, Error> ResolveCamera(ecs::World& world, ecs::Entity entity,
                                             uint32_t width, uint32_t height, Vec2& target) {
    const auto* camera = world.TryGet<Camera2D>(entity);
    const auto* name = world.TryGet<ObjectName>(entity);
    const auto fail = [&](std::string reason) -> Expected<render::Camera2D, Error> {
        return Error{"Camera2D '" + (name ? name->value : std::to_string(entity.index)) + "': " + reason, 1};
    };
    const auto finite = [](Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); };
    if (!camera || width == 0 || height == 0) return fail("camera and nonzero viewport are required");
    if (!std::isfinite(camera->zoom) || camera->zoom < .25f || camera->zoom > 8 ||
        !finite(camera->offset) || !finite(camera->deadzoneHalf) ||
        camera->deadzoneHalf.x < 0 || camera->deadzoneHalf.y < 0)
        return fail("require finite offset, nonnegative deadzoneHalf and zoom in [0.25,8]");
    const auto& b = camera->bounds;
    if (camera->boundsEnabled && (!std::isfinite(b.x) || !std::isfinite(b.y) ||
        !std::isfinite(b.w) || !std::isfinite(b.h) || b.w <= 0 || b.h <= 0 ||
        !std::isfinite(b.x + b.w) || !std::isfinite(b.y + b.h)))
        return fail("bounds require finite minimum XY and positive width/height");
    const auto* pose = world.TryGet<WorldTransform>(entity);
    if (!pose) return fail("a world transform is required");
    target = {pose->matrix.m[3][0], pose->matrix.m[3][1]};
    if (!camera->followTarget.empty()) {
        unsigned matches = 0;
        // ponytail: one name scan per resolve; use invalidated entity handles if profiling finds a bottleneck.
        world.Query<ObjectName, WorldTransform>().Each([&](ecs::Entity, const auto& n, const auto& t) {
            if (n.value == camera->followTarget) {
                ++matches;
                target = {t.matrix.m[3][0], t.matrix.m[3][1]};
            }
        });
        if (matches != 1) return fail("followTarget must name exactly one transformed object: " + camera->followTarget);
    }
    target = target + camera->offset;
    if (!finite(target)) return fail("follow position must be finite");
    auto view = camera->view;
    view.ClearWorldBounds();
    view.SetViewportSize(width, height);
    view.SetZoom(camera->zoom);
    view.SetPixelSnap(camera->pixelSnap);
    if (camera->boundsEnabled) view.SetWorldBounds(camera->bounds);
    if (!camera->initialized) view.SetPosition(target);
    if (!FiniteView(view)) return fail("camera position exceeds finite pixel-snap precision");
    // Follow only commits in UpdateGameCamera2D, not in this render/validation copy.
    return view;
}
} // namespace

Expected<render::Camera2D, Error> ResolveGameCamera2D(ecs::World& world, ecs::Entity entity,
                                                   uint32_t width, uint32_t height) {
    Vec2 target{};
    return ResolveCamera(world, entity, width, height, target);
}

Expected<void, Error> UpdateGameCamera2D(ecs::World& world, float dt, float zoomSteps) {
    if (!std::isfinite(dt) || dt < 0 || dt > 1 || !std::isfinite(zoomSteps))
        return Error{"Camera2D tick requires dt in [0,1] and finite zoom input", 1};
    Camera2D* active = nullptr;
    ecs::Entity entity{};
    unsigned count = 0;
    world.Query<Camera2D>().Each([&](ecs::Entity e, Camera2D& c) {
        if (c.current) { active = &c; entity = e; ++count; }
    });
    world.Query<Camera3D>().Each([&](ecs::Entity, const Camera3D& c) { if (c.current) ++count; });
    if (count > 1) return Error{"Game view requires at most one current Camera2D or Camera3D", 1};
    if (!active) return {};
    Vec2 target{};
    auto resolved = ResolveCamera(world, entity, render::kInternalWidth, render::kInternalHeight, target);
    if (!resolved) return resolved.GetError();
    auto next = resolved.Value();
    const float zoom = std::clamp(active->zoom * std::pow(1.1f, std::clamp(zoomSteps, -16.0f, 16.0f)), .25f, 8.0f);
    next.SetZoom(zoom);
    next.FollowDeadzone(target, active->deadzoneHalf);
    next.TickShake(dt);
    if (!FiniteView(next)) return Error{"Camera2D follow/shake exceeds finite pixel-snap precision", 1};
    active->view = next;
    active->zoom = zoom;
    active->initialized = true;
    return {};
}
} // namespace mye::scene
