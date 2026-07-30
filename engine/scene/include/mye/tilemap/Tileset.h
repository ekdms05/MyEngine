// mye/tilemap/Tileset.h — 타일 아틀라스 레이아웃 + 타일 UV 계산 (docs/03 §5, M7 렌더 잔여)
//
// 타일맵 렌더가 타일 ID 를 아틀라스의 정확한 subrect UV 로 변환하도록, 아틀라스 레이아웃 메타
// (아틀라스 픽셀 크기·타일 픽셀 크기·여백margin·간격spacing·firstGid)를 데이터로 보유한다.
// ComputeTileUV 는 순수 함수 — GPU 무관·결정론(단위 테스트 대상). BuildTileChunkQuads 가 인용.
#pragma once

#include "mye/tilemap/Tilemap.h"   // TileId, kNoTile

#include <cstdint>

namespace mye::tilemap {

// 정규화 UV 사각형([0,1] 텍스처 좌표). u0<u1, v0<v1(위→아래).
struct TileUV {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    bool valid = false;   // 유효 타일이면 true(무효 시 전체 아틀라스 [0,1] 폴백)
};

// 아틀라스 레이아웃 메타. 픽셀 단위. firstGid 는 이 타일셋의 첫 타일 id(전역 id 오프셋).
//   타일 배치는 좌상단부터 행 우선(row-major): 인덱스 0 = 좌상단, 오른쪽으로 진행 후 다음 행.
struct Tileset {
    int    atlasWidth  = 0;    // 아틀라스 텍스처 폭(px)
    int    atlasHeight = 0;    // 높이(px)
    int    tileWidth   = 0;    // 타일 한 칸 폭(px)
    int    tileHeight  = 0;    // 높이(px)
    int    margin      = 0;    // 아틀라스 가장자리 여백(px)
    int    spacing     = 0;    // 타일 사이 간격(px)
    TileId firstGid    = 1;    // 이 타일셋 첫 타일의 전역 id(보통 1; 0=빈 타일)

    // 한 행의 타일 수(열 수). 유효 레이아웃이 아니면 0.
    int Columns() const {
        if (tileWidth <= 0 || atlasWidth <= 0) return 0;
        const int usable = atlasWidth - 2 * margin + spacing;
        const int stride = tileWidth + spacing;
        return stride > 0 ? (usable / stride) : 0;
    }
    int Rows() const {
        if (tileHeight <= 0 || atlasHeight <= 0) return 0;
        const int usable = atlasHeight - 2 * margin + spacing;
        const int stride = tileHeight + spacing;
        return stride > 0 ? (usable / stride) : 0;
    }
    int TileCount() const { return Columns() * Rows(); }

    // 이 타일셋이 tileId 를 담는가(firstGid .. firstGid+TileCount-1). kNoTile 은 항상 false.
    bool Contains(TileId tileId) const {
        if (tileId == kNoTile) return false;
        const int n = TileCount();
        return n > 0 && tileId >= firstGid && tileId < firstGid + static_cast<TileId>(n);
    }
};

// 타일 id → 아틀라스 UV subrect. 유효하지 않으면 전체 아틀라스 [0,1](valid=false) 폴백.
inline TileUV ComputeTileUV(const Tileset& ts, TileId tileId) {
    TileUV uv;   // 기본 [0,1], valid=false
    if (!ts.Contains(tileId)) return uv;
    const int cols = ts.Columns();
    if (cols <= 0 || ts.atlasWidth <= 0 || ts.atlasHeight <= 0) return uv;

    const int index = static_cast<int>(tileId - ts.firstGid);   // 0-based 로컬 인덱스
    const int col = index % cols;
    const int row = index / cols;

    const int px = ts.margin + col * (ts.tileWidth + ts.spacing);
    const int py = ts.margin + row * (ts.tileHeight + ts.spacing);

    const float aw = static_cast<float>(ts.atlasWidth);
    const float ah = static_cast<float>(ts.atlasHeight);
    uv.u0 = static_cast<float>(px) / aw;
    uv.v0 = static_cast<float>(py) / ah;
    uv.u1 = static_cast<float>(px + ts.tileWidth)  / aw;
    uv.v1 = static_cast<float>(py + ts.tileHeight) / ah;
    uv.valid = true;
    return uv;
}

} // namespace mye::tilemap
