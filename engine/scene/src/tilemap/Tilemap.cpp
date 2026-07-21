// mye/tilemap/Tilemap.cpp — 타일맵 데이터·높이·경사·다리 구현 (docs/03 §5)
//
// 셀당 다중 컬럼 저장/조회, heightLevel↔월드Y 환산, 경사 높이 보간을 제공한다.
// 정렬·깊이 함수는 02(M2-C) 몫 — 여기서는 groundY 등 "데이터"만 산출한다.
#include "mye/tilemap/Tilemap.h"

#include <algorithm>

namespace mye::tilemap {

// ===========================================================================
// TileChunk
// ===========================================================================
std::span<const TileColumn> TileChunk::ColumnsAtLocal(int lx, int ly) const {
    m_scratch.clear();
    const int i = Idx(lx, ly);
    if (m_hasBase[i]) m_scratch.push_back(m_base[i]);
    auto it = m_overflow.find(i);
    if (it != m_overflow.end())
        for (const TileColumn& c : it->second) m_scratch.push_back(c);
    return std::span<const TileColumn>(m_scratch.data(), m_scratch.size());
}

void TileChunk::SetBaseColumn(int lx, int ly, const TileColumn& col) {
    const int i = Idx(lx, ly);
    if (!m_hasBase[i]) ++m_columnCount;
    m_base[i] = col;
    m_hasBase[i] = 1;
    m_dirty = true;
}

void TileChunk::AddColumn(int lx, int ly, const TileColumn& col) {
    const int i = Idx(lx, ly);
    if (!m_hasBase[i]) {
        m_base[i] = col;
        m_hasBase[i] = 1;
        ++m_columnCount;
    } else {
        m_overflow[i].push_back(col);
        ++m_columnCount;
    }
    m_dirty = true;
}

void TileChunk::ClearCell(int lx, int ly) {
    const int i = Idx(lx, ly);
    if (m_hasBase[i]) { m_hasBase[i] = 0; --m_columnCount; }
    auto it = m_overflow.find(i);
    if (it != m_overflow.end()) {
        m_columnCount -= it->second.size();
        m_overflow.erase(it);
    }
    m_dirty = true;
}

// ===========================================================================
// TileLayer
// ===========================================================================
TileChunk* TileLayer::FindChunk(Vec2i chunkCoord) {
    auto it = m_chunks.find(Key(chunkCoord));
    return it == m_chunks.end() ? nullptr : it->second.get();
}

const TileChunk* TileLayer::FindChunk(Vec2i chunkCoord) const {
    auto it = m_chunks.find(Key(chunkCoord));
    return it == m_chunks.end() ? nullptr : it->second.get();
}

TileChunk& TileLayer::GetOrCreateChunk(Vec2i chunkCoord) {
    const uint64_t k = Key(chunkCoord);
    auto it = m_chunks.find(k);
    if (it != m_chunks.end()) return *it->second;
    auto chunk = std::make_unique<TileChunk>();
    chunk->SetCoord(chunkCoord);
    TileChunk& ref = *chunk;
    m_chunks.emplace(k, std::move(chunk));
    return ref;
}

void TileLayer::SetTile(Vec2i cell, const TileColumn& col) {
    const Vec2i cc = CellToChunk(cell);
    const Vec2i lc = CellToLocal(cell);
    TileColumn c = col;
    c.layer = m_index;
    GetOrCreateChunk(cc).SetBaseColumn(lc.x, lc.y, c);
}

void TileLayer::AddBridge(Vec2i cell, const TileColumn& col) {
    const Vec2i cc = CellToChunk(cell);
    const Vec2i lc = CellToLocal(cell);
    TileColumn c = col;
    c.layer = m_index;
    c.flags |= ColumnFlags::Bridge;
    GetOrCreateChunk(cc).AddColumn(lc.x, lc.y, c);
}

std::span<const TileColumn> TileLayer::ColumnsAt(Vec2i cell) const {
    const TileChunk* chunk = FindChunk(CellToChunk(cell));
    if (!chunk) return {};
    const Vec2i lc = CellToLocal(cell);
    return chunk->ColumnsAtLocal(lc.x, lc.y);
}

// ===========================================================================
// TilemapWorld
// ===========================================================================
TileLayer& TilemapWorld::GetOrCreateLayer(uint8_t index) {
    if (index >= m_layers.size()) m_layers.resize(index + 1);
    if (!m_layers[index]) m_layers[index] = std::make_unique<TileLayer>(index);
    return *m_layers[index];
}

TileLayer* TilemapWorld::Layer(uint8_t index) {
    return index < m_layers.size() ? m_layers[index].get() : nullptr;
}

const TileLayer* TilemapWorld::Layer(uint8_t index) const {
    return index < m_layers.size() ? m_layers[index].get() : nullptr;
}

std::span<const TileColumn> TilemapWorld::ColumnsAt(Vec2i cell) const {
    m_scratch.clear();
    for (const auto& lp : m_layers) {
        if (!lp) continue;
        for (const TileColumn& c : lp->ColumnsAt(cell)) m_scratch.push_back(c);
    }
    return std::span<const TileColumn>(m_scratch.data(), m_scratch.size());
}

std::optional<TileColumn> TilemapWorld::ColumnAtLevel(Vec2i cell, int level) const {
    for (const auto& lp : m_layers) {
        if (!lp) continue;
        for (const TileColumn& c : lp->ColumnsAt(cell))
            if (c.baseHeightLevel == level) return c;   // 값 반환(앨리어싱 없음)
    }
    return std::nullopt;
}

float TilemapWorld::SampleHeight(Vec2 worldXY, int8_t level) const {
    // 셀 좌표: 1 unit = 1 타일(PPU 48). worldXY는 world unit(타일 단위).
    const int cx = static_cast<int>(std::floor(worldXY.x));
    const int cy = static_cast<int>(std::floor(worldXY.y));
    const float fx = worldXY.x - static_cast<float>(cx);   // 0..1 셀 내 위치
    const float fy = worldXY.y - static_cast<float>(cy);

    const std::optional<TileColumn> col = ColumnAtLevel(Vec2i{cx, cy}, level);
    if (!col) {
        // 해당 레벨 컬럼이 없으면 정수 레벨 높이만 반환(평지 가정).
        return HeightLevelToWorldY(level);
    }

    const float baseY = HeightLevelToWorldY(col->baseHeightLevel);
    const int rise = SlopeRiseLevels(col->slope);
    if (rise == 0) return baseY;   // 평지

    const float riseY = HeightLevelToWorldY(rise);
    // 경사 방향에 따라 셀 내 보간 파라미터 t(0=낮은 변, 1=높은 변)를 정한다.
    // GentleN: 북(+Y)으로 상승 → t = fy. GentleS: 남(-Y)으로 상승 → t = 1-fy. 이하 동일.
    float t = 0.0f;
    switch (col->slope) {
        case SlopeType::GentleN: case SlopeType::SteepN: case SlopeType::StairsN: t = fy; break;
        case SlopeType::GentleS: case SlopeType::SteepS: case SlopeType::StairsS: t = 1.0f - fy; break;
        case SlopeType::GentleE: case SlopeType::SteepE: case SlopeType::StairsE: t = fx; break;
        case SlopeType::GentleW: case SlopeType::SteepW: case SlopeType::StairsW: t = 1.0f - fx; break;
        default: t = 0.0f; break;
    }
    t = Clamp(t, 0.0f, 1.0f);
    return baseY + riseY * t;
}

} // namespace mye::tilemap
