// TilemapTests.cpp — 타일맵(높이·경사·다리) + RenderExtract 단위 테스트 (M2-B / docs/03 §4·§5)
//
// 검증 불변식:
//   - 청크 경계 조회(음수 셀 floor 나눗셈, 로컬 좌표 환산)
//   - 다중 컬럼(다리) 조회: 같은 셀에 지면 + 다리 컬럼 공존
//   - heightLevel ↔ 월드 Y 환산(PPU 48, 반칸=0.5unit)
//   - 경사 높이 보간(셀 내 선형 보간, 방향별)
//   - RenderExtract가 올바른 sortLayer(FloorLevel→World 밴드)·sortKeyY·floorLevel 산출
//     (정렬은 하지 않지만 값 정확)
#include "TestFramework.h"

#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/tilemap/Tilemap.h"

#include <algorithm>

using namespace mye;
using mye::ecs::Entity;
using mye::ecs::World;
namespace tmap = mye::tilemap;

// ---------------------------------------------------------------------------
// heightLevel ↔ 월드 Y 환산 (PPU 48: 1레벨 = 반칸 = 24px = 0.5 unit)
// ---------------------------------------------------------------------------
MYE_TEST(TilemapHeightLevelToWorldY) {
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToWorldY(0), 0.0f));
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToWorldY(1), 0.5f));   // 반칸
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToWorldY(2), 1.0f));   // 한칸
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToWorldY(-2), -1.0f));
    // 픽셀 환산
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToPixels(1), 24.0f));
    MYE_EXPECT(ApproxEqual(tmap::HeightLevelToPixels(2), 48.0f));
    // 역환산(반올림)
    MYE_EXPECT(tmap::WorldYToHeightLevel(0.5f) == 1);
    MYE_EXPECT(tmap::WorldYToHeightLevel(1.0f) == 2);
    MYE_EXPECT(tmap::WorldYToHeightLevel(0.24f) == 0);
    MYE_EXPECT(tmap::WorldYToHeightLevel(-1.0f) == -2);
}

// ---------------------------------------------------------------------------
// 청크 좌표 환산 — 경계·음수 floor 나눗셈
// ---------------------------------------------------------------------------
MYE_TEST(TilemapChunkCoordConversion) {
    // 청크 크기 32.
    MYE_EXPECT(tmap::CellToChunk(Vec2i{0, 0}) == (Vec2i{0, 0}));
    MYE_EXPECT(tmap::CellToChunk(Vec2i{31, 31}) == (Vec2i{0, 0}));
    MYE_EXPECT(tmap::CellToChunk(Vec2i{32, 0}) == (Vec2i{1, 0}));
    MYE_EXPECT(tmap::CellToChunk(Vec2i{-1, -1}) == (Vec2i{-1, -1}));   // 음수 floor
    MYE_EXPECT(tmap::CellToChunk(Vec2i{-32, 0}) == (Vec2i{-1, 0}));
    MYE_EXPECT(tmap::CellToChunk(Vec2i{-33, 0}) == (Vec2i{-2, 0}));

    MYE_EXPECT(tmap::CellToLocal(Vec2i{0, 0}) == (Vec2i{0, 0}));
    MYE_EXPECT(tmap::CellToLocal(Vec2i{33, 0}) == (Vec2i{1, 0}));
    MYE_EXPECT(tmap::CellToLocal(Vec2i{-1, 0}) == (Vec2i{31, 0}));   // 음수 로컬 랩
    MYE_EXPECT(tmap::CellToLocal(Vec2i{-32, 0}) == (Vec2i{0, 0}));
}

// ---------------------------------------------------------------------------
// 청크 경계 조회 — 다른 청크에 걸친 셀이 올바르게 저장·조회
// ---------------------------------------------------------------------------
MYE_TEST(TilemapChunkBoundaryQuery) {
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);

    tmap::TileColumn a; a.tile = 10; a.baseHeightLevel = 0;
    tmap::TileColumn b; b.tile = 20; b.baseHeightLevel = 1;

    ground.SetTile(Vec2i{31, 0}, a);   // 청크(0,0) 마지막 열
    ground.SetTile(Vec2i{32, 0}, b);   // 청크(1,0) 첫 열

    // 서로 다른 청크에 저장됨 → 청크 2개.
    MYE_EXPECT(ground.ChunkCount() == 2);

    auto colsA = ground.ColumnsAt(Vec2i{31, 0});
    auto colsB = ground.ColumnsAt(Vec2i{32, 0});
    MYE_EXPECT(colsA.size() == 1 && colsA[0].tile == 10);
    MYE_EXPECT(colsB.size() == 1 && colsB[0].tile == 20);

    // 존재하지 않는 셀은 빈 span.
    MYE_EXPECT(ground.ColumnsAt(Vec2i{5, 5}).empty());
    // 음수 셀도 조회 가능.
    ground.SetTile(Vec2i{-1, -1}, a);
    MYE_EXPECT(ground.ColumnsAt(Vec2i{-1, -1}).size() == 1);
}

// ---------------------------------------------------------------------------
// 다중 컬럼(다리) — 같은 셀에 지면 + 다리 컬럼 공존
// ---------------------------------------------------------------------------
MYE_TEST(TilemapMultiColumnBridge) {
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);

    // 지면 컬럼(레벨 0) + 다리 컬럼(레벨 4 = 두 칸 위).
    tmap::TileColumn groundCol; groundCol.tile = 1; groundCol.baseHeightLevel = 0;
    tmap::TileColumn bridgeCol; bridgeCol.tile = 2; bridgeCol.baseHeightLevel = 4;
    bridgeCol.flags = tmap::ColumnFlags::OneWayBridge;

    ground.SetTile(Vec2i{10, 10}, groundCol);
    ground.AddBridge(Vec2i{10, 10}, bridgeCol);

    auto cols = ground.ColumnsAt(Vec2i{10, 10});
    MYE_EXPECT(cols.size() == 2);

    // 지면 + 다리가 모두 조회된다(순서 무관하게 존재 확인).
    bool hasGround = false, hasBridge = false;
    for (const auto& c : cols) {
        if (c.baseHeightLevel == 0 && c.tile == 1) hasGround = true;
        if (c.baseHeightLevel == 4 && c.IsBridge()) hasBridge = true;
    }
    MYE_EXPECT(hasGround && hasBridge);

    // TilemapWorld 통합 조회도 두 컬럼 반환.
    MYE_EXPECT(map.ColumnsAt(Vec2i{10, 10}).size() == 2);

    // 레벨별 컬럼 선택(다리 위/아래 판정). 값 반환이라 두 컬럼을 동시에 보관·비교 가능.
    auto atBridge = map.ColumnAtLevel(Vec2i{10, 10}, 4);
    auto atGround = map.ColumnAtLevel(Vec2i{10, 10}, 0);
    MYE_EXPECT(atBridge.has_value() && atBridge->tile == 2);
    MYE_EXPECT(atGround.has_value() && atGround->tile == 1);
    MYE_EXPECT(!map.ColumnAtLevel(Vec2i{10, 10}, 7).has_value());
}

// ---------------------------------------------------------------------------
// 경사 높이 보간 — 셀 내 선형 보간, 방향별
// ---------------------------------------------------------------------------
MYE_TEST(TilemapSlopeInterpolation) {
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);

    // 셀 (0,0): 완경사 북(+Y로 상승), base 레벨 0 → 남단 0, 북단 +1레벨(0.5 unit).
    tmap::TileColumn slope; slope.tile = 1; slope.baseHeightLevel = 0;
    slope.slope = tmap::SlopeType::GentleN;
    ground.SetTile(Vec2i{0, 0}, slope);

    // 남단(y=0.0): 높이 0. 중앙(y=0.5): 0.25. 북단(y≈1.0): 0.5.
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{0.5f, 0.0f}, 0), 0.0f, 1e-3f));
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{0.5f, 0.5f}, 0), 0.25f, 1e-3f));
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{0.5f, 0.99f}, 0), 0.495f, 1e-2f));

    // 급경사(Steep)는 셀당 +2레벨(1.0 unit) 상승.
    tmap::TileColumn steep; steep.tile = 1; steep.baseHeightLevel = 0;
    steep.slope = tmap::SlopeType::SteepE;   // +X로 상승
    ground.SetTile(Vec2i{1, 0}, steep);
    // 셀 (1,0) 내부: worldX 1.0(서단)=0, 1.5(중앙)=0.5, 2.0(동단)=1.0.
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{1.0f, 0.5f}, 0), 0.0f, 1e-3f));
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{1.5f, 0.5f}, 0), 0.5f, 1e-3f));

    // 평지 컬럼은 base 높이 그대로.
    tmap::TileColumn flat; flat.tile = 1; flat.baseHeightLevel = 2;   // 1.0 unit
    ground.SetTile(Vec2i{5, 5}, flat);
    MYE_EXPECT(ApproxEqual(map.SampleHeight(Vec2{5.5f, 5.5f}, 2), 1.0f, 1e-3f));
}

// ---------------------------------------------------------------------------
// FloorLevel → World 밴드 sortLayer 변환
// ---------------------------------------------------------------------------
MYE_TEST(RenderExtractFloorLevelToSortLayer) {
    MYE_EXPECT(scene::FloorLevelToSortLayer(0) == scene::kSortLayerWorldBase);
    MYE_EXPECT(scene::FloorLevelToSortLayer(1) == scene::kSortLayerWorldBase + 1);
    MYE_EXPECT(scene::FloorLevelToSortLayer(3) == scene::kSortLayerWorldBase + 3);
    // 음수 층은 World 0로 클램프.
    MYE_EXPECT(scene::FloorLevelToSortLayer(-1) == scene::kSortLayerWorldBase);
}

// ---------------------------------------------------------------------------
// RenderExtract — sortLayer / sortKeyY / floorLevel 산출 정확성 (정렬은 안 함)
// ---------------------------------------------------------------------------
MYE_TEST(RenderExtractSpriteValues) {
    World w;

    // 다리 위 캐릭터 A: FloorLevel 1, Transform.y = 3.0.
    Entity a = w.Create();
    auto& sa = w.Add<scene::SpriteRenderer>(a);
    sa.sort.sortLayer = 0; sa.sort.orderInLayer = 5;
    w.Add<scene::WorldTransform>(a).matrix = Mat4::Translation(Vec3{2, 3, 0});
    w.Add<scene::FloorLevel>(a).level = 1;

    // 다리 아래 캐릭터 B: FloorLevel 0, Transform.y = 3.0(같은 화면 Y).
    Entity b = w.Create();
    auto& sb = w.Add<scene::SpriteRenderer>(b);
    sb.sort.orderInLayer = -2;
    w.Add<scene::WorldTransform>(b).matrix = Mat4::Translation(Vec3{2, 3, 0});
    w.Add<scene::FloorLevel>(b).level = 0;

    scene::RenderProxyList list;
    scene::ExtractRenderItems(w, list);
    MYE_EXPECT(list.Size() == 2);

    // 항목을 엔티티 대응으로 찾기(추출은 정렬하지 않으므로 순서 불특정).
    const scene::RenderItem* itemA = nullptr;
    const scene::RenderItem* itemB = nullptr;
    for (const auto& it : list.items) {
        if (it.floorLevel == 1) itemA = &it;
        else if (it.floorLevel == 0) itemB = &it;
    }
    MYE_EXPECT(itemA != nullptr && itemB != nullptr);
    if (itemA && itemB) {
        // A: FloorLevel 1 → World 1 밴드. sortKeyY = 3.0 + 0.5(1레벨) = 3.5.
        MYE_EXPECT(itemA->sortLayer == scene::kSortLayerWorldBase + 1);
        MYE_EXPECT(ApproxEqual(itemA->sortKeyY, 3.5f, 1e-3f));
        MYE_EXPECT(itemA->orderInLayer == 5);
        // B: FloorLevel 0 → World 0 밴드. sortKeyY = 3.0.
        MYE_EXPECT(itemB->sortLayer == scene::kSortLayerWorldBase);
        MYE_EXPECT(ApproxEqual(itemB->sortKeyY, 3.0f, 1e-3f));
        MYE_EXPECT(itemB->orderInLayer == -2);
        // 다리 밴드 분리: A가 B보다 상위 밴드(sortLayer 큼) — 02가 이 값으로 깊이 분리.
        MYE_EXPECT(itemA->sortLayer > itemB->sortLayer);
    }
}

// FloorLevel이 없는 렌더러블은 컴포넌트 자체 sortLayer를 그대로 쓴다.
MYE_TEST(RenderExtractNoFloorLevel) {
    World w;
    Entity e = w.Create();
    auto& sr = w.Add<scene::SpriteRenderer>(e);
    sr.sort.sortLayer = 42;
    w.Add<scene::WorldTransform>(e).matrix = Mat4::Translation(Vec3{0, 7, 0});

    scene::RenderProxyList list;
    scene::ExtractRenderItems(w, list);
    MYE_EXPECT(list.Size() == 1);
    MYE_EXPECT(list.items[0].sortLayer == 42);          // 변환 없음
    MYE_EXPECT(ApproxEqual(list.items[0].sortKeyY, 7.0f, 1e-3f));
    MYE_EXPECT(list.items[0].floorLevel == 0);

    // visible=false는 추출에서 제외.
    sr.visible = false;
    scene::ExtractRenderItems(w, list);
    MYE_EXPECT(list.Size() == 0);
}

// ---------------------------------------------------------------------------
// RenderExtract — 타일맵 청크(가시 청크만 청크 프록시 산출)
// ---------------------------------------------------------------------------
MYE_TEST(RenderExtractTilemapChunks) {
    World w;
    tmap::TilemapWorld map;
    tmap::TileLayer& ground = map.GetOrCreateLayer(0);

    tmap::TileColumn c; c.tile = 1;
    ground.SetTile(Vec2i{0, 0}, c);      // 청크 (0,0)
    ground.SetTile(Vec2i{40, 40}, c);    // 청크 (1,1)

    Entity e = w.Create();
    auto& tr = w.Add<scene::TilemapRenderer>(e);
    tr.runtime = &map;
    tr.sort.sortLayer = 7;
    w.Add<scene::WorldTransform>(e).matrix = Mat4::Identity();

    scene::RenderProxyList list;
    scene::ExtractRenderItems(w, list);

    // 컬럼이 있는 청크 2개 → 청크 프록시 2개.
    int chunkItems = 0;
    for (const auto& it : list.items)
        if (it.kind == scene::RenderItemKind::TilemapChunk) {
            ++chunkItems;
            MYE_EXPECT(it.sortLayer == 7);
            MYE_EXPECT(it.tilemap == &map);
            MYE_EXPECT(it.tileLayer == 0);
        }
    MYE_EXPECT(chunkItems == 2);
}

// 혼합 렌더러블(스프라이트 + 메시 + 타일맵)이 한 번의 추출로 모두 수집된다.
MYE_TEST(RenderExtractMixedKinds) {
    World w;

    Entity s = w.Create();
    w.Add<scene::SpriteRenderer>(s);
    w.Add<scene::WorldTransform>(s);

    Entity m = w.Create();
    auto& mr = w.Add<scene::MeshRenderer>(m);
    mr.depthMode = 2;
    w.Add<scene::WorldTransform>(m).matrix = Mat4::Translation(Vec3{0, 9, 0});
    w.Add<scene::FloorLevel>(m).level = 2;

    scene::RenderProxyList list;
    scene::ExtractRenderItems(w, list);

    int sprites = 0, meshes = 0;
    for (const auto& it : list.items) {
        if (it.kind == scene::RenderItemKind::Sprite) ++sprites;
        if (it.kind == scene::RenderItemKind::Mesh) {
            ++meshes;
            MYE_EXPECT(it.depthMode == 2);
            // Mesh floorLevel 2 → sortKeyY = 9 + 1.0(2레벨) = 10.0.
            MYE_EXPECT(ApproxEqual(it.sortKeyY, 10.0f, 1e-3f));
            MYE_EXPECT(it.sortLayer == scene::kSortLayerWorldBase + 2);
        }
    }
    MYE_EXPECT(sprites == 1 && meshes == 1);
}
