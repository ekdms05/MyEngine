#include "TestFramework.h"
#include "mye/editor/Project.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Viewport.h"
#include "mye/editor/Command.h"
#include "mye/editor/EditorContext.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/phys/Collision.h"
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
