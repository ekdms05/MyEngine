// TileAnimEditorTests.cpp — M5-B 콘텐츠 제작 도구 헤드리스 테스트 (docs/07)
//
// 검증:
//   - TilePaintCommand 스트로크 Undo(여러 셀을 1회 Undo로 원복, 페인트→Undo→Redo 왕복)
//   - 오토타일 룰 평가(이웃 마스크 → 변형 타일)
//   - 높이/경사 편집이 Tilemap 데이터에 반영
//   - 애니 클립 프레임·이벤트 편집 왕복(AnimClipEditCommand)
//   - 8방향 세트 구성(DirectionalAnimSet)
#include "TestFramework.h"

#include "mye/editor/TileEditing.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/EditorContext.h"

#include "mye/tilemap/Tilemap.h"

using namespace mye;
namespace ed = mye::editor;
namespace tmap = mye::tilemap;

// 셀의 base 컬럼 타일 id 조회(없으면 kNoTile).
static tmap::TileId TileAt(const tmap::TilemapWorld& map, uint8_t layer, Vec2i cell) {
    const tmap::TileLayer* l = map.Layer(layer);
    if (!l) return tmap::kNoTile;
    auto cols = l->ColumnsAt(cell);
    return cols.empty() ? tmap::kNoTile : cols[0].tile;
}

// ---------------------------------------------------------------------------
// WriteCell / SnapshotCell 라운드트립
// ---------------------------------------------------------------------------
MYE_TEST(TileWriteAndSnapshotRoundtrip) {
    tmap::TilemapWorld map;
    std::vector<tmap::TileColumn> cols;
    tmap::TileColumn c;
    c.tile = 7;
    c.baseHeightLevel = 3;
    cols.push_back(c);
    ed::WriteCell(map, 0, Vec2i{5, 5}, cols);

    MYE_EXPECT(TileAt(map, 0, Vec2i{5, 5}) == 7u);
    auto snap = ed::SnapshotCell(map, 0, Vec2i{5, 5});
    MYE_EXPECT(snap.size() == 1);
    MYE_EXPECT(snap[0].tile == 7u);
    MYE_EXPECT(snap[0].baseHeightLevel == 3);

    // 빈 목록으로 쓰면 셀 삭제.
    ed::WriteCell(map, 0, Vec2i{5, 5}, {});
    MYE_EXPECT(TileAt(map, 0, Vec2i{5, 5}) == tmap::kNoTile);
}

// ---------------------------------------------------------------------------
// TilePaintCommand 스트로크 Undo — 여러 셀 1회 Undo 원복
// ---------------------------------------------------------------------------
MYE_TEST(TilePaintCommandStrokeUndo) {
    tmap::TilemapWorld map;
    ed::EditorContext ctx;   // 커맨드는 map 포인터로 직접 편집(ctx 미사용).

    // 3셀 스트로크: (0,0),(1,0),(2,0)에 타일 4 칠하기.
    std::vector<ed::TileCellEdit> edits;
    for (int x = 0; x < 3; ++x) {
        ed::TileCellEdit e;
        e.layer = 0;
        e.cell = Vec2i{x, 0};
        e.before = ed::SnapshotCell(map, 0, e.cell);   // 비어있음
        tmap::TileColumn col; col.tile = 4;
        e.after.push_back(col);
        edits.push_back(std::move(e));
    }
    ed::TilePaintCommand cmd(&map, std::move(edits));
    MYE_EXPECT(cmd.EditCount() == 3);

    cmd.Execute(ctx);
    MYE_EXPECT(TileAt(map, 0, Vec2i{0, 0}) == 4u);
    MYE_EXPECT(TileAt(map, 0, Vec2i{1, 0}) == 4u);
    MYE_EXPECT(TileAt(map, 0, Vec2i{2, 0}) == 4u);

    cmd.Undo(ctx);   // 1회 Undo로 3셀 모두 원복.
    MYE_EXPECT(TileAt(map, 0, Vec2i{0, 0}) == tmap::kNoTile);
    MYE_EXPECT(TileAt(map, 0, Vec2i{1, 0}) == tmap::kNoTile);
    MYE_EXPECT(TileAt(map, 0, Vec2i{2, 0}) == tmap::kNoTile);

    cmd.Execute(ctx);   // Redo.
    MYE_EXPECT(TileAt(map, 0, Vec2i{1, 0}) == 4u);
}

// 기존 타일을 덮어쓴 스트로크의 Undo가 이전 타일을 복원.
MYE_TEST(TilePaintCommandRestoresPrevious) {
    tmap::TilemapWorld map;
    ed::EditorContext ctx;
    { std::vector<tmap::TileColumn> c(1); c[0].tile = 9; ed::WriteCell(map, 0, Vec2i{0, 0}, c); }

    ed::TileCellEdit e;
    e.layer = 0; e.cell = Vec2i{0, 0};
    e.before = ed::SnapshotCell(map, 0, e.cell);
    tmap::TileColumn nc; nc.tile = 2; e.after.push_back(nc);
    ed::TilePaintCommand cmd(&map, {std::move(e)});
    cmd.Execute(ctx);
    MYE_EXPECT(TileAt(map, 0, Vec2i{0, 0}) == 2u);
    cmd.Undo(ctx);
    MYE_EXPECT(TileAt(map, 0, Vec2i{0, 0}) == 9u);   // 이전 타일 복원
}

// ---------------------------------------------------------------------------
// 오토타일 룰 평가
// ---------------------------------------------------------------------------
MYE_TEST(AutotileRuleEvaluation) {
    ed::AutotileRuleSet rs;
    rs.terrainTile = 100;   // 폴백(사방이 같은 터레인=안쪽 타일)

    // 룰: 위(N)가 비었으면(matchMask N=0, careMask=N만) 상단 가장자리 타일 101.
    ed::AutotileRule top;
    top.careMask = ed::NeighborBit::N;
    top.matchMask = 0;   // N 비트가 0이어야
    top.tile = 101;
    top.priority = 1;
    rs.rules.push_back(top);

    // 룰: 왼쪽(W)이 비었으면 좌측 가장자리 타일 102.
    ed::AutotileRule left;
    left.careMask = ed::NeighborBit::W;
    left.matchMask = 0;
    left.tile = 102;
    left.priority = 1;
    rs.rules.push_back(left);

    // 마스크 = 사방 모두 같음(0xFF) → 폴백 terrainTile.
    MYE_EXPECT(rs.Evaluate(0xFF) == 100u);

    // 위가 빈 마스크(N 비트 0, 나머지 1) → top 룰(101).
    uint8_t maskNoN = static_cast<uint8_t>(0xFF & ~ed::NeighborBit::N);
    MYE_EXPECT(rs.Evaluate(maskNoN) == 101u);

    // 왼쪽만 빈 마스크 → left 룰(102).
    uint8_t maskNoW = static_cast<uint8_t>(0xFF & ~ed::NeighborBit::W);
    MYE_EXPECT(rs.Evaluate(maskNoW) == 102u);

    // BuildMask: N 이웃만 같고 나머지 다름.
    uint8_t m = ed::AutotileRuleSet::BuildMask([](int dx, int dy) {
        return dx == 0 && dy == -1;   // 북(위)만 같음
    });
    MYE_EXPECT((m & ed::NeighborBit::N) != 0);
    MYE_EXPECT((m & ed::NeighborBit::S) == 0);
}

// ---------------------------------------------------------------------------
// 높이/경사 편집이 Tilemap 데이터에 반영(WriteCell 경유 컬럼 필드)
// ---------------------------------------------------------------------------
MYE_TEST(TileHeightSlopeEdit) {
    tmap::TilemapWorld map;
    { std::vector<tmap::TileColumn> c(1); c[0].tile = 1; ed::WriteCell(map, 0, Vec2i{2, 2}, c); }

    // 높이 +2.
    auto snap = ed::SnapshotCell(map, 0, Vec2i{2, 2});
    snap[0].baseHeightLevel += 2;
    ed::WriteCell(map, 0, Vec2i{2, 2}, snap);
    {
        auto s2 = ed::SnapshotCell(map, 0, Vec2i{2, 2});
        MYE_EXPECT(s2[0].baseHeightLevel == 2);
    }

    // 경사 GentleN.
    snap = ed::SnapshotCell(map, 0, Vec2i{2, 2});
    snap[0].slope = tmap::SlopeType::GentleN;
    ed::WriteCell(map, 0, Vec2i{2, 2}, snap);
    {
        auto s3 = ed::SnapshotCell(map, 0, Vec2i{2, 2});
        MYE_EXPECT(s3[0].slope == tmap::SlopeType::GentleN);
    }
}

// 다리(다중 컬럼) 편집 — 지면 + 다리 컬럼 공존.
MYE_TEST(TileBridgeMultiColumn) {
    tmap::TilemapWorld map;
    std::vector<tmap::TileColumn> cols;
    tmap::TileColumn ground; ground.tile = 1; ground.baseHeightLevel = 0;
    tmap::TileColumn bridge; bridge.tile = 2; bridge.baseHeightLevel = 4;
    bridge.flags = tmap::ColumnFlags::Bridge;
    cols.push_back(ground);
    cols.push_back(bridge);
    ed::WriteCell(map, 0, Vec2i{3, 3}, cols);

    auto snap = ed::SnapshotCell(map, 0, Vec2i{3, 3});
    MYE_EXPECT(snap.size() == 2);
    MYE_EXPECT(snap[0].tile == 1u);
    MYE_EXPECT(snap[1].tile == 2u);
    MYE_EXPECT((snap[1].flags & tmap::ColumnFlags::Bridge) != 0);
}

// ---------------------------------------------------------------------------
// 애니 클립 프레임·이벤트 편집 왕복 (AnimClipEditCommand + anim_edit 헬퍼)
// ---------------------------------------------------------------------------
MYE_TEST(AnimClipFrameEditRoundtrip) {
    asset::AnimationClipData clip;
    clip.name = "walk";

    // 프레임 3개 추가.
    clip = ed::anim_edit::WithFrameAppended(clip, 0, 0.1f);
    clip = ed::anim_edit::WithFrameAppended(clip, 1, 0.1f);
    clip = ed::anim_edit::WithFrameAppended(clip, 2, 0.2f);
    MYE_EXPECT(clip.frameIndices.size() == 3);
    MYE_EXPECT(clip.frameDurations.size() == 3);
    MYE_EXPECT(clip.frameIndices[2] == 2u);

    // duration 변경.
    clip = ed::anim_edit::WithFrameDuration(clip, 1, 0.5f);
    MYE_EXPECT(clip.frameDurations[1] == 0.5f);

    // 순서 이동(0 → 2).
    asset::AnimationClipData moved = ed::anim_edit::WithFrameMoved(clip, 0, 2);
    MYE_EXPECT(moved.frameIndices[2] == 0u);
    MYE_EXPECT(moved.frameIndices.size() == 3);

    // 프레임 제거.
    asset::AnimationClipData removed = ed::anim_edit::WithFrameRemoved(clip, 1);
    MYE_EXPECT(removed.frameIndices.size() == 2);
    MYE_EXPECT(removed.frameIndices[1] == 2u);
}

MYE_TEST(AnimClipEditCommandUndo) {
    asset::AnimationClipData clip;
    clip.name = "idle";
    ed::EditorContext ctx;

    asset::AnimationClipData after = ed::anim_edit::WithFrameAppended(clip, 5, 0.25f);
    ed::AnimClipEditCommand cmd(&clip, clip, after, "프레임 추가");

    cmd.Execute(ctx);
    MYE_EXPECT(clip.frameIndices.size() == 1);
    MYE_EXPECT(clip.frameIndices[0] == 5u);

    cmd.Undo(ctx);
    MYE_EXPECT(clip.frameIndices.empty());

    cmd.Execute(ctx);   // Redo
    MYE_EXPECT(clip.frameIndices.size() == 1);
}

MYE_TEST(AnimEventEditRoundtrip) {
    asset::AnimationClipData clip;
    clip = ed::anim_edit::WithFrameAppended(clip, 0, 0.1f);
    clip = ed::anim_edit::WithFrameAppended(clip, 1, 0.1f);
    clip = ed::anim_edit::WithFrameAppended(clip, 2, 0.1f);

    asset::AnimEventMarker ev;
    ev.frameIndex = 2;
    ev.name = "footstep";
    clip = ed::anim_edit::WithEventAdded(clip, ev);
    asset::AnimEventMarker ev0;
    ev0.frameIndex = 0;
    ev0.name = "start";
    clip = ed::anim_edit::WithEventAdded(clip, ev0);

    MYE_EXPECT(clip.events.size() == 2);
    // frameIndex 오름차순 정렬 확인.
    MYE_EXPECT(clip.events[0].frameIndex == 0u);
    MYE_EXPECT(clip.events[0].name == "start");
    MYE_EXPECT(clip.events[1].frameIndex == 2u);
    MYE_EXPECT(clip.events[1].name == "footstep");

    // 프레임 1 제거 시 프레임 2를 가리키던 이벤트가 1로 당겨짐.
    asset::AnimationClipData r = ed::anim_edit::WithFrameRemoved(clip, 1);
    bool footAt1 = false;
    for (auto& e : r.events) if (e.name == "footstep") footAt1 = (e.frameIndex == 1);
    MYE_EXPECT(footAt1);

    // 이벤트 제거.
    clip = ed::anim_edit::WithEventRemoved(clip, 0);
    MYE_EXPECT(clip.events.size() == 1);
    MYE_EXPECT(clip.events[0].name == "footstep");
}

// ---------------------------------------------------------------------------
// 8방향 세트 구성
// ---------------------------------------------------------------------------
MYE_TEST(DirectionalSetBuild) {
    std::vector<asset::AnimationClipData> clips;
    for (int d = 0; d < 8; ++d) {
        asset::AnimationClipData c;
        c.name = std::string("walk_") + anim::Dir8Suffix(static_cast<anim::Dir8>(d));
        c.frameIndices.push_back(static_cast<uint32_t>(d));
        clips.push_back(std::move(c));
    }

    anim::DirectionalAnimSet set = ed::BuildDirectionalSet("walk", clips, /*mirrorRight*/ false);
    MYE_EXPECT(set.name == "walk");
    for (int d = 0; d < 8; ++d) {
        MYE_EXPECT(set.clips[static_cast<size_t>(d)] != nullptr);
        MYE_EXPECT(set.clips[static_cast<size_t>(d)]->frameIndices[0] == static_cast<uint32_t>(d));
    }
    // Down 슬롯이 walk_down에 매핑되는지.
    auto resolved = set.Resolve(anim::Dir8::Down);
    MYE_EXPECT(resolved.clip != nullptr);
    MYE_EXPECT(resolved.clip->name == "walk_down");

    // mirrorRight: 우측 3방향은 좌측 클립 + flipX.
    anim::DirectionalAnimSet mset = ed::BuildDirectionalSet("walk", clips, /*mirrorRight*/ true);
    auto right = mset.Resolve(anim::Dir8::Right);
    MYE_EXPECT(right.clip != nullptr);
    MYE_EXPECT(right.flipX == true);
    MYE_EXPECT(right.clip->name == "walk_left");
}
