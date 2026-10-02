#include "TestFramework.h"
#include "mye/editor/Project.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/Importer.h"
#include "mye/core/JsonFile.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"
#include "mye/scene/SpriteGeometry.h"
#include "mye/gameplay/Progression.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/rhi/Rhi.h"
#include <chrono>
#include <cmath>
#include <filesystem>

using namespace mye;
namespace ed = mye::editor;
namespace {
std::filesystem::path FreshRoot() {
    return Utf8Path(MYE_TEST_DATA_DIR) / "starter-editor" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

MYE_TEST(ProjectInputSettingsSaveReloadAndPlayKeepSceneAndUnknownMetadata) {
    const auto root = FreshRoot();
    ed::ProjectContext project;
    MYE_EXPECT(project.Create("Input settings", Utf8String(root), false, MYE_STARTER_SOURCE_DIR));
    if (!project.IsOpen() || !project.Active()) return;
    const auto manifestPath = Utf8Path(project.ProjectFilePath());
    auto metadata = ReadJsonFile(manifestPath);
    MYE_EXPECT(metadata);
    if (!metadata) return;
    auto object = metadata.Value().AsObject(); object["userSetting"] = json::Value(std::string("keep"));
    MYE_EXPECT(WriteJsonFile(manifestPath, json::Value(std::move(object))));
    MYE_EXPECT(project.Open(Utf8String(manifestPath)));
    auto* doc = project.Active();
    const auto sceneBefore = ReadJsonFile(Utf8Path(doc->Path()));
    auto remap = project.InputSettings();
    for (auto& action : remap.actions) {
        if (action.name == "move_right") action.bindings = {{InputDevice::Key, static_cast<int>(KeyCode::L)}};
        if (action.name == "move_up") action.bindings = {{InputDevice::Key, static_cast<int>(KeyCode::I)}};
    }
    MYE_EXPECT(project.SaveInputSettings(remap));
    MYE_EXPECT(project.Active() == doc && !doc->IsDirty() && project.InputSettings() == remap);
    const auto sceneAfter = ReadJsonFile(Utf8Path(doc->Path()));
    MYE_EXPECT(sceneBefore && sceneAfter && json::Stringify(sceneBefore.Value()) == json::Stringify(sceneAfter.Value()));
    const auto saved = ReadJsonFile(manifestPath);
    MYE_EXPECT(saved && saved.Value().Find("userSetting")->AsString() == "keep");
    ed::ProjectContext reopened;
    MYE_EXPECT(reopened.Open(Utf8String(manifestPath)) && reopened.InputSettings() == remap);
    runtime::GameInputBuffer buffer; MYE_EXPECT(buffer.Configure(reopened.InputSettings()));
    ed::PlayModeController play; play.SetEditWorld(&reopened.Active()->World());
    MYE_EXPECT(play.Play());
    InputState input; input.NewFrame(); input.OnKey(KeyCode::D, true); buffer.Capture(input, true);
    MYE_EXPECT(buffer.ConsumeTick().movement.x == 0);
    input.NewFrame(); input.OnKey(KeyCode::L, true); input.OnKey(KeyCode::I, true); buffer.Capture(input, true);
    const auto controls = buffer.ConsumeTick();
    MYE_EXPECT_NEAR(std::hypot(controls.movement.x, controls.movement.y), 1, 1e-6f);
    ecs::Entity player;
    play.ActiveWorld()->Query<runtime::CharacterController2D>().Each([&](ecs::Entity e, const auto& c) { if (c.enabled) player = e; });
    MYE_EXPECT(!player.IsNull());
    if (player.IsNull()) return;
    const auto before = play.ActiveWorld()->TryGet<scene::LocalTransform>(player)->position;
    ed::ProjectContext standalone;
    MYE_EXPECT(standalone.Open(Utf8String(manifestPath)));
    runtime::ObjectSystem objects(standalone.Active()->World()); MYE_EXPECT(objects.Initialize());
    runtime::GameInputBuffer gameBuffer;
    MYE_EXPECT(gameBuffer.Configure(runtime::LoadGameInputMap(saved.Value().Find("inputMap")).Value()));
    gameBuffer.Capture(input, true);
    MYE_EXPECT(objects.Tick(1.0f / 60, gameBuffer.ConsumeTick()));
    MYE_EXPECT(play.Tick(1.0f / 60, controls, Utf8String(root)));
    const auto after = play.ActiveWorld()->TryGet<scene::LocalTransform>(player)->position;
    MYE_EXPECT(after.x > before.x && after.y > before.y);
    const auto standalonePosition = standalone.Active()->World().TryGet<scene::LocalTransform>(player)->position;
    MYE_EXPECT_NEAR(after.x, standalonePosition.x, 1e-6f);
    MYE_EXPECT_NEAR(after.y, standalonePosition.y, 1e-6f);
    MYE_EXPECT(reopened.Active()->World().TryGet<scene::LocalTransform>(player)->position == before);
    play.Stop();

    const auto backup = root / "manifest-backup.json";
    std::filesystem::rename(manifestPath, backup);
    std::filesystem::create_directory(manifestPath); // Checked writer cannot replace a directory.
    MYE_EXPECT(!project.SaveInputSettings(runtime::DefaultGameInputMap()) && project.InputSettings() == remap);
    MYE_EXPECT(ReadJsonFile(backup).Value().Find("userSetting")->AsString() == "keep");
    std::filesystem::remove(manifestPath); std::filesystem::rename(backup, manifestPath);
    auto malformed = saved.Value().AsObject(); malformed["inputMap"] = json::Value(false);
    MYE_EXPECT(WriteJsonFile(manifestPath, json::Value(std::move(malformed))));
    MYE_EXPECT(!project.Open(Utf8String(manifestPath)) && project.Active() == doc && project.InputSettings() == remap);
    MYE_EXPECT(WriteJsonFile(manifestPath, saved.Value()));
    MYE_EXPECT(project.Save());
    MYE_EXPECT(ReadJsonFile(manifestPath).Value().Find("userSetting")->AsString() == "keep");
}
asset::AnimationAsset ReadAnimation(const std::filesystem::path& path) {
    auto value = ReadJsonFile(path);
    MYE_EXPECT(value);
    if (!value) return {};
    auto animation = asset::AnimationAsset::FromJson(value.Value());
    MYE_EXPECT(animation);
    return animation ? std::move(animation).Value() : asset::AnimationAsset{};
}
}

MYE_TEST(StarterProjectAnimationRoundTripAndSceneUndo) {
    const auto root = FreshRoot();
    ed::ProjectContext project;
    MYE_EXPECT(project.Create("검증 마을", Utf8String(root), false, MYE_STARTER_SOURCE_DIR));
    auto* sceneDoc = project.Active();
    if (!sceneDoc) return;
    auto& world = sceneDoc->World();
    ecs::Entity player{};
    world.Query<gameplay::Progression, anim::SpriteAnimator>().Each(
        [&](ecs::Entity e, gameplay::Progression& progression, anim::SpriteAnimator& animator) {
            player = e;
            MYE_EXPECT(progression.level == 1 && progression.xp == 0);
            MYE_EXPECT(animator.animation.guid.IsValid());
            MYE_EXPECT(animator.sheet == nullptr && animator.directClip == nullptr);
        });
    MYE_EXPECT(!player.IsNull());
    scene::UpdateWorldTransforms(world);
    const auto* transform = world.TryGet<scene::WorldTransform>(player);
    MYE_EXPECT(transform && std::abs(transform->matrix.m[0][0] - 0.15f) < 0.0001f);
    MYE_EXPECT(transform && std::abs(transform->matrix.m[3][1] + 0.9f) < 0.0001f);

    const auto opened = project.OpenAnimation("assets/animations/novice_walk.anim");
    MYE_EXPECT(opened);
    if (!opened) return;
    auto* animationDoc = opened.Value();
    MYE_EXPECT(project.Active() == sceneDoc);
    MYE_EXPECT(project.OpenAnimation("assets/animations/./novice_walk.anim").Value() == animationDoc);
    ed::PlayModeController play;
    play.SetEditWorld(&world);
    ed::EditorContext ctx;
    ctx.project = &project; ctx.playMode = &play; ctx.commands = &sceneDoc->Commands();
    sceneDoc->Commands().SetContext(&ctx); animationDoc->Commands().SetContext(&ctx);
    auto after = animationDoc->Animation(); after.clip.frameDurations[0] = 0.22f;
    animationDoc->Commands().Push(std::make_unique<ed::AnimAssetEditCommand>(animationDoc->Animation(), after, "duration"));
    MYE_EXPECT(animationDoc->IsDirty() && !sceneDoc->IsDirty() && project.HasUnsavedChanges());
    MYE_EXPECT(!project.Open(MYE_STARTER_SOURCE_DIR));
    const auto recovery = Utf8Path(std::string(animationDoc->Path()) + ".tmp");
    MYE_EXPECT(WriteJsonFile(recovery, json::Value(std::string("recovery"))));
    MYE_EXPECT(!project.SaveAnimation(animationDoc->Id(), animationDoc->Path()));
    MYE_EXPECT(animationDoc->IsDirty());
    MYE_EXPECT(ReadAnimation(Utf8Path(animationDoc->Path())).clip.frameDurations[0] == 0.14f);
    std::error_code ec; std::filesystem::remove(recovery, ec);
    MYE_EXPECT(project.Save());
    MYE_EXPECT(ReadAnimation(root / "assets/animations/novice_walk.anim").clip.frameDurations[0] == 0.22f);

    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(root / "assets")), 0);
    asset::AssetManager assets(vfs, nullptr);
    assets.RegisterImporter(std::make_unique<asset::TextureImporter>());
    asset::AssetDatabase database(assets, nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(root / "assets")));
    const auto guid = database.GuidFromPath("assets://animations/novice_walk.anim");
    const auto original = world.TryGet<anim::SpriteAnimator>(player)->animation;
    MYE_EXPECT(ed::AssignAnimationToEntity(ctx, player, asset::AssetRef{guid, 0}));
    MYE_EXPECT(world.TryGet<anim::SpriteAnimator>(player)->animation.guid == guid);
    sceneDoc->Commands().Undo();
    MYE_EXPECT(world.TryGet<anim::SpriteAnimator>(player)->animation.guid == original.guid);
    sceneDoc->Commands().Redo();
    MYE_EXPECT(project.Save());
    MYE_EXPECT(play.Play());
    MYE_EXPECT(play.ActiveWorld() != &world);
    int levelOne = 0;
    play.ActiveWorld()->Query<gameplay::Progression>().Each([&](ecs::Entity, gameplay::Progression& p) { if (p.level == 1) ++levelOne; });
    MYE_EXPECT(levelOne == 1);
    play.Pause(); play.StepFrame();
    MYE_EXPECT(play.ConsumeStepRequest());
    MYE_EXPECT(!play.ConsumeStepRequest());
    play.Stop();

    const auto* type = refl::TypeRegistry::Get().Find("LocalTransform");
    const auto path = refl::PropertyPath::Parse("position.y");
    MYE_EXPECT(type && path);
    sceneDoc->Commands().Push(std::make_unique<ed::PropertyEditCommand>(ed::ObjectRef::Component(player, *type), path.Value(),
        ed::ValueBlob{"-0.9"}, ed::ValueBlob{"-2.0"}));
    scene::UpdateWorldTransforms(world);
    MYE_EXPECT(world.TryGet<scene::WorldTransform>(player)->matrix.m[3][1] == -2.0f);
    sceneDoc->Commands().Undo(); scene::UpdateWorldTransforms(world);
    MYE_EXPECT(std::abs(world.TryGet<scene::WorldTransform>(player)->matrix.m[3][1] + 0.9f) < 0.0001f);
    MYE_EXPECT(project.Open(Utf8String(root / "project.myeproj")));
    MYE_EXPECT(project.Name() == "검증 마을");
}

MYE_TEST(AnimationFileRejectsInvalidFramesAndEvents) {
    const auto source = Utf8Path(MYE_STARTER_SOURCE_DIR) / "assets/animations/novice_walk.anim";
    const auto original = ReadAnimation(source);
    const auto roundTrip = asset::AnimationAsset::FromJson(original.ToJson());
    MYE_EXPECT(roundTrip && roundTrip.Value().clip.events.size() == 2);
    auto bad = original; bad.sheet.frames[0].rect.x = -1; MYE_EXPECT(!bad.Validate());
    bad = original; bad.sheet.frames[0].rect.w = INT32_MAX; MYE_EXPECT(!bad.Validate());
    bad = original; bad.clip.frameIndices[0] = 4096; MYE_EXPECT(!bad.Validate());
    bad = original; bad.clip.frameDurations[0] = 0; MYE_EXPECT(!bad.Validate());
    bad = original; bad.clip.frameDurations.pop_back(); MYE_EXPECT(!bad.Validate());
    bad = original; bad.clip.events[0].frameIndex = 100; MYE_EXPECT(!bad.Validate());
    bad = original; bad.imageSize.x = 0; MYE_EXPECT(!asset::AnimationAsset::FromJson(bad.ToJson()));
    ed::ProjectContext project;
    MYE_EXPECT(project.Create("paths", Utf8String(FreshRoot())));
    MYE_EXPECT(!project.OpenAnimation("../outside.anim"));
    auto* animation = project.NewAnimation();
    animation->Animation() = original;
    MYE_EXPECT(!project.SaveAnimation(animation->Id(), "outside-assets.anim"));
}

MYE_TEST(SpriteCornersRespectPivotScaleAndPosition) {
    const auto matrix = Mat4::TRS(Vec3{3, -2, 0}, Quat::Identity(), Vec3{0.5f, 2, 1});
    const auto corners = scene::SpriteCorners(matrix, {96, 48}, {48, 48}, 48);
    MYE_EXPECT(corners[0].x == 2.5f && corners[0].y == 0);
    MYE_EXPECT(corners[3].x == 3.5f && corners[3].y == -2);
    const auto rotated = scene::SpriteCorners(Mat4::TRS({}, Quat::FromAxisAngle({0, 0, 1}, kPi * 0.5f), {1, 1, 1}),
                                               {48, 48}, {24, 48}, 48);
    MYE_EXPECT(std::abs(rotated[0].x + 1) < 0.0001f && std::abs(rotated[0].y + 0.5f) < 0.0001f);
}

MYE_TEST(AssetScanPreservesGuidsAndRejectsDuplicateMetadata) {
    const auto root = FreshRoot();
    ed::ProjectContext project;
    MYE_EXPECT(project.Create("index", Utf8String(root), false, MYE_STARTER_SOURCE_DIR));
    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(root / "assets")), 0);
    auto device = rhi::CreateDevice(rhi::Backend::DX11, {});
    MYE_EXPECT(device);
    if (!device) return;
    asset::AssetManager assets(vfs, device.Value().get());
    assets.RegisterImporter(std::make_unique<asset::TextureImporter>());
    asset::AssetDatabase database(assets, nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(root / "assets")));
    const auto guid = database.GuidFromPath("assets://characters/novice.png");
    MYE_EXPECT(guid.IsValid());
    const auto texture = assets.LoadSync<asset::Texture>("assets://characters/novice.png");
    MYE_EXPECT(texture.IsLoaded() && texture.Guid() == guid);
    MYE_EXPECT(database.ScanDirectory(Utf8String(root / "assets")));
    MYE_EXPECT(database.GuidFromPath("assets://characters/novice.png") == guid);
    auto meta = ReadJsonFile(root / "assets/characters/novice.png.meta");
    MYE_EXPECT(meta);
    MYE_EXPECT(WriteJsonFile(root / "assets/animations/novice_idle.anim.meta", meta.Value()));
    MYE_EXPECT(!database.ScanDirectory(Utf8String(root / "assets")));
    MYE_EXPECT(database.GuidFromPath("assets://characters/novice.png") == guid);
}
