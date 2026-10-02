#pragma once
#include "mye/phys/PhysicsWorld3D.h"
#include <string>
namespace mye::runtime {
struct OnlineScene3D {
    phys::PhysicsWorld3D physics;
    phys::MotionSettings3D settings;
    Vec3 spawn;
    uint64_t hash = 0;
    std::string sceneId;
};
// Loads collision and movement contracts only; the server never loads visual assets.
Expected<OnlineScene3D, Error> LoadOnlineScene3D(std::string_view project, std::string_view scene = {});
} // namespace mye::runtime
