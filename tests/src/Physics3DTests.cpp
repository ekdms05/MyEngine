#include "TestFramework.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/core/Events.h"
#include "mye/ecs/World.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/scene/Camera3D.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Transform.h"
#include <limits>
using namespace mye;

MYE_TEST(Physics3DGroundJumpWallCeilingRampAndStep) {
    phys::PhysicsWorld3D world;
    MYE_EXPECT(world.Add({1, {{0, -.5f, 0}, {20, .5f, 20}}}));
    MYE_EXPECT(world.Add({2, {{2, 2, 0}, {.2f, 2, 20}}}));
    MYE_EXPECT(world.Add({3, {{0, 2.5f, 0}, {20, .1f, 20}}}));
    phys::MotionSettings3D settings;
    phys::MotionState3D state;
    const auto tick = [&](Vec2 movement = {}, bool jump = false) {
        MYE_EXPECT(world.Step(state, movement, jump, 1.0f / 60, settings));
    };
    for (int i = 0; i < 30; ++i)
        tick();
    MYE_EXPECT(state.grounded);
    MYE_EXPECT_NEAR(state.position.y, settings.skin, .002f);
    tick({}, true);
    MYE_EXPECT(state.position.y > .05f && !state.grounded);
    bool ceiling = false;
    for (int i = 0; i < 100; ++i) {
        tick({1, 1});
        ceiling |= state.onCeiling;
    }
    MYE_EXPECT(ceiling);
    MYE_EXPECT(state.grounded);
    MYE_EXPECT(state.position.x <= 1.501f && state.position.z > 3);
    const auto before = state.position;
    MYE_EXPECT(!world.Step(state, {std::numeric_limits<float>::quiet_NaN(), 0}, false, .02f, settings));
    MYE_EXPECT_NEAR(state.position.x, before.x, 0);
    state.position = {2, 0, 0};
    MYE_EXPECT(!world.Step(state, {}, false, .02f, settings));

    world.Clear();
    MYE_EXPECT(world.Add({1, {{0, -.5f, 0}, {20, .5f, 20}}}));
    MYE_EXPECT(world.Add({2, {{0, .5f, 2}, {2, .5f, 2}}, phys::Shape3D::Ramp}));
    state = {};
    state.position = {0, 0, -1};
    for (int i = 0; i < 90; ++i)
        tick({0, 1});
    MYE_EXPECT(state.position.y > .5f);
    MYE_EXPECT(state.grounded);
    MYE_EXPECT(state.floorNormal.y > .9f);
    const auto onRamp = state.position;
    for (int i = 0; i < 60; ++i)
        tick();
    MYE_EXPECT_NEAR(state.position.x, onRamp.x, .001f);
    MYE_EXPECT_NEAR(state.position.z, onRamp.z, .001f);
    world.Clear();
    MYE_EXPECT(world.Add({1, {{0, -.5f, 0}, {20, .5f, 20}}}));
    MYE_EXPECT(world.Add({2, {{0, .1f, 1}, {2, .1f, .5f}}}));
    state = {};
    for (int i = 0; i < 20; ++i)
        tick();
    for (int i = 0; i < 18; ++i)
        tick({0, 1});
    MYE_EXPECT(state.position.z > .55f && state.position.y > .19f);
}

MYE_TEST(Runtime3DCameraRelativeControlsTriggersAndSerialization) {
    EventBus events;
    ecs::World world;
    world.SetEventBus(&events);
    scene::RegisterCoreComponents(world);
    runtime::RegisterObjectComponents(world);
    const auto object = [&](std::string name, Vec3 position) {
        auto e = world.Create();
        world.Add<scene::ObjectName>(e).value = name;
        world.Add<scene::LocalTransform>(e).position = position;
        world.Add<scene::WorldTransform>(e);
        return e;
    };
    const auto player = object("Player", {});
    auto& collider = world.Add<phys::Collider3D>(player);
    collider.half = {.3f, .8f, .3f};
    collider.offset = {0, .8f, 0};
    world.Add<phys::KinematicBody3D>(player);
    world.Add<runtime::CharacterController3D>(player);
    auto floor = object("Floor", {0, -.5f, 0});
    world.Add<phys::Collider3D>(floor).half = {10, .5f, 10};
    auto camera = object("Camera", {});
    auto& c = world.Add<scene::Camera3D>(camera);
    c.orbitEnabled = true;
    c.followTarget = "Player";
    c.target = {0, 1, 0};
    c.yawDegrees = 90;
    auto trigger = object("Exit", {-1, 1, 0});
    auto& t = world.Add<phys::Collider3D>(trigger);
    t.isTrigger = true;
    world.Add<runtime::ScenePortal>(trigger).scenePath = "assets/next.scene";
    world.TryGet<runtime::ScenePortal>(trigger)->spawnName = "Entry";
    world.TryGet<runtime::ScenePortal>(trigger)->onInteract = false;
    world.Add<runtime::ObjectBehavior>(trigger).luaSource = R"(local C={}
function C:on_trigger_enter(other)
    local player=mye.world.entity_from_packed(other)
    local self_entity=mye.world.entity_from_packed(self.entity)
    local position=self_entity:get_position3d()
    if player:is_on_floor3d() then position.z=2; self_entity:set_position3d(position) end
end
return C)";
    scene::UpdateWorldTransforms(world);
    auto snapshot = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(snapshot);
    if (snapshot) {
        ecs::World copy;
        scene::RegisterCoreComponents(copy);
        runtime::RegisterObjectComponents(copy);
        MYE_EXPECT(scene::SceneSerializer{}.ReadInto(copy, snapshot.Value()));
        MYE_EXPECT(runtime::ValidateObjectComponents(copy));
        copy.Query<scene::Camera3D>().Each([](ecs::Entity, const auto& camera) {
            MYE_EXPECT(camera.orbitEnabled && camera.yawDegrees == 90);
        });
    }
    // The legacy 3D controller must not replace a saved state's clip or restart its cursor.
    auto& animator = world.Add<anim::SpriteAnimator>(player);
    animator.stateMachine.guid = {7, 7};
    animator.animation.guid = {8, 8};
    animator.cursor.timeInStep = .125f;
    animator.started = true;
    auto* controller = world.TryGet<runtime::CharacterController3D>(player);
    controller->idleAnimation.guid = {9, 9};
    controller->walkAnimation.guid = {10, 10};
    runtime::ObjectSystem objects(world);    MYE_EXPECT(objects.Initialize());
    for (int i = 0; i < 20; ++i)
        MYE_EXPECT(objects.Tick(1.0f / 60, runtime::GameInput{{0, 1}}));
    MYE_EXPECT((animator.animation.guid == asset::AssetGuid{8, 8}) && animator.started);
    MYE_EXPECT_NEAR(animator.cursor.timeInStep, .125f, 0);
    animator.stateMachine.guid = {};
    MYE_EXPECT(objects.Tick(1.0f / 60, {}));
    MYE_EXPECT(animator.animation.guid == controller->idleAnimation.guid && !animator.started);
    MYE_EXPECT_NEAR(animator.cursor.timeInStep, 0, 0);
    auto pose = world.TryGet<scene::LocalTransform>(player)->position;    MYE_EXPECT(pose.x < -.9f);
    MYE_EXPECT(std::abs(pose.z) < .01f);
    MYE_EXPECT(objects.TakeMapRequest().scenePath == "assets/next.scene");
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(trigger)->position.z, 2, 0);
    const auto facing = world.TryGet<phys::KinematicBody3D>(player)->state.facingRadians;
    MYE_EXPECT(objects.Tick(1.0f / 60, runtime::GameInput{{}, false, false, 1, 100}));
    MYE_EXPECT_NEAR(c.yawDegrees, 111.5f, .001f);
    MYE_EXPECT_NEAR(world.TryGet<phys::KinematicBody3D>(player)->state.facingRadians, facing, .001f);
    MYE_EXPECT(scene::BuildGameView(world, render::Camera2D{}));
    c.orbitEnabled = false;
    auto* cameraPose = world.TryGet<scene::LocalTransform>(camera);
    cameraPose->position = {4, 4, 0};
    cameraPose->dirty = true;
    c.target = {};
    scene::UpdateWorldTransforms(world);
    const auto forward = scene::CameraRelativeMovement(world, {0, 1});
    MYE_EXPECT_NEAR(forward.x, -1, .001f);
    MYE_EXPECT_NEAR(forward.y, 0, .001f);
}
