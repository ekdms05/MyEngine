// SceneReflectionTests.cpp — 코어 씬 컴포넌트 리플렉션/직렬화 왕복 (docs/03, M7)
//
// LocalTransform/SpriteRenderer가 실제 값(위치·스프라이트 GUID·틴트·피벗)과 함께 World↔JSON
// 왕복되는지 검증. 이게 성립해야 데이터드리븐 씬(에디터 저장/로드, 게임 런타임 로드)이 가능하다.
#include "TestFramework.h"

#include "mye/scene/SceneSerializer.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Camera3D.h"
#include "mye/scene/Camera2D.h"
#include "mye/scene/SpriteGeometry.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/anim/AnimationSystem.h"

#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/core/Math.h"
#include <limits>

using namespace mye;

namespace {
void RegisterSpriteComponents(ecs::World& w) {
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::LocalTransform>("LocalTransform"));
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::SpriteRenderer>("SpriteRenderer"));
}

MYE_TEST(ThreeDSceneReferencesCameraAndModesRoundtrip) {
    ecs::World world;
    scene::RegisterCoreComponents(world);
    const auto actor = world.Create();
    world.Add<scene::ObjectName>(actor).value = "Actor";
    world.Add<scene::LocalTransform>(actor).position = {3, 2, 4};
    world.Add<scene::WorldTransform>(actor);
    auto& billboard = world.Add<scene::BillboardRenderer>(actor);
    billboard.sprite.guid = {12, 34}; billboard.mode = scene::BillboardMode::Full;
    billboard.pivotPx = {24, 48}; billboard.flipX = true; billboard.sort.orderInLayer = 5;
    const auto meshEntity = world.Create();
    world.Add<scene::LocalTransform>(meshEntity);
    world.Add<scene::WorldTransform>(meshEntity);
    auto& mesh = world.Add<scene::MeshRenderer>(meshEntity);
    mesh.mesh.guid = {56, 78}; mesh.material.guid = {90, 12}; mesh.depthMode = 2;
    const auto cameraEntity = world.Create();
    world.Add<scene::LocalTransform>(cameraEntity).position = {0, 3, -8};
    world.Add<scene::WorldTransform>(cameraEntity);
    auto& camera = world.Add<scene::Camera3D>(cameraEntity);
    camera.followTarget = "Actor"; camera.target = {0, 1, 0}; camera.fovDegrees = 60;
    scene::UpdateWorldTransforms(world);
    auto before = scene::BuildGameView(world, render::Camera2D{});
    MYE_EXPECT(before && before.Value().geometryDepth);
    auto saved = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(saved);
    if (!saved) return;
    ecs::World restored;
    scene::RegisterCoreComponents(restored);
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(restored, saved.Value()));
    MYE_EXPECT(runtime::ValidateObjectComponents(restored));
    auto after = scene::BuildGameView(restored, render::Camera2D{});
    MYE_EXPECT(after && after.Value().geometryDepth);
    if (before && after) for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 4; ++col)
        MYE_EXPECT(ApproxEqual(before.Value().viewProj.m[row][col], after.Value().viewProj.m[row][col]));
    restored.Query<scene::BillboardRenderer>().Each([&](ecs::Entity, const auto& value) {
        MYE_EXPECT(value.sprite.guid == billboard.sprite.guid && value.flipX && value.mode == scene::BillboardMode::Full);
        MYE_EXPECT(value.pivotPx.y == 48 && value.sort.orderInLayer == 5);
    });
    restored.Query<scene::MeshRenderer>().Each([&](ecs::Entity, const auto& value) {
        MYE_EXPECT(value.mesh.guid == mesh.mesh.guid && value.material.guid == mesh.material.guid && value.depthMode == 2);
    });
    camera.nearPlane = 0; MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    camera.nearPlane = .05f; camera.followTarget = "Missing"; MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    camera.followTarget = "Actor";
    const auto duplicate = world.Create(); world.Add<scene::Camera3D>(duplicate);
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    world.Destroy(duplicate); world.TryGet<scene::Camera3D>(cameraEntity)->current = false;
    auto fallback = scene::BuildGameView(world, render::Camera2D{});
    MYE_EXPECT(fallback && !fallback.Value().geometryDepth);
}

MYE_TEST(TwoDSceneCameraSavesSettingsAndRefusesAmbiguousOrInvalidViews) {
    ecs::World world;
    scene::RegisterCoreComponents(world);
    const auto actor = world.Create(), cameraEntity = world.Create();
    world.Add<scene::ObjectName>(actor).value = "Actor";
    world.Add<scene::LocalTransform>(actor).position = {3, 2, 7};
    world.Add<scene::WorldTransform>(actor);
    world.Add<scene::LocalTransform>(cameraEntity).position = {100, 100, -20};
    world.Add<scene::WorldTransform>(cameraEntity);
    auto& camera = world.Add<scene::Camera2D>(cameraEntity);
    camera.followTarget = "Actor"; camera.offset = {.003f, 1}; camera.zoom = 2;
    camera.boundsEnabled = true; camera.bounds = {-20, -10, 40, 20};
    scene::UpdateWorldTransforms(world);
    MYE_EXPECT(runtime::ValidateObjectComponents(world));
    auto initial = scene::BuildGameView(world, render::Camera2D{}, 480, 270);
    MYE_EXPECT(initial && initial.Value().viewportWidth == 480 && initial.Value().viewportHeight == 270);
    MYE_EXPECT(!camera.initialized);
    MYE_EXPECT(scene::UpdateGameCamera2D(world, 0));
    MYE_EXPECT_NEAR(camera.view.Position().x, 3.003f, .0001f);
    MYE_EXPECT_NEAR(camera.view.Position().y, 3, .0001f);
    camera.view.AddShake(.2f, .5f);
    MYE_EXPECT(scene::UpdateGameCamera2D(world, .1f));
    const auto saved = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(saved);
    if (!saved) return;
    const auto text = json::Stringify(saved.Value());
    MYE_EXPECT(text.find("initialized") == std::string::npos && text.find("shakeElapsed") == std::string::npos);
    ecs::World loaded;
    scene::RegisterCoreComponents(loaded);
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(loaded, saved.Value()));
    MYE_EXPECT(runtime::ValidateObjectComponents(loaded));
    loaded.Query<scene::Camera2D>().Each([&](ecs::Entity, const auto& c) {
        MYE_EXPECT(c.followTarget == "Actor" && c.zoom == 2 && c.boundsEnabled);
        MYE_EXPECT(c.offset.y == 1 && !c.initialized && !c.view.IsShaking());
    });
    camera.zoom = std::numeric_limits<float>::quiet_NaN();
    MYE_EXPECT(!scene::BuildGameView(world, render::Camera2D{}));
    camera.zoom = 2; camera.deadzoneHalf.x = -1;
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    camera.deadzoneHalf.x = 2.5f; camera.bounds.w = 0;
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    camera.bounds.w = 40; camera.followTarget = "Missing";
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    camera.followTarget = "Actor";
    const auto other = world.Create();
    world.Add<scene::Camera3D>(other);
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    MYE_EXPECT(!scene::UpdateGameCamera2D(world, .1f));
    world.Destroy(other);
    MYE_EXPECT(runtime::ValidateObjectComponents(world));
    MYE_EXPECT(!scene::BuildGameView(world, render::Camera2D{}, 0, 540));
    const auto position = camera.view.Position();
    world.TryGet<scene::LocalTransform>(actor)->position.x = std::numeric_limits<float>::max();
    world.TryGet<scene::LocalTransform>(actor)->dirty = true;
    scene::UpdateWorldTransforms(world);
    camera.boundsEnabled = false;
    MYE_EXPECT(!scene::UpdateGameCamera2D(world, .1f));
    MYE_EXPECT(camera.view.Position() == position);
    world.TryGet<scene::LocalTransform>(actor)->position.x = 3;
    world.TryGet<scene::LocalTransform>(actor)->dirty = true;
    scene::UpdateWorldTransforms(world);
    camera.followTarget.clear(); camera.boundsEnabled = false;
    auto own = scene::ResolveGameCamera2D(world, cameraEntity);
    MYE_EXPECT(own);
    camera.initialized = false;
    own = scene::ResolveGameCamera2D(world, cameraEntity);
    MYE_EXPECT(own && ApproxEqual(own.Value().Position().x, 100.003f));
}

MYE_TEST(BillboardFacingKeepsFootAnchorAndAnimatesOneCursor) {
    const auto matrix = Mat4::TRS({2, 0, 3}, Quat::FromAxisAngle({0, 1, 0}, kPi * .5f), {2, 1, 1});
    const auto view = Mat4::LookAtLH({8, 4, -5}, {2, 0, 3}, {0, 1, 0});
    const auto full = scene::BillboardCorners(matrix, view, scene::BillboardMode::Full, {48, 48}, {24, 48}, 48);
    const auto upright = scene::BillboardCorners(matrix, view, scene::BillboardMode::YAxis, {48, 48}, {24, 48}, 48);
    const auto fixed = scene::BillboardCorners(matrix, view, scene::BillboardMode::None, {48, 48}, {24, 48}, 48);
    const auto right = (full[3] - full[1]).Normalized();
    MYE_EXPECT(ApproxEqual(Vec3::Dot(right, {view.m[0][0], view.m[1][0], view.m[2][0]}), 1));
    MYE_EXPECT(ApproxEqual(upright[0].y - upright[1].y, 1));
    MYE_EXPECT(ApproxEqual(fixed[0].x, fixed[2].x));
    const auto anchor = (full[1] + full[3]) * .5f;
    MYE_EXPECT(ApproxEqual(anchor.x, 2) && ApproxEqual(anchor.y, 0) && ApproxEqual(anchor.z, 3));
    ecs::World world;
    scene::RegisterCoreComponents(world);
    const auto entity = world.Create();
    auto& renderer = world.Add<scene::BillboardRenderer>(entity);
    auto& animator = world.Add<anim::SpriteAnimator>(entity);
    asset::SpriteSheet sheet;
    sheet.texture.guid = {1, 2};
    sheet.frames.resize(2); sheet.frames[1].uv = {.5f, 0, .5f, 1};
    sheet.frames[1].pivot = {24, 48}; sheet.frames[1].pivotInPixels = true;
    asset::AnimationClipData clip; clip.frameIndices = {0, 1}; clip.frameDurations = {.1f, .1f};
    animator.sheet = &sheet; animator.directClip = &clip;
    anim::RunAnimationSystem(world, .11f);
    MYE_EXPECT(animator.currentFrameIndex == 1 && ApproxEqual(renderer.srcUV.x, .5f));
    MYE_EXPECT(renderer.sprite.guid == sheet.texture.guid && renderer.pivotPx.y == 48);
}
} // namespace

MYE_TEST(SceneComponentReflectionRoundtrip) {
    scene::RegisterCoreComponentReflection();   // lazy 리플렉션 강제 등록

    ecs::World world;
    RegisterSpriteComponents(world);

    ecs::Entity e = world.Create();
    auto* lt = static_cast<scene::LocalTransform*>(
        world.AddDynamic(e, scene::LocalTransform::kComponentTypeId));
    MYE_EXPECT(lt != nullptr);
    lt->position = Vec3{3.0f, -4.0f, 2.5f};
    lt->scale = Vec3{2.0f, 2.0f, 1.0f};

    auto* sr = static_cast<scene::SpriteRenderer*>(
        world.AddDynamic(e, scene::SpriteRenderer::kComponentTypeId));
    MYE_EXPECT(sr != nullptr);
    sr->sprite.guid = asset::AssetGuid{0x1122334455667788ull, 0x99AABBCCDDEEFF00ull};
    sr->tint = Color{0.5f, 0.25f, 0.75f, 1.0f};
    sr->pivotPx = Vec2{24.0f, 48.0f};
    sr->flipX = true;
    sr->sort.sortLayer = 105;

    // World → JSON
    scene::SceneSerializer ser;
    auto json = ser.WriteWorld(world);
    MYE_EXPECT(static_cast<bool>(json));

    // JSON → 새 World
    ecs::World world2;
    RegisterSpriteComponents(world2);
    auto roots = ser.ReadInto(world2, json.Value());
    MYE_EXPECT(static_cast<bool>(roots));
    MYE_EXPECT(roots.Value().size() == 1);
    ecs::Entity e2 = roots.Value()[0];

    const auto* lt2 = static_cast<const scene::LocalTransform*>(
        world2.TryGetDynamic(e2, scene::LocalTransform::kComponentTypeId));
    MYE_EXPECT(lt2 != nullptr);
    MYE_EXPECT(ApproxEqual(lt2->position.x, 3.0f));
    MYE_EXPECT(ApproxEqual(lt2->position.y, -4.0f));
    MYE_EXPECT(ApproxEqual(lt2->position.z, 2.5f));
    MYE_EXPECT(ApproxEqual(lt2->scale.x, 2.0f));

    const auto* sr2 = static_cast<const scene::SpriteRenderer*>(
        world2.TryGetDynamic(e2, scene::SpriteRenderer::kComponentTypeId));
    MYE_EXPECT(sr2 != nullptr);
    MYE_EXPECT(sr2->sprite.guid.hi == 0x1122334455667788ull);
    MYE_EXPECT(sr2->sprite.guid.lo == 0x99AABBCCDDEEFF00ull);
    MYE_EXPECT(ApproxEqual(sr2->tint.r, 0.5f));
    MYE_EXPECT(ApproxEqual(sr2->tint.g, 0.25f));
    MYE_EXPECT(ApproxEqual(sr2->pivotPx.x, 24.0f));
    MYE_EXPECT(ApproxEqual(sr2->pivotPx.y, 48.0f));
    MYE_EXPECT(sr2->flipX == true);
    MYE_EXPECT(sr2->sort.sortLayer == 105);
}

MYE_TEST(SceneRejectsMalformedAssetReferenceBeforeChangingWorld) {
    ecs::World world;
    scene::RegisterCoreComponents(world);
    const auto original = world.Create();
    world.Add<scene::ObjectName>(original).value = "Keep";
    scene::SceneSerializer serializer;
    const auto before = serializer.WriteWorld(world);
    MYE_EXPECT(before);
    if (!before) return;
    for (const auto* source : {
        R"({"__version":1,"entities":[{"id":1,"components":{"SpriteRenderer":{"sprite":{"guid":"invalid","type":"0"}}}}]})",
        R"({"__version":1,"entities":[{"id":1,"components":{"MeshRenderer":{"mesh":{"guid":"00112233-4455-6677-8899-aabbccddeeff-extra","type":"0"}}}}]})",
        R"({"__version":1,"entities":[{"id":1,"components":{"MeshRenderer":{"material":{"guid":"00112233-4455-6677-8899-aabbccddeeff","type":"18446744073709551616"}}}}]})"}) {
        const auto parsed = json::Parse(source);
        MYE_EXPECT(parsed);
        if (!parsed) continue;
        const auto loaded = serializer.ReadInto(world, parsed.Value());
        MYE_EXPECT(!loaded);
        if (!loaded) {
            MYE_EXPECT(loaded.GetError().message.find("entity 1 component") != std::string::npos);
            MYE_EXPECT(loaded.GetError().message.find("Invalid asset reference") != std::string::npos);
        }
        MYE_EXPECT(world.Valid(original));
        const auto after = serializer.WriteWorld(world);
        MYE_EXPECT(after);
        if (after) MYE_EXPECT(json::Stringify(before.Value()) == json::Stringify(after.Value()));
    }
}
