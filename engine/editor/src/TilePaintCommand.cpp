// TilePaintCommand.cpp — 타일맵 스트로크 커맨드 + 저수준 셀 라이터 (docs/07 §4)
//
// 편집은 TilemapWorld API(레이어→청크)만 경유한다. before/after 컬럼 목록으로 스트로크 전체를
//   1회 Undo로 되돌린다. 셀 쓰기는 청크 ClearCell + SetBaseColumn/AddColumn 조합으로 재구성한다.
#include "mye/editor/TileEditing.h"

namespace mye::editor {

namespace tm = mye::tilemap;

std::vector<tm::TileColumn> SnapshotCell(const tm::TilemapWorld& map,
                                         uint8_t layer, Vec2i cell) {
    std::vector<tm::TileColumn> out;
    const tm::TileLayer* lyr = map.Layer(layer);
    if (!lyr) return out;
    std::span<const tm::TileColumn> cols = lyr->ColumnsAt(cell);
    out.assign(cols.begin(), cols.end());
    return out;
}

void WriteCell(tm::TilemapWorld& map, uint8_t layer, Vec2i cell,
               const std::vector<tm::TileColumn>& cols) {
    tm::TileLayer& lyr = map.GetOrCreateLayer(layer);
    tm::TileChunk& chunk = lyr.GetOrCreateChunk(tm::CellToChunk(cell));
    const Vec2i lc = tm::CellToLocal(cell);
    chunk.ClearCell(lc.x, lc.y);
    // 첫 컬럼은 base, 나머지는 overflow(AddColumn이 base 비면 승격하므로 순서대로 Add로 충분).
    //   col.layer 를 강제로 덮어쓰지 않고 스냅샷 값을 그대로 재적용한다(before==Undo 결과 무손실
    //   왕복 보장). layer 지정 책임은 컬럼 생성 시점(tile_ops)에 둔다.
    (void)layer;
    for (const tm::TileColumn& c : cols)
        chunk.AddColumn(lc.x, lc.y, c);
}

TilePaintCommand::TilePaintCommand(tm::TilemapWorld* map,
                                   std::vector<TileCellEdit> edits,
                                   std::string label)
    : m_map(map), m_edits(std::move(edits)), m_label(std::move(label)) {}

void TilePaintCommand::ApplyCell(const TileCellEdit& e, bool useAfter) {
    if (!m_map) return;
    WriteCell(*m_map, e.layer, e.cell, useAfter ? e.after : e.before);
}

void TilePaintCommand::Execute(EditorContext&) {
    for (const TileCellEdit& e : m_edits) ApplyCell(e, /*useAfter*/ true);
}

void TilePaintCommand::Undo(EditorContext&) {
    for (const TileCellEdit& e : m_edits) ApplyCell(e, /*useAfter*/ false);
}

} // namespace mye::editor
