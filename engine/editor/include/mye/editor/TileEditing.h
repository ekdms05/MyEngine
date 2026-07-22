// mye/editor/TileEditing.h — 타일맵 에디터 편집 모델·오토타일 룰·스트로크 커맨드 (docs/07 §타일맵)
//
// 07 §타일맵 편집: 씬 뷰포트 인플레이스 편집. 브러시/사각형/채우기/지우개/스포이드 도구,
//   Paint/Height/Slope/Collision 모드, 오토타일 브러시, 레이어 관리, 다리(다중 컬럼) 편집.
//   모든 스트로크는 마우스 업 시점 단일 커맨드(TilePaintCommand)로 커밋 — Undo 1회=스트로크 1회.
//
// 데이터·편집 API는 03 Tilemap(TilemapWorld)이 소유한다. 이 헤더는 "에디터가 어떻게 편집하나"
//   (도구/모드/오토타일 룰 평가/커맨드)만 소유하며, 실제 셀 쓰기는 tilemap::TilemapWorld API로만 한다.
//
// [notes/07] TilesetAsset(04 관리, 오토타일 룰의 정본 저장소)은 아직 미존재다. docs/03 §137이
//   "룰 평가는 에디터·임포트가 수행, 룰은 TilesetAsset에 저장"이라 규정하므로, 04가 TilesetAsset을
//   추가하면 아래 AutotileRuleSet은 그 에셋 필드로 이관하고 여기서는 참조만 하도록 바꾼다. 지금은
//   에디터 자체 보유 값으로 두어(직렬화·평가 로직 포함) 브러시가 동작·테스트 가능하게 한다.
#pragma once

#include "mye/editor/Command.h"
#include "mye/tilemap/Tilemap.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mye::editor {

// ---------------------------------------------------------------------------
// 편집 도구·모드 (07 §타일맵 툴바)
// ---------------------------------------------------------------------------
enum class TileTool : std::uint8_t {
    Brush,      // B: 단일/스탬프 브러시
    Rect,       // R: 사각형 채우기
    Fill,       // F: 연결 영역 채우기(flood)
    Eraser,     // E: 지우개(셀 비우기)
    Eyedropper, // I: 스포이드(맵에서 타일 집기)
    Autotile,   // 오토타일 브러시(주변 8방 이웃으로 변형 자동 선택)
};

enum class TileEditMode : std::uint8_t {
    Paint,      // 타일 id 페인팅
    Height,     // heightLevel 증감
    Slope,      // SlopeType 지정
    Collision,  // 충돌 플래그(Solid/Trigger/OneWayBridge) 토글
};

// ---------------------------------------------------------------------------
// 오토타일 룰 — 47타일 블롭/Wang 비트마스크 (docs/03 §137). 룰 평가는 에디터·임포트가 수행.
// ---------------------------------------------------------------------------
// 8방향 이웃 비트마스크(같은 터레인이면 1). 비트 순서(시계, N부터):
//   bit0=N, bit1=NE, bit2=E, bit3=SE, bit4=S, bit5=SW, bit6=W, bit7=NW.
namespace NeighborBit {
inline constexpr uint8_t N  = 1u << 0;
inline constexpr uint8_t NE = 1u << 1;
inline constexpr uint8_t E  = 1u << 2;
inline constexpr uint8_t SE = 1u << 3;
inline constexpr uint8_t S  = 1u << 4;
inline constexpr uint8_t SW = 1u << 5;
inline constexpr uint8_t W  = 1u << 6;
inline constexpr uint8_t NW = 1u << 7;
}

// 하나의 룰: (마스크 & careMask) == (matchMask & careMask) 이면 tile 사용.
//   careMask=0xFF면 정확 일치. 부분 마스크(대각 무시 등)는 careMask로 표현.
struct AutotileRule {
    uint8_t  careMask  = 0xFF;   // 어떤 이웃 비트를 검사하는가
    uint8_t  matchMask = 0;      // 검사 비트가 이 값과 같아야 함
    tilemap::TileId tile = tilemap::kNoTile;   // 매치 시 배치할 타일 id
    int      priority  = 0;      // 높은 priority 우선(동점은 careMask 비트 수 많은 쪽)
};

// 하나의 터레인 룰셋(예: "흙길"). terrainTile = 룰 미매치 시 폴백(가장 안쪽 타일).
struct AutotileRuleSet {
    std::string          name;
    tilemap::TileId      terrainTile = tilemap::kNoTile;   // 이 터레인의 "코어" 타일(이웃 판정 기준)
    std::vector<AutotileRule> rules;

    // isSame(dx,dy)=인접 셀이 같은 터레인인가(오토타일 브러시가 넘겨줌)로 8방 마스크를 만든다.
    template <typename SameFn>
    static uint8_t BuildMask(SameFn&& isSame) {
        uint8_t m = 0;
        if (isSame( 0, -1)) m |= NeighborBit::N;    // 위(cellY-1 = 북, +Y 업)
        if (isSame( 1, -1)) m |= NeighborBit::NE;
        if (isSame( 1,  0)) m |= NeighborBit::E;
        if (isSame( 1,  1)) m |= NeighborBit::SE;
        if (isSame( 0,  1)) m |= NeighborBit::S;
        if (isSame(-1,  1)) m |= NeighborBit::SW;
        if (isSame(-1,  0)) m |= NeighborBit::W;
        if (isSame(-1, -1)) m |= NeighborBit::NW;
        return m;
    }

    // 8방 마스크로 룰을 평가해 배치할 타일 id를 반환. 매치 없으면 terrainTile.
    tilemap::TileId Evaluate(uint8_t mask) const {
        const AutotileRule* best = nullptr;
        int bestBits = -1;
        for (const AutotileRule& r : rules) {
            if ((mask & r.careMask) != (r.matchMask & r.careMask)) continue;
            const int bits = PopCount(r.careMask);
            if (!best || r.priority > best->priority ||
                (r.priority == best->priority && bits > bestBits)) {
                best = &r;
                bestBits = bits;
            }
        }
        return best ? best->tile : terrainTile;
    }

    static int PopCount(uint8_t v) {
        int c = 0;
        while (v) { c += v & 1; v >>= 1; }
        return c;
    }
};

// ---------------------------------------------------------------------------
// TilePaintCommand — 스트로크 단위 Undo (docs/07 §4: 마우스 다운~업 = 1 TilePaintCommand)
// ---------------------------------------------------------------------------
// 변경된 셀 각각의 (레이어, 셀, before 컬럼들, after 컬럼들)을 담는다. Execute=after 적용,
//   Undo=before 복원. 여러 셀을 1회 Undo로 되돌린다. 데이터 쓰기는 TilemapWorld API로만.
//
// 편집 대상 TilemapWorld는 커맨드 생성 시 비소유 포인터로 캡처한다(에디터 World의 TilemapRenderer
//   런타임 인스턴스). 셀 컬럼 목록은 값으로 스냅샷해 앨리어싱을 피한다.
struct TileCellEdit {
    uint8_t                          layer = 0;
    Vec2i                            cell{0, 0};
    std::vector<tilemap::TileColumn> before;   // 편집 전 컬럼들(비어있으면 셀 없음)
    std::vector<tilemap::TileColumn> after;    // 편집 후 컬럼들(비어있으면 삭제)
};

class TilePaintCommand final : public IEditorCommand {
public:
    TilePaintCommand(tilemap::TilemapWorld* map, std::vector<TileCellEdit> edits,
                     std::string label = "타일 페인트");

    void Execute(EditorContext& ctx) override;
    void Undo(EditorContext& ctx) override;
    std::string_view Label() const override { return m_label; }
    CommandKind Kind() const override { return CommandKind::Custom; }

    std::size_t EditCount() const { return m_edits.size(); }

private:
    void ApplyCell(const TileCellEdit& e, bool useAfter);

    tilemap::TilemapWorld*     m_map = nullptr;
    std::vector<TileCellEdit>  m_edits;
    std::string                m_label;
};

// 셀의 현재 컬럼들을 값으로 스냅샷(TilePaintCommand before/after 채우기 헬퍼).
std::vector<tilemap::TileColumn> SnapshotCell(const tilemap::TilemapWorld& map,
                                              uint8_t layer, Vec2i cell);

// 한 레이어의 셀에 컬럼 목록을 그대로 반영(기존 컬럼 대체). cols 비면 셀 삭제.
//   에디터·커맨드 공용 저수준 라이터(TilemapWorld API 경유).
void WriteCell(tilemap::TilemapWorld& map, uint8_t layer, Vec2i cell,
               const std::vector<tilemap::TileColumn>& cols);

} // namespace mye::editor
