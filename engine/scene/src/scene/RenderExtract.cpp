// mye/scene/RenderExtract.cpp — 렌더 추출(03 → 02) 구현 (docs/03 §4, docs/02 §렌더 프록시)
//
// World를 순회해 가시 렌더러블 → RenderItem 목록을 만든다. 정렬·밴드 배분·깊이 인코딩은
// 하지 않는다(02/M2-C). sortKeyY는 논리 지면 Y(Transform.y + FloorLevel 높이 반영, 점프 제외).
#include "mye/scene/RenderExtract.h"

#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/scene/Transform.h"
#include "mye/tilemap/Tilemap.h"

namespace mye::scene {

namespace {

// WorldTransform(row-vector, translation = m[3][*])에서 월드 위치를 뽑는다.
Vec3 WorldPosition(const WorldTransform* wt) {
    if (!wt) return Vec3{0, 0, 0};
    return Vec3{wt->matrix.m[3][0], wt->matrix.m[3][1], wt->matrix.m[3][2]};
}

// 논리 지면 Y = Transform.y + FloorLevel의 높이 오프셋(층이 높을수록 화면상 뒤 = sortKeyY 큼).
// 점프·엘리베이션의 *시각* 오프셋은 반영하지 않는다(02 §깊이함수) — Transform.y가 이미 지면.
float ComputeSortKeyY(const Vec3& worldPos, const FloorLevel* floor) {
    float y = worldPos.y;
    if (floor)
        y += tilemap::HeightLevelToWorldY(floor->level);
    return y;
}

// sortLayer 결정: FloorLevel이 있으면 World k 밴드로 변환, 없으면 컴포넌트 자체 sortLayer 사용.
uint16_t ResolveSortLayer(const SortingRef& sort, const FloorLevel* floor) {
    if (floor) return FloorLevelToSortLayer(floor->level);
    return sort.sortLayer;
}

int8_t FloorValue(const FloorLevel* floor) { return floor ? floor->level : int8_t{0}; }

} // namespace

void ExtractRenderItems(ecs::World& world, RenderProxyList& out) {
    out.Clear();

    // ---- SpriteRenderer ----
    world.Query<SpriteRenderer>().Each([&](ecs::Entity e, SpriteRenderer& sr) {
        if (!sr.visible) return;
        auto* wt = world.TryGet<WorldTransform>(e);
        auto* floor = world.TryGet<FloorLevel>(e);
        const Vec3 pos = WorldPosition(wt);

        RenderItem& it = out.Push();
        it.kind = RenderItemKind::Sprite;
        it.texture = sr.sprite.guid;
        it.srcUV = sr.srcUV;
        it.pivotPx = sr.pivotPx;
        it.tint = sr.tint;
        it.flashColor = sr.flashColor;
        it.flashAmount = sr.flashAmount;
        it.flipX = sr.flipX;
        it.flipY = sr.flipY;
        it.worldTransform = wt ? wt->matrix : Mat4::Translation(pos);
        it.sortLayer = ResolveSortLayer(sr.sort, floor);
        it.orderInLayer = sr.sort.orderInLayer;
        it.sortKeyY = ComputeSortKeyY(pos, floor);
        it.floorLevel = FloorValue(floor);
    });

    // ---- BillboardRenderer ----
    world.Query<BillboardRenderer>().Each([&](ecs::Entity e, BillboardRenderer& br) {
        if (!br.visible) return;
        auto* wt = world.TryGet<WorldTransform>(e);
        auto* floor = world.TryGet<FloorLevel>(e);
        const Vec3 pos = WorldPosition(wt);

        RenderItem& it = out.Push();
        it.kind = RenderItemKind::Billboard;
        it.texture = br.sprite.guid;
        it.srcUV = br.srcUV;
        it.pivotPx = br.pivotPx;
        it.tint = br.tint;
        it.billboardMode = static_cast<uint8_t>(br.mode);
        it.worldTransform = wt ? wt->matrix : Mat4::Translation(pos);
        it.sortLayer = ResolveSortLayer(br.sort, floor);
        it.orderInLayer = br.sort.orderInLayer;
        it.sortKeyY = ComputeSortKeyY(pos, floor);
        it.floorLevel = FloorValue(floor);
    });

    // ---- MeshRenderer ----
    world.Query<MeshRenderer>().Each([&](ecs::Entity e, MeshRenderer& mr) {
        if (!mr.visible) return;
        auto* wt = world.TryGet<WorldTransform>(e);
        auto* floor = world.TryGet<FloorLevel>(e);
        const Vec3 pos = WorldPosition(wt);

        RenderItem& it = out.Push();
        it.kind = RenderItemKind::Mesh;
        it.mesh = mr.mesh.guid;
        it.material = mr.material.guid;
        it.depthMode = mr.depthMode;
        it.worldTransform = wt ? wt->matrix : Mat4::Translation(pos);
        it.sortLayer = ResolveSortLayer(mr.sort, floor);
        it.orderInLayer = mr.sort.orderInLayer;
        it.sortKeyY = ComputeSortKeyY(pos, floor);   // 앵커 Y = 발밑(Transform.y)
        it.floorLevel = FloorValue(floor);
    });

    // ---- TilemapRenderer: 가시 청크 → 청크 프록시(청크 단위) ----
    world.Query<TilemapRenderer>().Each([&](ecs::Entity e, TilemapRenderer& tr) {
        if (!tr.visible || !tr.runtime) return;
        auto* wt = world.TryGet<WorldTransform>(e);
        const Vec3 origin = WorldPosition(wt);

        tr.runtime->ForEachLayer([&](tilemap::TileLayer& layer) {
            layer.ForEachChunk([&](tilemap::TileChunk& chunk) {
                if (!chunk.HasAnyColumn()) return;
                RenderItem& it = out.Push();
                it.kind = RenderItemKind::TilemapChunk;
                it.tilemap = tr.runtime;
                it.tileLayer = layer.Index();
                it.chunkCoord = chunk.Coord();
                // 타일셋 아틀라스 텍스처 GUID(02가 청크 쿼드를 이 텍스처로 그린다).
                // M2-B: TilemapRenderer.map을 타일셋 텍스처 참조로 사용(단일 아틀라스).
                it.texture = tr.map.guid;
                it.worldTransform = wt ? wt->matrix : Mat4::Translation(origin);
                it.sortLayer = tr.sort.sortLayer;      // 지면 밴드 등(02 밴드 표)
                it.orderInLayer = tr.sort.orderInLayer;
                // 청크 대표 sortKeyY. Y 규약 정본(worldY = -cellY, tilemap::CellToWorldY)을 경유해
                // 렌더러 wyBase와 부호를 통일한다. 단, 02(HybridRenderer::BuildTileChunkQuads)는
                // 이 값을 소비하지 않고 셀별 sortKeyBottom/Top을 재계산하므로(램프 깊이) 이 필드는
                // 프록시 프로파일링·컬링용 대표값이다. 청크 첫 행(coord.y*32) 북단 월드 Y를 쓴다.
                it.sortKeyY = origin.y +
                    tilemap::CellToWorldY(chunk.Coord().y * tilemap::kChunkSize);
                it.floorLevel = 0;
            });
        });
    });
}

// ---------------------------------------------------------------------------
// SystemScheduler 배선 헬퍼
// ---------------------------------------------------------------------------
void RegisterRenderExtractSystem(SystemScheduler& scheduler, RenderProxyList& target) {
    SystemDesc desc;
    desc.name = "RenderExtractSystem";
    desc.phase = Phase::RenderExtract;
    desc.reads = {
        SpriteRenderer::kComponentTypeId, BillboardRenderer::kComponentTypeId,
        MeshRenderer::kComponentTypeId,   TilemapRenderer::kComponentTypeId,
        WorldTransform::kComponentTypeId, FloorLevel::kComponentTypeId,
    };
    desc.fn = [&target](ecs::World& w, ecs::CommandBuffer&, const TimeStep&) {
        ExtractRenderItems(w, target);
    };
    scheduler.RegisterSystem(std::move(desc));
}

} // namespace mye::scene
