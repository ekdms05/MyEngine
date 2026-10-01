#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/ComponentType.h"
#include "mye/render/HybridRenderer.h"
#include <string>

namespace mye::ecs { class World; }
namespace mye::scene {

// Position comes from WorldTransform. With followTarget, position and target are
// offsets from that named object's world position; otherwise target is world space.
struct Camera3D {
    MYE_COMPONENT(Camera3D);
    bool current = true;
    Vec3 target{0, 0, 0};
    std::string followTarget;
    float fovDegrees = 45.0f;
    float nearPlane = 0.05f;
    float farPlane = 1000.0f;
};

// No current Camera3D keeps the existing 2D camera. Ambiguous or invalid cameras fail.
Expected<render::HybridViewInfo, Error> BuildGameView(ecs::World& world,
    const render::Camera2D& fallback, uint32_t width = render::kInternalWidth,
    uint32_t height = render::kInternalHeight);
} // namespace mye::scene
