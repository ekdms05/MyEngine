#include "TestFramework.h"
#include "mye/editor/Project.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Viewport.h"
#include "mye/editor/Command.h"
#include "mye/editor/EditorContext.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/runtime/OnlineScene.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera2D.h"
#include "mye/scene/Camera3D.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/ecs/World.h"
#include "mye/core/JsonFile.h"
#include "mye/ser/JsonArchive.h"
#include "mye/gameplay/Progression.h"
#include <chrono>
#include <filesystem>

using namespace mye;
namespace {
ecs::Entity Object(ecs::World& world, std::string name, Vec2 position = {}) {
    const auto e = world.Create();
    world.Add<scene::ObjectName>(e).value = std::move(name);
    world.Add<scene::LocalTransform>(e).position = {position.x, position.y, 0};
    world.Add<scene::WorldTransform>(e);
    return e;
}
ecs::Entity Player(ecs::World& world) {
    const auto player = Object(world, "Player");
    world.Add<phys::Collider2D>(player).shape = phys::Shape2D::MakeBox(.1f, .1f);
    world.Add<phys::KinematicBody2D>(player);
    world.Add<runtime::CharacterController2D>(player).speed = 2;
    world.Add<gameplay::Progression>(player).level = 7;
    return player;
}
std::filesystem::path FreshObjectRoot() {
    return Utf8Path(MYE_TEST_DATA_DIR) / "object-workflow" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
std::filesystem::path OnlineProject(ecs::World& world) {
    const auto root = FreshObjectRoot();
    std::filesystem::create_directories(root / "assets/scenes");
    MYE_EXPECT(WriteJsonFile(root / "project.myeproj", json::Value::Object{
        {"version", int64_t{1}}, {"name", std::string("Online fixture")},
        {"mainScene", std::string("assets/scenes/main.scene")}}));
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    return root;
}
}

MYE_TEST(OnlineScene2DUsesSharedCentersFloorsAndValidatedSpawns) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    world.TryGet<phys::Collider2D>(player)->shape = phys::Shape2D::MakeCircle(.2f);
    world.TryGet<phys::Collider2D>(player)->offset = {.05f, .1f};
    world.Add<scene::FloorLevel>(player).level = 1;
    const auto parent = Object(world, "Parent", {2, 0});
    const auto wall = Object(world, "Wall", {.3f, .4f});
    auto& collider = world.Add<phys::Collider2D>(wall);
    collider.shape = phys::Shape2D::MakeBox(.005f, 10);
    collider.offset = {.1f, .2f};
    scene::ApplyReparent(world, wall, parent, false);
    world.Add<scene::FloorLevel>(wall).level = 1;
    const auto upper = Object(world, "Upper floor");
    world.Add<phys::Collider2D>(upper);
    world.Add<scene::FloorLevel>(upper).level = 2;
    const auto trigger = Object(world, "Trigger");
    world.Add<phys::Collider2D>(trigger).isTrigger = true;
    world.Add<scene::FloorLevel>(trigger).level = 1;
    scene::UpdateWorldTransforms(world);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    auto loaded = runtime::LoadOnlineScene2D(project);
    MYE_EXPECT(loaded);
    if (!loaded) return;
    const auto& online = loaded.Value();
    const auto selected = runtime::LoadOnlineScene(project);
    MYE_EXPECT(selected && std::holds_alternative<runtime::OnlineScene2D>(selected.Value()));
    if (selected) {
        const auto& scene = std::get<runtime::OnlineScene2D>(selected.Value());
        MYE_EXPECT(scene.hash == online.hash && scene.sceneId == online.sceneId);
        MYE_EXPECT_NEAR(scene.offset.y, online.offset.y, .00001f);
    }
    MYE_EXPECT(online.colliders.size() == 3 && online.hash != 0);
    MYE_EXPECT(online.character.shape.kind == phys::ShapeKind::Circle);
    MYE_EXPECT(online.character.floorMask == phys::FloorBit(1));
    MYE_EXPECT_NEAR(online.character.pos.x, .05f, .00001f);
    MYE_EXPECT_NEAR(online.character.pos.y, .1f, .00001f);
    MYE_EXPECT_NEAR(online.spawn.x, 0, .00001f);
    MYE_EXPECT_NEAR(online.offset.y, .1f, .00001f);
    MYE_EXPECT_NEAR(online.speed, 2, .00001f);
    const auto alias = runtime::LoadOnlineScene2D(project, "assets/scenes/../scenes/main.scene");
    MYE_EXPECT(alias && alias.Value().sceneId == online.sceneId && alias.Value().hash == online.hash);
    const auto moved = phys::MoveAndSlide2D(online.character, online.colliders, {30, 0}, .1f, online.maxSlideIters);
    MYE_EXPECT(moved);
    if (!moved) return;
    MYE_EXPECT_NEAR(moved.Value().position.x - online.offset.x, 2.145f, .00001f);
    world.TryGet<phys::KinematicBody2D>(player)->velocity = {30, 0};
    MYE_EXPECT(phys::PhysicsWorld2D{}.Step(world, nullptr, .1f));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x,
                    moved.Value().position.x - online.offset.x, .00001f);
    auto saved = online.character;
    saved.pos = {2.4f, .6f};
    MYE_EXPECT(!phys::ValidateSpawn2D(saved, online.colliders));
    MYE_EXPECT_NEAR(saved.pos.x, 2.4f, .00001f); // Invalid stored positions are never repaired.
    saved.pos = {.05f, .1f};
    MYE_EXPECT(phys::ValidateSpawn2D(saved, online.colliders));
    MYE_EXPECT(runtime::LoadOnlineScene2D(Utf8String(Utf8Path(MYE_STARTER_SOURCE_DIR) / "project.myeproj")));
}

MYE_TEST(SavedTwoDCameraMatchesPlayAndStandaloneFixedTicks) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    Player(world);
    const auto cameraEntity = Object(world, "Camera", {100, 100});
    auto& camera = world.Add<scene::Camera2D>(cameraEntity);
    camera.followTarget = "Player"; camera.offset = {.003f, .002f};
    camera.zoom = 1.5f; camera.deadzoneHalf = {.4f, .3f};
    camera.boundsEnabled = true; camera.bounds = {-12, -8, 24, 16};
    world.Add<runtime::ObjectBehavior>(cameraEntity).luaSource = R"(
        return {on_init = function(self)
            mye.world.entity_from_packed(self.entity):shake_camera(.2, .2)
        end}
    )";
    const auto saved = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(saved);
    if (!saved) return;
    editor::Document standalone({2}, editor::Document::Kind::Scene, "");
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(standalone.World(), saved.Value()));
    runtime::ObjectSystem objects(standalone.World());
    MYE_EXPECT(objects.Initialize());
    render::Camera2D defaultCamera;
    runtime::UpdateDefaultCamera2D(standalone.World(), defaultCamera, true);
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    MYE_EXPECT(play.Play());
    play.Pause(); play.StepFrame();
    MYE_EXPECT(play.ConsumeStepRequest() && !play.ConsumeStepRequest());
    for (int i = 0; i < 120; ++i) {
        runtime::GameInput input{{1, 0}};
        if (i == 10) input.cameraZoomSteps = 1;
        MYE_EXPECT(play.Tick(1.0f / 60, input, ""));
        MYE_EXPECT(objects.Tick(1.0f / 60, input));
        runtime::UpdateDefaultCamera2D(standalone.World(), defaultCamera);
        const auto a = scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera());
        const auto b = scene::BuildGameView(standalone.World(), defaultCamera);
        MYE_EXPECT(a && b);
        if (!a || !b) return;
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
            MYE_EXPECT_NEAR(a.Value().viewProj.m[row][col], b.Value().viewProj.m[row][col], .000001f);
        MYE_EXPECT_NEAR(a.Value().subpixelResidual.x, b.Value().subpixelResidual.x, .000001f);
        MYE_EXPECT_NEAR(a.Value().subpixelResidual.y, b.Value().subpixelResidual.y, .000001f);
    }
    play.ActiveWorld()->Query<scene::Camera2D>().Each([&](ecs::Entity, auto& c) {
        MYE_EXPECT_NEAR(c.view.Position().x, 3.603f, .0001f);
        MYE_EXPECT_NEAR(c.zoom, 1.65f, .0001f);
        MYE_EXPECT(!c.view.IsShaking());
        const auto position = c.view.Position();
        MYE_EXPECT(scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera()));
        MYE_EXPECT(c.view.Position() == position); // Render does not consume another follow/shake tick.
        c.current = false;
    });
    standalone.World().Query<scene::Camera2D>().Each([](ecs::Entity, auto& c) { c.current = false; });
    const auto a = scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera());
    const auto b = scene::BuildGameView(standalone.World(), defaultCamera);
    MYE_EXPECT(a && b && ApproxEqual(a.Value().view.m[3][0], b.Value().view.m[3][0]));
    play.Stop();
    MYE_EXPECT(world.TryGet<scene::Camera2D>(cameraEntity)->zoom == 1.5f && !camera.initialized);
}

MYE_TEST(OnlineScene2DRefusesInvalidPrototypesAndPreservesSceneFiles) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    const auto sceneFile = root / "assets/scenes/main.scene";
    const auto verify = [&] {
        MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(sceneFile)));
        const auto before = ReadJsonFile(sceneFile);
        MYE_EXPECT(before);
        MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
        MYE_EXPECT(!runtime::LoadOnlineScene(project));
        const auto after = ReadJsonFile(sceneFile);
        MYE_EXPECT(after);
        if (before && after) MYE_EXPECT(json::Stringify(before.Value()) == json::Stringify(after.Value()));
    };
    world.TryGet<runtime::CharacterController2D>(player)->enabled = false;
    verify();
    world.TryGet<runtime::CharacterController2D>(player)->enabled = true;
    world.TryGet<phys::Collider2D>(player)->isTrigger = true;
    verify();
    world.TryGet<phys::Collider2D>(player)->isTrigger = false;
    const auto duplicate = Player(world);
    world.TryGet<scene::ObjectName>(duplicate)->value = "Second player";
    verify();
    world.Destroy(duplicate);
    const auto moving = Object(world, "Unsupported mover", {4, 0});
    world.Add<phys::Collider2D>(moving);
    world.Add<phys::KinematicBody2D>(moving);
    verify();
    world.Destroy(moving);
    const auto wall = Object(world, "Blocking spawn");
    world.Add<phys::Collider2D>(wall);
    verify();
    world.Destroy(wall);
    world.Add<scene::FloorLevel>(player).level = 8;
    verify();
}

MYE_TEST(OnlineScenePathBoundsAndExisting3DLoad) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Object(world, "Player");
    world.Add<phys::Collider3D>(player).offset = {0, .8f, 0};
    world.TryGet<phys::Collider3D>(player)->half = {.3f, .8f, .3f};
    world.Add<phys::KinematicBody3D>(player);
    world.Add<runtime::CharacterController3D>(player);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    MYE_EXPECT(runtime::LoadOnlineScene3D(project));
    const auto selected = runtime::LoadOnlineScene(project);
    MYE_EXPECT(selected && std::holds_alternative<runtime::OnlineScene3D>(selected.Value()));
    const auto previousDirectory = std::filesystem::current_path();
    std::filesystem::current_path(root);
    const auto relativeProject = runtime::LoadOnlineScene3D("project.myeproj");
    std::filesystem::current_path(previousDirectory);
    MYE_EXPECT(relativeProject);
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
    const auto other = Player(world);
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    MYE_EXPECT(!runtime::LoadOnlineScene(project)); // Never choose a dimension for mixed enabled prototypes.
    world.Destroy(other);
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    for (auto path : {"../outside.scene", "assets/scenes/missing.scene", "assets/scenes/main.txt"}) {
        MYE_EXPECT(!runtime::LoadOnlineScene2D(project, path));
        MYE_EXPECT(!runtime::LoadOnlineScene3D(project, path));
        MYE_EXPECT(!runtime::LoadOnlineScene(project, path));
    }
    const auto absolute = Utf8String(root / "assets/scenes/main.scene");
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, absolute));
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, absolute));
    std::string nul = "assets/scenes/main.scene";
    nul.push_back('\0'); nul += "outside";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, nul));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, nul));
    const std::string invalidUtf8 = "assets/scenes/" + std::string(1, static_cast<char>(0xFF)) + ".scene";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, invalidUtf8));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, invalidUtf8));
    const auto invalidProject = project + std::string(1, static_cast<char>(0xFF)) + ".myeproj";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(invalidProject));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(invalidProject));
    MYE_EXPECT(WriteJsonFile(root / "project.myeproj", json::Value::Object{
        {"version", int64_t{99}}, {"name", std::string("Unsupported")},
        {"mainScene", std::string("assets/scenes/main.scene")}}));
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
}

MYE_TEST(ObjectNamedActionsAreVisibleOnlyInsideTheirFixedTick) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    const auto marker = Object(world, "Input marker");
    world.Add<runtime::ObjectBehavior>(marker).luaSource = R"(
        return {
            on_init = function(self)
                assert(not mye.input.is_action_just_pressed('attack'))
            end,
            on_update = function(self, dt)
                local entity = mye.world.entity_from_packed(self.entity)
                local p = entity:get_position()
                if mye.input.is_action_just_pressed('attack') then p.x = p.x + 1 end
                if mye.input.is_action_just_released('attack') then p.y = p.y + 1 end
                entity:set_position(p)
            end,
            on_interact = function(self)
                assert(not mye.input.is_action_just_pressed('attack'))
                assert(not mye.input.is_action_pressed('attack'))
                mye.world.entity_from_packed(self.entity):set_position(mye.Vec2(10, 10))
            end,
            on_destroy = function(self)
                assert(not mye.input.is_action_pressed('attack'))
                mye.world.entity_from_packed(self.entity):set_position(mye.Vec2(20, 20))
            end
        }
    )";
    {
        runtime::ObjectSystem objects(world); MYE_EXPECT(objects.Initialize());
        {
            runtime::GameInputBuffer buffer;
            MYE_EXPECT(buffer.Configure({{{"attack", .2f, {{InputDevice::Key, static_cast<int>(KeyCode::F)}}}}}));
            InputState input; input.NewFrame(); input.OnKey(KeyCode::F, true); input.OnKey(KeyCode::F, false);
            buffer.Capture(input, true);
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(1, 1, 0));
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(1, 1, 0));
            input.NewFrame(); input.OnKey(KeyCode::F, true); buffer.Capture(input, true);
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 0;
            MYE_EXPECT(!objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 4;
            objects.Dispatch(marker, runtime::ObjectEvent::Interact);
            MYE_EXPECT(objects.Message().empty());
        } // The input source dies before callbacks outside Tick and VM teardown.
        objects.Dispatch(marker, runtime::ObjectEvent::Interact);
        MYE_EXPECT(objects.Message().empty());
        MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(10, 10, 0));
        MYE_EXPECT(!objects.Tick(0, runtime::GameInput{}));
        MYE_EXPECT(objects.Tick(1.0f / 60, runtime::GameInput{}));
    }
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(20, 20, 0));
}

MYE_TEST(ObjectControlsCollisionTriggerAndLuaInteraction) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    world.TryGet<phys::Collider2D>(player)->offset = {.05f, .05f};
    const auto second = Player(world);
    world.TryGet<scene::ObjectName>(second)->value = "Second Player";
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    world.Destroy(second);
    const auto wall = Object(world, "Wall", {1,0});
    world.Add<phys::Collider2D>(wall).shape = phys::Shape2D::MakeBox(.2f, 2);
    const auto trigger = Object(world, "Trigger", {.3f,0});
    auto& collider = world.Add<phys::Collider2D>(trigger);
    collider.isTrigger = true; collider.shape = phys::Shape2D::MakeBox(.15f,.2f);
    auto& behavior = world.Add<runtime::ObjectBehavior>(trigger);
    behavior.connections.push_back({runtime::ObjectEvent::TriggerEnter, runtime::ObjectAction::Message, {}, "entered"});
    behavior.connections.push_back({runtime::ObjectEvent::TriggerExit, runtime::ObjectAction::Message, {}, "left"});
    const auto sign = Object(world, "Sign", {0,-.2f});
    world.Add<runtime::InteractionTarget>(sign).radius = 2;
    world.Add<runtime::ObjectBehavior>(sign).luaSource = R"lua(return {
        on_interact = function(self)
            local entity = mye.world.entity_from_packed(self.entity)
            entity:set_position(mye.Vec2(0, -0.5))
        end
    })lua";
    runtime::ObjectSystem system(world);
    MYE_EXPECT(system.Initialize());
    for (int i = 0; i < 5; ++i) MYE_EXPECT(system.Tick(.02f, {}, false));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, 0.0f, .001f);
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.y, 0.0f, .001f);
    for (int i = 0; i < 12; ++i) MYE_EXPECT(system.Tick(.02f, {1,0}, false));
    MYE_EXPECT(system.Message() == "entered");
    for (int i = 0; i < 50; ++i) MYE_EXPECT(system.Tick(.02f, {1,0}, false));
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(player)->position.x < .71f);
    MYE_EXPECT(system.Message() == "left");
    MYE_EXPECT(system.Tick(.02f, {}, true));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(sign)->position.y, -.5f, .001f);
    MYE_EXPECT(!system.Prompt().empty());
}

MYE_TEST(ObjectTickPropagatesPhysicsFailureBeforeMovementCommit) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    runtime::ObjectSystem system(world);
    MYE_EXPECT(system.Initialize());
    world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 0;
    auto failed = system.Tick(.02f, {1, 0}, false);
    MYE_EXPECT(!failed);
    if (!failed) MYE_EXPECT(failed.GetError().message.find("1..16 slides") != std::string::npos);
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, 0, .001f);
    world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 4;
    MYE_EXPECT(system.Tick(.02f, {1, 0}, false));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, .04f, .001f);
}

MYE_TEST(ObjectSceneRoundTripRejectsInvalidEventsAndDuplicateTargets) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto object = Object(world, "Sign");
    auto& behavior = world.Add<runtime::ObjectBehavior>(object);
    behavior.connections.push_back({runtime::ObjectEvent::Interact, runtime::ObjectAction::Message, {}, "welcome"});
    behavior.luaSource = "return {}";
    auto data = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(data);
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    editor::EditorContext context;
    context.playMode = &play; context.commands = &document.Commands();
    document.Commands().SetContext(&context);
    auto edit = behavior;
    edit.connections[0].text = "edited";
    const auto* type = refl::GetType<runtime::ObjectBehavior>();
    auto before = ser::JsonArchive::ForWrite(), after = ser::JsonArchive::ForWrite();
    MYE_EXPECT(refl::ReadValue(*type, &behavior, {}, before));
    MYE_EXPECT(refl::ReadValue(*type, &edit, {}, after));
    document.Commands().Push(std::make_unique<editor::PropertyEditCommand>(editor::ObjectRef::Component(object, *type),
        refl::PropertyPath{}, editor::ValueBlob{json::Stringify(before.Root())}, editor::ValueBlob{json::Stringify(after.Root())}));
    MYE_EXPECT(behavior.connections[0].text == "edited");
    document.Commands().Undo();
    MYE_EXPECT(behavior.connections[0].text == "welcome" && behavior.luaSource == "return {}");
    editor::Document restored({2}, editor::Document::Kind::Scene, "");
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(restored.World(), data.Value()));
    restored.World().Query<runtime::ObjectBehavior>().Each([&](ecs::Entity, const auto& b) {
        MYE_EXPECT(b.connections.size() == 1 && b.connections[0].text == "welcome" && b.luaSource == "return {}");
    });
    auto text = json::Stringify(data.Value());
    const auto pos = text.find("Interact");
    MYE_EXPECT(pos != std::string::npos);
    if (pos == std::string::npos) return;
    text.replace(pos, 8, "WrongEvent");
    auto invalid = json::Parse(text);
    MYE_EXPECT(invalid);
    MYE_EXPECT(!scene::SceneSerializer{}.ReadInto(restored.World(), invalid.Value()));
    auto oversized = json::Parse(R"json({"__version":1,"entities":[{"id":1,"components":{"ObjectBehavior":{"__version":1,"connections":[{"__version":1,"x":1e100}]}}}]})json");
    MYE_EXPECT(oversized);
    if (oversized) MYE_EXPECT(!scene::SceneSerializer{}.ReadInto(restored.World(), oversized.Value()));
    Object(world, "Sign");
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
}

MYE_TEST(ObjectMapTransitionPreservesEditWorldAndProgressionAndRejectsMissingMaps) {
    const auto root = FreshObjectRoot();
    std::filesystem::create_directories(root / "assets/scenes");
    editor::Document source({1}, editor::Document::Kind::Scene, "");
    auto& world = source.World();
    Player(world);
    const auto door = Object(world, "Door");
    world.Add<runtime::InteractionTarget>(door);
    auto& portal = world.Add<runtime::ScenePortal>(door);
    portal.scenePath = "assets/scenes/destination.scene"; portal.spawnName = "Spawn";
    editor::Document target({2}, editor::Document::Kind::Scene, "");
    Player(target.World());
    Object(target.World(), "Spawn", {4,2});
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(target.World(), Utf8String(root / portal.scenePath)));
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    MYE_EXPECT(play.Play());
    auto* old = play.ActiveWorld();
    MYE_EXPECT(play.Tick(.02f, {}, true, Utf8String(root)));
    for (int i = 0; i < 30; ++i) MYE_EXPECT(play.Tick(.02f, {}, false, Utf8String(root)));
    MYE_EXPECT(play.ActiveWorld() != old);
    play.ActiveWorld()->Query<runtime::CharacterController2D, scene::LocalTransform, gameplay::Progression>().Each(
        [&](ecs::Entity, const auto&, const auto& t, const auto& p) { MYE_EXPECT_NEAR(t.position.x, 4.0f, .001f); MYE_EXPECT(p.level == 7); });
    play.Stop();
    MYE_EXPECT(play.ActiveWorld() == &world);
    portal.scenePath = "assets/scenes/missing.scene";
    MYE_EXPECT(play.Play()); old = play.ActiveWorld();
    MYE_EXPECT(play.Tick(.02f, {}, true, Utf8String(root)));
    bool failed = false;
    for (int i = 0; i < 30; ++i) if (!play.Tick(.02f, {}, false, Utf8String(root))) failed = true;
    MYE_EXPECT(failed && play.ActiveWorld() == old);
    play.Stop();
}

MYE_TEST(ViewportPerspectiveProjectsWorldZAndRoundTripsTheEditingPlane) {
    for (bool perspective : {false, true}) {
        editor::ViewportCamera camera;
        camera.perspective = perspective; camera.center = {2,-1};
        const Vec3 point{3,2,0};
        const auto pixel = editor::ProjectViewportPoint(camera, point, 960,540);
        const auto restored = editor::ViewportPointOnPlane(camera, pixel,960,540);
        MYE_EXPECT_NEAR(restored.x, point.x, .002f); MYE_EXPECT_NEAR(restored.y, point.y, .002f);
        if (perspective) {
            const auto displaced = editor::ProjectViewportPoint(camera, {point.x,point.y,2},960,540);
            MYE_EXPECT(std::abs(displaced.x - pixel.x) + std::abs(displaced.y - pixel.y) > 1);
        }
    }
}

MYE_TEST(ObjectInvalidSavePreservesTheOriginalSceneAndDirtyState) {
    editor::ProjectContext project;
    MYE_EXPECT(project.Create("Save validation", Utf8String(FreshObjectRoot())));
    auto* doc = project.Active();
    if (!doc) return;
    const auto original = ReadJsonFile(Utf8Path(doc->Path()));
    MYE_EXPECT(original);
    editor::PlayModeController play;
    play.SetEditWorld(&doc->World());
    editor::EditorContext context;
    context.playMode = &play; context.activeDocument = doc; context.commands = &doc->Commands();
    doc->Commands().SetContext(&context);
    auto create = std::make_unique<editor::CreateEntityCommand>();
    auto* created = create.get();
    doc->Commands().Push(std::move(create));
    doc->Commands().Push(std::make_unique<editor::AddComponentCommand>(created->Created(), *refl::GetType<runtime::ScenePortal>()));
    MYE_EXPECT(doc->IsDirty());
    MYE_EXPECT(!project.Save()); // Incomplete portal cannot make the next file open fail.
    MYE_EXPECT(doc->IsDirty());
    const auto retained = ReadJsonFile(Utf8Path(doc->Path()));
    MYE_EXPECT(retained && json::Stringify(retained.Value()) == json::Stringify(original.Value()));
}
