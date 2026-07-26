// SceneReflectionTests.cpp — 코어 씬 컴포넌트 리플렉션/직렬화 왕복 (docs/03, M7)
//
// LocalTransform/SpriteRenderer가 실제 값(위치·스프라이트 GUID·틴트·피벗)과 함께 World↔JSON
// 왕복되는지 검증. 이게 성립해야 데이터드리븐 씬(에디터 저장/로드, 게임 런타임 로드)이 가능하다.
#include "TestFramework.h"

#include "mye/scene/SceneSerializer.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"

#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/core/Math.h"

using namespace mye;

namespace {
void RegisterSpriteComponents(ecs::World& w) {
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::LocalTransform>("LocalTransform"));
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::SpriteRenderer>("SpriteRenderer"));
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
