#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"
#include "mye/render/Camera2D.h"
#include <string>

namespace mye::ecs { class World; }
namespace mye::scene {

// A named target uses its world XY plus offset; otherwise use this object's world XY.
// Rotation/scale do not change the view. Runtime follow/shake state is never saved.
struct Camera2D {
    MYE_COMPONENT(Camera2D);
    bool current = true;
    std::string followTarget;
    Vec2 offset{};
    Vec2 deadzoneHalf{2.5f, 1.5f};
    float zoom = 1;
    bool pixelSnap = true;
    bool boundsEnabled = false;
    Rect bounds{-10, -5.625f, 20, 11.25f};
    render::Camera2D view;
    bool initialized = false;
};

// Resolves a copy: rendering/validation cannot advance follow or shake time.
Expected<render::Camera2D, Error> ResolveGameCamera2D(ecs::World& world, ecs::Entity entity,
    uint32_t width = render::kInternalWidth, uint32_t height = render::kInternalHeight);
// dt=0 initializes a scene; positive dt is fixed tick only. Positive wheel steps zoom in.
Expected<void, Error> UpdateGameCamera2D(ecs::World& world, float dt, float zoomSteps = 0);
} // namespace mye::scene
