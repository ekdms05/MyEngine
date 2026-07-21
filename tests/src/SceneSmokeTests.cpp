// SceneSmokeTests.cpp — M2-B 헤드리스 씬 시뮬 스모크 (docs/03 §4·§5·§8)
//
// 렌더 없이 데이터·시뮬레이션만으로 M2-B 통합 목표를 검증한다:
//   1) 작은 타일맵 구성: (a) 지면+다리 다중 컬럼 셀 1케이스, (b) 경사(램프) 셀 1케이스.
//   2) KinematicBody2D 캐릭터가 경사로를 오르며 SampleHeight로 heightLevel/FloorLevel 상승.
//   3) 다리 위 캐릭터 A와 다리 아래 캐릭터 B가 같은 XY라도 floorLevel이 다르면 비충돌.
//   4) 다리 위 트리거 진입 시 TriggerEnterEvent 발행(월드 EventBus).
//   5) RenderExtract가 두 캐릭터에 올바른 floorLevel·sortLayer(World 밴드)·sortKeyY 공급.
//
// 물리 월드는 FloorLevel을 자동 갱신하지 않는다(snapToGround 소스 미주입). 층 전이는
// M2-B 계약대로 "이동 시스템(게임 로직)"이 타일 SampleHeight를 질의해 결정한다 — 여기서
// 그 로직을 시뮬레이션으로 재현하고, 물리는 floorLevel 필터·트리거만 담당한다.
#include "TestFramework.h"

#include "mye/core/Events.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/tilemap/Tilemap.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

using namespace mye;
using mye::ecs::Entity;
using mye::ecs::World;
using mye::phys::Collider2D;
using mye::phys::KinematicBody2D;
using mye::phys::PhysicsWorld2D;
using mye::phys::Shape2D;
using mye::phys::TriggerEnterEvent;
using mye::phys::TriggerExitEvent;
using mye::scene::FloorLevel;
using mye::scene::LocalTransform;
using mye::scene::WorldTransform;
namespace tmap = mye::tilemap;

namespace {

// 위치 + 콜라이더(+선택 트리거)를 가진 엔티티 생성.
Entity SpawnBody(World& w, Vec2 pos, Shape2D shape, int8_t floor,
                 bool trigger = false, uint32_t triggerId = 0) {
    Entity e = w.Create();
    auto& lt = w.Add<LocalTransform>(e);
    lt.position = Vec3{pos.x, pos.y, 0};
    auto& wt = w.Add<WorldTransform>(e);
    wt.matrix.m[3][0] = pos.x;
    wt.matrix.m[3][1] = pos.y;
    auto& col = w.Add<Collider2D>(e);
    col.shape = shape;
    col.isTrigger = trigger;
    col.triggerId = triggerId;
    w.Add<FloorLevel>(e).level = floor;
    return e;
}

Vec2 BodyXY(World& w, Entity e) {
    auto* lt = w.TryGet<LocalTransform>(e);
    return Vec2{lt->position.x, lt->position.y};
}

// 이동 시스템(게임 로직)이 하는 지면 추종: 현재 XY 셀의 "지면(비다리) 컬럼"을 찾아
// 그 컬럼의 baseHeightLevel로 SampleHeight를 호출해 경사 보간된 지면 높이를 얻는다.
// 램프처럼 셀마다 base가 다른 연속 지형에서 캐릭터 층이 자연히 따라 오른다.
// 지면 컬럼이 없으면 nullopt.
std::optional<float> SampleGroundFollow(const tmap::TilemapWorld& map, Vec2 xy) {
    const int cx = static_cast<int>(std::floor(xy.x));
    const int cy = static_cast<int>(std::floor(xy.y));
    auto cols = map.ColumnsAt(Vec2i{cx, cy});
    const tmap::TileColumn* ground = nullptr;
    for (const auto& c : cols) {
        if (c.IsBridge()) continue;              // 다리 컬럼은 지면 추종 대상 아님
        if (!ground || c.baseHeightLevel < ground->baseHeightLevel) ground = &c;
    }
    if (!ground) return std::nullopt;
    return map.SampleHeight(xy, static_cast<int8_t>(ground->baseHeightLevel));
}

} // namespace

// ---------------------------------------------------------------------------
// 경사로 등반: 캐릭터가 램프 셀을 지나며 heightLevel(및 FloorLevel)이 단조 상승.
// 물리 이동(move-and-slide) + 게임 로직(SampleHeight → heightLevel 전이)을 결합.
// ---------------------------------------------------------------------------
MYE_TEST(SmokeSlopeClimbRaisesHeightLevel) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    // 타일맵: 셀 (0..3, 0)은 평지 base 0, 셀 (4,0)~(9,0)은 급경사 동향(SteepE, +2/셀).
    // 급경사로 6칸 = 12레벨 = 6 world unit 상승. 완경사도 함께 하나 둔다(케이스 다양).
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);
    for (int x = 0; x < 4; ++x) {
        tmap::TileColumn flat; flat.tile = 1; flat.baseHeightLevel = 0;
        ground.SetTile(Vec2i{x, 0}, flat);
    }
    // 램프: 각 셀의 base는 이전 셀 정상 높이(연속). SteepE = 셀당 +2레벨.
    for (int x = 4; x < 10; ++x) {
        tmap::TileColumn ramp; ramp.tile = 2;
        ramp.baseHeightLevel = (x - 4) * 2;   // 셀 서단 높이(연속)
        ramp.slope = tmap::SlopeType::SteepE;
        ground.SetTile(Vec2i{x, 0}, ramp);
    }

    // 캐릭터: 셀 (0,0) 중앙에서 출발, +X로 이동. floor 0에서 시작.
    Entity hero = SpawnBody(w, Vec2{0.5f, 0.5f}, Shape2D::MakeBox(0.2f, 0.2f), 0);
    auto& kb = w.Add<KinematicBody2D>(hero);
    kb.velocity = Vec2{2.0f, 0.0f};   // world unit/s

    auto h0 = SampleGroundFollow(map, Vec2{0.5f, 0.5f});
    MYE_EXPECT(h0.has_value());
    int startLevel = tmap::WorldYToHeightLevel(h0.value_or(0.0f));
    MYE_EXPECT(startLevel == 0);

    // 시뮬레이션 루프: 물리 스텝 후, 현재 XY 셀의 지면 컬럼을 추종해 heightLevel 갱신.
    // 게임 로직이 지면 높이(경사 보간)를 FloorLevel(정수 층)·시각 Y로 반영한다.
    const float dt = 1.0f / 60.0f;
    int maxHeightLevel = 0;
    float maxHeightY = 0.0f;
    std::vector<int> heightTrace;
    int prevSampleLevel = 0;
    bool monotonic = true;

    for (int step = 0; step < 400; ++step) {
        phys.Step(w, &bus, dt);
        Vec2 xy = BodyXY(w, hero);
        auto gh = SampleGroundFollow(map, xy);
        if (!gh) { if (xy.x >= 9.5f) break; continue; }
        float h = *gh;
        int hl = tmap::WorldYToHeightLevel(h);
        heightTrace.push_back(hl);
        if (hl < prevSampleLevel) monotonic = false;
        prevSampleLevel = hl;
        if (hl > maxHeightLevel) maxHeightLevel = hl;
        if (h > maxHeightY) maxHeightY = h;
        // 게임 로직: 지면 높이를 시각적 Z(2.5D 상승)와 FloorLevel(정수 층)에 반영.
        w.TryGet<LocalTransform>(hero)->position.z = h;
        w.TryGet<FloorLevel>(hero)->level = static_cast<int8_t>(hl / 2);   // 2레벨=1층
        // 정상(램프 끝) 통과하면 멈춤.
        if (xy.x >= 9.5f) break;
    }

    // 램프 끝 정상: 셀 9 base = 10레벨, 동단(SteepE) = base + 2 = 12레벨 = 6 unit.
    // 캐릭터가 x≈9.5 도달 시 셀9 중앙(fx=0.5) → base10 + 1레벨 = 11레벨(5.5 unit).
    MYE_EXPECT(maxHeightLevel >= 10);          // 최소 5 unit 이상 상승
    MYE_EXPECT(maxHeightY >= 5.0f);
    MYE_EXPECT(monotonic);                     // 등반 중 높이 단조 비감소
    MYE_EXPECT(BodyXY(w, hero).x >= 9.0f);     // 램프 끝까지 도달
    MYE_EXPECT(maxHeightLevel > startLevel);   // 시작보다 확실히 높다
    std::printf("    [smoke] slope climb: startLevel=%d -> maxHeightLevel=%d "
                "(%.2f unit) finalX=%.2f floor=%d monotonic=%d\n",
                startLevel, maxHeightLevel, maxHeightY, BodyXY(w, hero).x,
                static_cast<int>(w.TryGet<FloorLevel>(hero)->level), monotonic ? 1 : 0);
}

// ---------------------------------------------------------------------------
// 다리 위/아래 분리: 다리 위 A(floor 1)와 다리 아래 B(floor 0)가 같은 XY에서 비충돌.
// 다리 셀 데이터(지면+다리 컬럼)로 두 층을 표현하고, 물리 floorLevel 필터로 통과 검증.
// ---------------------------------------------------------------------------
MYE_TEST(SmokeBridgeAboveBelowNoCollision) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    // 타일맵: 셀 (5,5)에 지면(레벨0) + 다리(레벨2 = 1칸 위) 컬럼 공존.
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);
    tmap::TileColumn groundCol; groundCol.tile = 1; groundCol.baseHeightLevel = 0;
    tmap::TileColumn bridgeCol; bridgeCol.tile = 2; bridgeCol.baseHeightLevel = 2;
    bridgeCol.flags = tmap::ColumnFlags::OneWayBridge;
    ground.SetTile(Vec2i{5, 5}, groundCol);
    ground.AddBridge(Vec2i{5, 5}, bridgeCol);

    // 데이터 계약 확인: 셀에 두 컬럼(지면+다리)이 존재.
    MYE_EXPECT(map.ColumnsAt(Vec2i{5, 5}).size() == 2);
    MYE_EXPECT(map.ColumnAtLevel(Vec2i{5, 5}, 0).has_value());   // 지면
    MYE_EXPECT(map.ColumnAtLevel(Vec2i{5, 5}, 2).has_value());   // 다리

    // 아래 캐릭터 B: floor 0, 다리 셀 위치에 정지.
    Entity below = SpawnBody(w, Vec2{5.5f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 0);

    // 위 캐릭터 A: floor 1(다리 위), 같은 XY로 이동해 통과. 물리적으로 겹치지만 층이 달라 비충돌.
    Entity above = SpawnBody(w, Vec2{3.0f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 1);
    auto& akb = w.Add<KinematicBody2D>(above);
    akb.velocity = Vec2{2.0f, 0.0f};   // B를 향해 통과

    // 여러 스텝: A가 B의 XY를 지나쳐 이동해야 하며 벽 충돌 없어야 한다.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 300; ++i) {
        phys.Step(w, &bus, dt);
        if (BodyXY(w, above).x >= 8.0f) break;
    }
    // A가 B(x=5.5)를 지나 멀리 이동 → 층 분리로 통과 성공.
    MYE_EXPECT(BodyXY(w, above).x >= 7.0f);
    MYE_EXPECT(!w.TryGet<KinematicBody2D>(above)->hitWall);
    // B는 밀려나지 않음(정지 유지).
    MYE_EXPECT_NEAR(BodyXY(w, below).x, 5.5f, 1e-3f);
    MYE_EXPECT_NEAR(BodyXY(w, below).y, 5.5f, 1e-3f);

    // 대조: 같은 층(둘 다 floor 0)이면 A가 B에 막힌다.
    World w2; EventBus bus2; w2.SetEventBus(&bus2);
    PhysicsWorld2D phys2(1.0f);
    Entity b2 = SpawnBody(w2, Vec2{5.5f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 0);
    (void)b2;
    Entity a2 = SpawnBody(w2, Vec2{3.0f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 0);
    auto& a2kb = w2.Add<KinematicBody2D>(a2);
    a2kb.velocity = Vec2{2.0f, 0.0f};
    bool blocked = false;
    for (int i = 0; i < 300; ++i) {
        phys2.Step(w2, &bus2, dt);
        if (w2.TryGet<KinematicBody2D>(a2)->hitWall) blocked = true;
    }
    // 같은 층에서는 B 좌면(5.5-0.3=5.2)에 A 우면이 막힘 → A 중심 ≤ 5.2-0.3 근방.
    MYE_EXPECT(blocked);
    MYE_EXPECT(BodyXY(w2, a2).x < 5.3f);
}

// ---------------------------------------------------------------------------
// 다리 위 트리거: 다리 위(floor 1)에 트리거 셀을 두고, 위 캐릭터 진입 시 Enter 발행.
// 아래(floor 0) 캐릭터는 같은 XY라도 트리거를 발동하지 않아야 한다(층 필터).
// ---------------------------------------------------------------------------
MYE_TEST(SmokeBridgeTriggerFloorFiltered) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    int enterAbove = 0, exitAbove = 0;
    uint32_t lastEnterId = 0;
    bus.Subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        ++enterAbove; lastEnterId = e.triggerId; return false;
    });
    bus.Subscribe<TriggerExitEvent>([&](const TriggerExitEvent&) { ++exitAbove; return false; });

    // 다리 위 트리거(floor 1), triggerId 99. 중심 x=6.
    Entity trig = SpawnBody(w, Vec2{6.0f, 5.5f}, Shape2D::MakeBox(0.5f, 0.5f), 1,
                            /*trigger*/ true, /*id*/ 99);
    (void)trig;

    // 아래 캐릭터(floor 0)가 같은 XY 라인을 통과 — 층이 달라 트리거 발동 안 함.
    Entity below = SpawnBody(w, Vec2{3.0f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 0);
    auto& bkb = w.Add<KinematicBody2D>(below);
    bkb.velocity = Vec2{3.0f, 0.0f};

    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) phys.Step(w, &bus, dt);
    // 아래 캐릭터는 트리거를 통과했지만 층이 달라 이벤트 없음.
    MYE_EXPECT(BodyXY(w, below).x > 8.0f);
    MYE_EXPECT(enterAbove == 0);
    MYE_EXPECT(exitAbove == 0);

    // 위 캐릭터(floor 1)가 같은 트리거를 통과 → Enter 1, Exit 1.
    Entity above = SpawnBody(w, Vec2{3.0f, 5.5f}, Shape2D::MakeBox(0.3f, 0.3f), 1);
    auto& akb = w.Add<KinematicBody2D>(above);
    akb.velocity = Vec2{3.0f, 0.0f};
    for (int i = 0; i < 200; ++i) phys.Step(w, &bus, dt);

    MYE_EXPECT(enterAbove == 1);
    MYE_EXPECT(exitAbove == 1);
    MYE_EXPECT(lastEnterId == 99);
}

// ---------------------------------------------------------------------------
// RenderExtract 통합: 다리 위 A와 아래 B가 같은 화면 Y지만 올바른 floorLevel·sortLayer·
// sortKeyY로 공급돼 02가 깊이 분리할 수 있어야 한다.
// ---------------------------------------------------------------------------
MYE_TEST(SmokeRenderExtractBridgeDepthData) {
    World w;

    // 다리 위 A(floor 1)와 아래 B(floor 0)가 같은 Transform.y = 5.5(같은 스크린 Y).
    Entity above = w.Create();
    auto& sa = w.Add<scene::SpriteRenderer>(above);
    sa.sort.orderInLayer = 1;
    w.Add<scene::WorldTransform>(above).matrix = Mat4::Translation(Vec3{6, 5.5f, 0});
    w.Add<scene::FloorLevel>(above).level = 1;

    Entity below = w.Create();
    auto& sb = w.Add<scene::SpriteRenderer>(below);
    sb.sort.orderInLayer = 0;
    w.Add<scene::WorldTransform>(below).matrix = Mat4::Translation(Vec3{6, 5.5f, 0});
    w.Add<scene::FloorLevel>(below).level = 0;

    scene::RenderProxyList list;
    scene::ExtractRenderItems(w, list);
    MYE_EXPECT(list.Size() == 2);

    const scene::RenderItem* itA = nullptr;
    const scene::RenderItem* itB = nullptr;
    for (const auto& it : list.items) {
        if (it.floorLevel == 1) itA = &it;
        else if (it.floorLevel == 0) itB = &it;
    }
    MYE_EXPECT(itA != nullptr && itB != nullptr);
    if (itA && itB) {
        // floorLevel 원본값 공급.
        MYE_EXPECT(itA->floorLevel == 1);
        MYE_EXPECT(itB->floorLevel == 0);
        // sortLayer: World k 밴드로 변환, A(위)가 상위 밴드.
        MYE_EXPECT(itA->sortLayer == scene::kSortLayerWorldBase + 1);
        MYE_EXPECT(itB->sortLayer == scene::kSortLayerWorldBase);
        MYE_EXPECT(itA->sortLayer > itB->sortLayer);
        // sortKeyY = Transform.y + FloorLevel 높이(1레벨 = 0.5). A = 6.0, B = 5.5.
        MYE_EXPECT_NEAR(itA->sortKeyY, 6.0f, 1e-3f);
        MYE_EXPECT_NEAR(itB->sortKeyY, 5.5f, 1e-3f);
        // 같은 스크린 Y(5.5)라도 sortKeyY로 A가 뒤(더 큼) — 02 깊이 분리 데이터 정확.
        MYE_EXPECT(itA->sortKeyY > itB->sortKeyY);
    }
}
