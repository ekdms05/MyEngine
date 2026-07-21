// mye/tilemap/Tilemap.h — 타일맵 데이터·높이·경사·다리 (docs/03 §5) [M2-B 구현]
//
// 셀당 다중 컬럼(테일즈위버식 높이 지형) 모델. 같은 셀에 지면 컬럼과 다리 컬럼이 공존한다
// (다리 통과 케이스). 높이는 정수 레벨(heightLevel)로 양자화하고, 시각 오프셋 환산은
// 02가 확정한 PPU 48 규약을 인용한다: 1레벨 = 타일 반 칸 = 24px = 0.5 world unit(+Y 업).
//
// 저장: 32×32셀 청크. 청크 단위로 (1) 저장·조회·dirty, (2) 렌더 배치 빌드(03이 청크 프록시
// 산출 — RenderExtract), (3) 충돌·내비 베이크(M3+). 이 헤더는 03의 "데이터·질의" 소유부다.
// 정렬·깊이 함수·밴드 배분은 02(M2-C) 몫 — 본 모듈은 groundY/heightLevel 데이터만 공급한다.
#pragma once

#include "mye/core/Math.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace mye::tilemap {

using TileId = uint32_t;
inline constexpr TileId kNoTile = 0;   // 0 = 빈 타일

// ---------------------------------------------------------------------------
// 좌표·환산 규약 (02 PPU 48 인용 — 본 모듈은 소유하지 않고 파라미터/상수로 인용)
// ---------------------------------------------------------------------------
inline constexpr int   kChunkSize   = 32;          // 청크 한 변 셀 수 (docs/03 §5)
inline constexpr int   kChunkCells  = kChunkSize * kChunkSize;
inline constexpr float kPixelsPerUnit = 48.0f;     // 02 확정 PPU (인용)
// 1 heightLevel = 반 칸(24px) = 0.5 world unit. worldYOffset = heightLevel * 0.5.
inline constexpr float kWorldUnitsPerHeightLevel = 0.5f;
inline constexpr float kPixelsPerHeightLevel     = 24.0f;   // 참고(픽셀 환산)

// heightLevel(정수 층) → 월드 Y 오프셋(world unit, +Y 업). 지면 기준 높이.
constexpr float HeightLevelToWorldY(int heightLevel) {
    return static_cast<float>(heightLevel) * kWorldUnitsPerHeightLevel;
}
// heightLevel → 픽셀 높이(참고용, 스냅·에디터 표시).
constexpr float HeightLevelToPixels(int heightLevel) {
    return static_cast<float>(heightLevel) * kPixelsPerHeightLevel;
}
// 월드 Y 오프셋 → 가장 가까운 정수 heightLevel(반올림).
inline int WorldYToHeightLevel(float worldY) {
    const float lv = worldY / kWorldUnitsPerHeightLevel;
    return static_cast<int>(lv < 0.0f ? lv - 0.5f : lv + 0.5f);
}

// ---------------------------------------------------------------------------
// 셀 Y ↔ 월드 Y 단일 규약 (docs/02 §타일→월드: y = -row·tileH/PPU, row는 아래로 증가)
// ---------------------------------------------------------------------------
// 정본: 셀 Y(row)는 화면 아래로 증가하고, 월드는 +Y 업이므로 worldY = -cellY 다. 렌더러
// (BuildTileChunkQuads), SampleHeight, RenderExtract 청크 sortKeyY가 모두 이 함수를 경유한다.
// 셀 하나는 월드 Y 구간 [CellToWorldY(cy), CellToWorldY(cy)+1] = [-cy, -cy+1]을 덮는다.
constexpr float CellToWorldY(int cellY) { return -static_cast<float>(cellY); }

// 월드 Y → 셀 Y(정수). 셀 cy는 월드 Y 반개구간 [-cy, -cy+1)을 덮으므로 역변환은 ceil(-worldY).
// (worldY=0.5 → cell 0[0,1), worldY=-0.5 → cell 1[-1,0), worldY=1.5 → cell -1[1,2)).
inline int WorldYToCellY(float worldY) {
    return static_cast<int>(std::ceil(-worldY));
}

// 셀 좌표 → 청크 좌표(음수 안전 floor 나눗셈).
constexpr Vec2i CellToChunk(Vec2i cell) {
    auto fdiv = [](int a, int b) {
        int q = a / b, r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
    };
    return {fdiv(cell.x, kChunkSize), fdiv(cell.y, kChunkSize)};
}
// 셀 → 청크 내부 로컬 좌표(0..31).
constexpr Vec2i CellToLocal(Vec2i cell) {
    auto fmod = [](int a, int b) {
        int r = a % b;
        return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
    };
    return {fmod(cell.x, kChunkSize), fmod(cell.y, kChunkSize)};
}

// ---------------------------------------------------------------------------
// 경사 (SlopeType) — 경사 위 엔티티는 보간된 연속 높이를 갖는다 (docs/03 §5)
// ---------------------------------------------------------------------------
// 방향 규약: GentleN = "북(+Y)으로 올라가는" 완경사 → 셀 남단이 baseHeight, 북단이 +1레벨.
// 계단(Stairs)은 경사와 동일 보간(층 전이는 이동 시스템 몫)이되 급경사 취급.
enum class SlopeType : uint8_t {
    None = 0,
    GentleN, GentleS, GentleE, GentleW,   // 완경사: 셀 하나에 +1레벨 상승
    SteepN,  SteepS,  SteepE,  SteepW,    // 급경사: 셀 하나에 +2레벨 상승
    StairsN, StairsS, StairsE, StairsW,   // 계단: 완경사와 동일 보간, 층 전이 트리거
    Count
};

// 경사의 상승 레벨(셀 남/서단 baseHeight → 북/동단 baseHeight+rise).
constexpr int SlopeRiseLevels(SlopeType s) {
    switch (s) {
        case SlopeType::SteepN: case SlopeType::SteepS:
        case SlopeType::SteepE: case SlopeType::SteepW: return 2;
        case SlopeType::None:   case SlopeType::Count:  return 0;
        default: return 1;   // Gentle*, Stairs*
    }
}

// ---------------------------------------------------------------------------
// 타일 컬럼 (셀 하나의 "층" 하나 — 지면 또는 다리)
// ---------------------------------------------------------------------------
namespace ColumnFlags {
inline constexpr uint8_t Solid        = 1u << 0;   // 충돌 솔리드(벽·절벽)
inline constexpr uint8_t OneWayBridge = 1u << 1;   // 다리 상판(아래는 통과)
inline constexpr uint8_t EventTrigger = 1u << 2;   // 이벤트 트리거 셀
inline constexpr uint8_t Bridge       = 1u << 3;   // 다리 컬럼(지면 위 공중 층)
}

// 셀 하나의 컬럼 하나. baseHeightLevel = 이 컬럼 지면의 정수 높이 층.
// 같은 셀에 여러 컬럼이 존재하면 다리(위 컬럼)+지면(아래 컬럼)을 표현한다.
struct TileColumn {
    TileId    tile           = kNoTile;
    int32_t   baseHeightLevel = 0;      // 이 컬럼 지면 높이(정수 레벨). int(넉넉히) — 압축은 후속.
    SlopeType slope          = SlopeType::None;
    uint8_t   flags          = 0;       // ColumnFlags 조합
    uint8_t   layer          = 0;       // 렌더 레이어(지면/장식/오버헤드 등)
    uint8_t   _pad           = 0;

    bool IsSolid()  const { return (flags & ColumnFlags::Solid) != 0; }
    bool IsBridge() const { return (flags & ColumnFlags::Bridge) != 0; }
};

// M2-B 별칭: docs 스케치의 TileCell == "컬럼 하나"와 정합(같은 타입).
using TileCell = TileColumn;

// ---------------------------------------------------------------------------
// 청크 (32×32) — 단일 컬럼 SoA + 다중 컬럼 셀 오버플로 리스트
// ---------------------------------------------------------------------------
// 대부분 셀은 컬럼 1개 → base 배열에 저장. 다리 등 다중 컬럼 셀만 overflow에 추가 컬럼 보관.
class TileChunk {
public:
    TileChunk() { m_base.resize(kChunkCells); m_hasBase.assign(kChunkCells, 0); }

    Vec2i Coord() const { return m_coord; }
    void  SetCoord(Vec2i c) { m_coord = c; }
    bool  Dirty() const { return m_dirty; }
    void  ClearDirty() { m_dirty = false; }

    // 로컬 좌표(0..31)의 컬럼들(base + overflow)을 out에 채워 개수 반환.
    // 반환 span은 다음 QueryColumns/구조변경 전까지 유효(내부 스크래치 버퍼).
    std::span<const TileColumn> ColumnsAtLocal(int lx, int ly) const;

    // 기본(단일) 컬럼 설정 — dirty 마킹. layer는 컬럼에 이미 담긴 값을 사용.
    void SetBaseColumn(int lx, int ly, const TileColumn& col);
    // 다중 컬럼 셀에 컬럼 추가(다리 층 등). base가 비어 있으면 base로 승격.
    void AddColumn(int lx, int ly, const TileColumn& col);
    void ClearCell(int lx, int ly);

    bool HasAnyColumn() const { return m_columnCount > 0; }
    size_t ColumnCount() const { return m_columnCount; }

    // 청크 내 모든 컬럼 순회: fn(int lx, int ly, const TileColumn&). 렌더 배치 빌드용.
    template <typename Fn>
    void ForEachColumn(Fn&& fn) const {
        for (int ly = 0; ly < kChunkSize; ++ly)
            for (int lx = 0; lx < kChunkSize; ++lx) {
                const int i = ly * kChunkSize + lx;
                if (m_hasBase[i]) fn(lx, ly, m_base[i]);
                auto it = m_overflow.find(i);
                if (it != m_overflow.end())
                    for (const TileColumn& c : it->second) fn(lx, ly, c);
            }
    }

private:
    static int Idx(int lx, int ly) { return ly * kChunkSize + lx; }

    Vec2i m_coord{0, 0};
    bool  m_dirty = true;
    size_t m_columnCount = 0;
    std::vector<TileColumn> m_base;           // [kChunkCells] 단일 컬럼 SoA
    std::vector<uint8_t>    m_hasBase;         // base 슬롯 점유 여부
    std::unordered_map<int, std::vector<TileColumn>> m_overflow;  // 로컬 idx → 추가 컬럼들
    mutable std::vector<TileColumn> m_scratch;  // ColumnsAtLocal 반환 버퍼
};

// ---------------------------------------------------------------------------
// TilemapLayer — 한 렌더 레이어의 청크 그리드(희소, 좌표 → 청크)
// ---------------------------------------------------------------------------
class TileLayer {
public:
    explicit TileLayer(uint8_t index = 0) : m_index(index) {}

    uint8_t Index() const { return m_index; }

    TileChunk*       FindChunk(Vec2i chunkCoord);
    const TileChunk* FindChunk(Vec2i chunkCoord) const;
    TileChunk&       GetOrCreateChunk(Vec2i chunkCoord);

    // 셀 단위 편의(청크 자동 생성). 컬럼의 layer 필드는 이 레이어 index로 강제.
    void SetTile(Vec2i cell, const TileColumn& col);
    void AddBridge(Vec2i cell, const TileColumn& col);

    // 셀의 컬럼들(존재하는 청크에서만). 없으면 빈 span.
    std::span<const TileColumn> ColumnsAt(Vec2i cell) const;

    // 청크 순회: fn(TileChunk&). 렌더 추출이 가시 청크만 걸러 쓰기 위한 이터레이션.
    template <typename Fn>
    void ForEachChunk(Fn&& fn) {
        for (auto& [key, chunk] : m_chunks) fn(*chunk);
    }
    template <typename Fn>
    void ForEachChunk(Fn&& fn) const {
        for (const auto& [key, chunk] : m_chunks) fn(*chunk);
    }

    size_t ChunkCount() const { return m_chunks.size(); }

private:
    static uint64_t Key(Vec2i c) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) |
               static_cast<uint32_t>(c.y);
    }
    uint8_t m_index = 0;
    std::unordered_map<uint64_t, std::unique_ptr<TileChunk>> m_chunks;
};

// ---------------------------------------------------------------------------
// TilemapWorld — 런타임 질의 허브(렌더·충돌·내비 공유) (docs/03 §5)
// ---------------------------------------------------------------------------
class TilemapWorld {
public:
    TilemapWorld() = default;

    // 레이어 관리(0..N). 렌더 레이어 = 지면/장식/오버헤드 등.
    TileLayer&       GetOrCreateLayer(uint8_t index);
    TileLayer*       Layer(uint8_t index);
    const TileLayer* Layer(uint8_t index) const;
    size_t           LayerCount() const { return m_layers.size(); }

    template <typename Fn>
    void ForEachLayer(Fn&& fn) { for (auto& lp : m_layers) if (lp) fn(*lp); }
    template <typename Fn>
    void ForEachLayer(Fn&& fn) const { for (const auto& lp : m_layers) if (lp) fn(*lp); }

    // 모든 레이어에서 셀 (x,y)의 컬럼들을 모아 반환(내부 스크래치, 다음 호출 전까지 유효).
    // 다리 통과 케이스: 지면 컬럼 + 다리 컬럼이 함께 반환된다.
    std::span<const TileColumn> ColumnsAt(Vec2i cell) const;

    // 경사 보간 포함 지면 높이(월드 Y, world unit). level = 원하는 층(다리/지면 선택).
    // 셀 안에서 worldXY의 소수부로 경사면을 선형 보간한다. groundY 계약(02 §깊이함수).
    float SampleHeight(Vec2 worldXY, int8_t level) const;

    // 특정 셀·레벨의 컬럼을 찾는다(정확히 baseHeightLevel == level 인 컬럼). 없으면 nullopt.
    // 값 반환(스크래치 앨리어싱 회피 — 다리 위/아래 컬럼을 동시에 보관·비교 가능).
    std::optional<TileColumn> ColumnAtLevel(Vec2i cell, int level) const;

private:
    std::vector<std::unique_ptr<TileLayer>> m_layers;   // index로 접근(희소 허용)
    mutable std::vector<TileColumn>         m_scratch;
};

} // namespace mye::tilemap
