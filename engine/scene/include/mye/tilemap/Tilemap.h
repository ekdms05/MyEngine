// mye/tilemap/Tilemap.h — 타일맵 스텁 (docs/03 §5) [M2-C / M3+ 구현]
//
// M2-A: 시그니처 주석만(구현 금지). 셀당 다중 컬럼(테일즈위버식 높이 지형) 모델의 계약
// 표면을 후속 워크플로우가 채운다. 이 헤더는 03의 데이터 스키마 소유 위치를 고정한다.
#pragma once

#include "mye/core/Math.h"

#include <cstdint>

namespace mye::tilemap {

// enum class SlopeType : uint8_t { None, GentleN, GentleS, GentleE, GentleW,
//                                  SteepN, /*...*/ StairsN, /*...*/ };
//
// using TileId = uint32_t;
//
// struct TileCell {              // 셀 하나의 컬럼 하나(지면 또는 다리 층)
//     TileId    tile;
//     int8_t    heightLevel;     // 정수 층(시각 오프셋 환산은 02 PPU 48 규약)
//     SlopeType slope;
//     uint8_t   flags;           // solid, oneWayBridge, eventTrigger 등
// };
// 저장: 청크(32x32) SoA + "다중 컬럼 셀" 오버플로 리스트.
//
// struct TileProps {            // TilesetAsset의 타일별 속성(04 직렬화)
//     CollisionShape collision; float moveCost;
//     StringId materialTag;      // 발소리 등 → 06 오디오
//     StringId eventId;          // 이벤트 트리거 → Lua(05)
//     PropertyBag custom;        // 확장 키-값
// };
//
// class TilemapWorld {           // 런타임 질의 허브(렌더·충돌·내비 공유)
// public:
//     std::span<const TileCell> columnsAt(Vec2i cell) const;
//     float sampleHeight(Vec2 worldXY, int8_t level) const;    // 경사 보간 포함
//     void  setTile(Vec2i cell, int layer, TileId);            // 청크 dirty → 배치·내비 리베이크
//     void  resolveAutotile(RectInt region, int layer);        // 룰 평가(에디터·런타임 공용)
// };

} // namespace mye::tilemap
