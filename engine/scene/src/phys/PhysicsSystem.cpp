// mye/phys/PhysicsSystem.cpp — 물리 스텝 시스템 등록 (docs/03 §8, §2)
#include "mye/phys/PhysicsSystem.h"

#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"

namespace mye::phys {

scene::SystemId RegisterPhysicsSystem(scene::SystemScheduler& scheduler, PhysicsWorld2D& physics) {
    scene::SystemDesc desc;
    desc.name = "PhysicsSystem";
    desc.phase = scene::Phase::FixedUpdate;
    desc.reads  = { KinematicBody2D::kComponentTypeId, Collider2D::kComponentTypeId };
    desc.writes = { KinematicBody2D::kComponentTypeId };

    PhysicsWorld2D* pw = &physics;
    desc.fn = [pw](ecs::World& world, ecs::CommandBuffer& /*cb*/, const TimeStep& time) {
        // FixedUpdate 페이즈: deltaSeconds가 곧 고정 dt(TimeStep 규약).
        const float dt = static_cast<float>(time.deltaSeconds);
        pw->Step(world, world.Events(), dt);
    };
    return scheduler.RegisterSystem(std::move(desc));
}

} // namespace mye::phys
