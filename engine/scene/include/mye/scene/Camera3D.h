#pragma once

#include "mye/core/Base.h"
#include "mye/ecs/ComponentType.h"
#include "mye/render/HybridRenderer.h"
#include <string>

namespace mye::ecs { class World; }
namespace mye::phys { class PhysicsWorld3D; }
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
    bool orbitEnabled = false;
    float yawDegrees = 0, pitchDegrees = 25, distance = 8;
    float rotationSpeed = 90, mouseSensitivity = .2f, collisionMargin = .15f;
    float resolvedDistance = 0; // Runtime only; zero uses the authored distance.
};

// No current Camera3D keeps the existing 2D camera. Ambiguous or invalid cameras fail.
Expected<render::HybridViewInfo, Error> BuildGameView(ecs::World& world,
    const render::Camera2D& fallback, uint32_t width = render::kInternalWidth,
    uint32_t height = render::kInternalHeight);
Expected<void, Error> UpdateGameCamera(ecs::World& world, const phys::PhysicsWorld3D& physics,
    float dt, float rotationAxis, float mouseDeltaX);
Vec2 CameraRelativeMovement(ecs::World& world, Vec2 movement);
} // namespace mye::scene
