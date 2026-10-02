#pragma once
#include "mye/phys/Motion2D.h"
#include "mye/phys/PhysicsWorld3D.h"
#include <string>
#include <vector>
namespace mye::runtime {
struct OnlineScene2D {
    std::vector<phys::CollisionBody2D> colliders; // Character prototype excluded.
    phys::CollisionBody2D character;
    Vec2 offset{}, spawn{}; // spawn is the transform origin, not the collider center.
    float speed = 3;
    int maxSlideIters = 4;
    uint64_t hash = 0;
    std::string sceneId;
};
struct OnlineScene3D {
    phys::PhysicsWorld3D physics;
    phys::MotionSettings3D settings;
    Vec3 spawn;
    uint64_t hash = 0;
    std::string sceneId;
};
// Loads collision and movement contracts only; the server never loads visual assets.
Expected<OnlineScene2D, Error> LoadOnlineScene2D(std::string_view project, std::string_view scene = {});
Expected<OnlineScene3D, Error> LoadOnlineScene3D(std::string_view project, std::string_view scene = {});
} // namespace mye::runtime
