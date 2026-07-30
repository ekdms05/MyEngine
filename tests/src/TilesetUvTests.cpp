// TilesetUvTests.cpp — 타일 아틀라스 UV 계산(순수 로직) (docs/03 §5, M7 렌더 잔여)
#include "TestFramework.h"

#include "mye/tilemap/Tileset.h"

#include <cmath>

using namespace mye;
using namespace mye::tilemap;

namespace { bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; } }

MYE_TEST(TilesetSimpleGridUV) {
    // 128x128 아틀라스, 32px 타일, 여백/간격 0 → 4x4 = 16타일, firstGid 1.
    Tileset ts;
    ts.atlasWidth = 128; ts.atlasHeight = 128; ts.tileWidth = 32; ts.tileHeight = 32;
    ts.firstGid = 1;
    MYE_EXPECT(ts.Columns() == 4 && ts.Rows() == 4 && ts.TileCount() == 16);

    // 타일 1(인덱스 0) = 좌상단.
    TileUV a = ComputeTileUV(ts, 1);
    MYE_EXPECT(a.valid);
    MYE_EXPECT(Near(a.u0, 0.0f) && Near(a.v0, 0.0f) && Near(a.u1, 0.25f) && Near(a.v1, 0.25f));

    // 타일 2(인덱스 1) = 같은 행 다음 열.
    TileUV b = ComputeTileUV(ts, 2);
    MYE_EXPECT(Near(b.u0, 0.25f) && Near(b.v0, 0.0f) && Near(b.u1, 0.5f) && Near(b.v1, 0.25f));

    // 타일 5(인덱스 4) = 다음 행 첫 열(row-major).
    TileUV c = ComputeTileUV(ts, 5);
    MYE_EXPECT(Near(c.u0, 0.0f) && Near(c.v0, 0.25f) && Near(c.u1, 0.25f) && Near(c.v1, 0.5f));

    // 타일 16(인덱스 15) = 우하단.
    TileUV d = ComputeTileUV(ts, 16);
    MYE_EXPECT(Near(d.u0, 0.75f) && Near(d.v0, 0.75f) && Near(d.u1, 1.0f) && Near(d.v1, 1.0f));
}

MYE_TEST(TilesetMarginSpacingUV) {
    // 여백 4, 간격 2, 타일 32 → 열 폭 = 4행(atlasWidth=142).
    Tileset ts;
    ts.tileWidth = 32; ts.tileHeight = 32; ts.margin = 4; ts.spacing = 2;
    ts.atlasWidth = 4 * 32 + 3 * 2 + 2 * 4;   // 142
    ts.atlasHeight = ts.atlasWidth;
    ts.firstGid = 1;
    MYE_EXPECT(ts.Columns() == 4 && ts.Rows() == 4);

    // 인덱스 0: px = margin = 4.
    TileUV a = ComputeTileUV(ts, 1);
    MYE_EXPECT(a.valid);
    MYE_EXPECT(Near(a.u0, 4.0f / 142.0f) && Near(a.u1, 36.0f / 142.0f));

    // 인덱스 1(col 1): px = 4 + (32+2) = 38.
    TileUV b = ComputeTileUV(ts, 2);
    MYE_EXPECT(Near(b.u0, 38.0f / 142.0f) && Near(b.u1, 70.0f / 142.0f));

    // 인덱스 4(row 1 col 0): py = 4 + 34 = 38.
    TileUV c = ComputeTileUV(ts, 5);
    MYE_EXPECT(Near(c.v0, 38.0f / 142.0f) && Near(c.u0, 4.0f / 142.0f));
}

MYE_TEST(TilesetFirstGidAndBounds) {
    Tileset ts;
    ts.atlasWidth = 64; ts.atlasHeight = 32; ts.tileWidth = 32; ts.tileHeight = 32;
    ts.firstGid = 10;   // 이 타일셋은 id 10..11 (2타일: 2x1)
    MYE_EXPECT(ts.Columns() == 2 && ts.Rows() == 1 && ts.TileCount() == 2);
    MYE_EXPECT(ts.Contains(10) && ts.Contains(11) && !ts.Contains(12) && !ts.Contains(9));

    // firstGid 오프셋: 타일 10 = 인덱스 0.
    TileUV a = ComputeTileUV(ts, 10);
    MYE_EXPECT(a.valid && Near(a.u0, 0.0f) && Near(a.u1, 0.5f));
    // 타일 11 = 인덱스 1.
    TileUV b = ComputeTileUV(ts, 11);
    MYE_EXPECT(b.valid && Near(b.u0, 0.5f) && Near(b.u1, 1.0f));
}

MYE_TEST(TilesetInvalidFallsBackToFullAtlas) {
    Tileset ts;
    ts.atlasWidth = 128; ts.atlasHeight = 128; ts.tileWidth = 32; ts.tileHeight = 32;
    ts.firstGid = 1;

    // 빈 타일(0) → 전체 아틀라스 폴백, valid=false.
    TileUV empty = ComputeTileUV(ts, kNoTile);
    MYE_EXPECT(!empty.valid);
    MYE_EXPECT(Near(empty.u0, 0.0f) && Near(empty.v0, 0.0f) && Near(empty.u1, 1.0f) && Near(empty.v1, 1.0f));

    // 범위 밖 타일 → 폴백.
    TileUV oob = ComputeTileUV(ts, 999);
    MYE_EXPECT(!oob.valid && Near(oob.u1, 1.0f));

    // 미설정(0 크기) 타일셋 → 폴백(크래시 없음).
    Tileset bad;
    MYE_EXPECT(bad.Columns() == 0 && bad.TileCount() == 0);
    TileUV b = ComputeTileUV(bad, 1);
    MYE_EXPECT(!b.valid);
}
