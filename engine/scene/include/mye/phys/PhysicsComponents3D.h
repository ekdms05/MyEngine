#pragma once
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"
#include "mye/phys/PhysicsWorld3D.h"
namespace mye::ecs {
class World;
}
namespace mye::phys {
struct Collider3D {
    MYE_COMPONENT(Collider3D);
    bool enabled = true, isTrigger = false;
    Shape3D shape = Shape3D::Box;
    Vec3 half{.5f, .5f, .5f}, offset{};
};
struct KinematicBody3D {
    MYE_COMPONENT(KinematicBody3D);
    MotionSettings3D settings;
    MotionState3D state;
    Vec3 lastMove{};
    bool initialized = false;
};
// Reuses the caller's storage. All 3D colliders must have axis-aligned positive TRS.
Expected<void, Error> GatherPhysicsWorld3D(ecs::World& world, PhysicsWorld3D& physics);
Expected<MotionSettings3D, Error> CharacterSettings3D(ecs::World& world, ecs::Entity entity);
} // namespace mye::phys
