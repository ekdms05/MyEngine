// SpriteImportTests.cpp — 스프라이트시트 임포트 + AtlasPacker 검증 (04, M3-A)
//
// aseprite.exe 없이 결정적으로 8방향 걷기 시트 메타(Aseprite --data JSON)를 코드로 생성해
//   ParseAsepriteJson 을 검증하고, SliceGrid 그리드 자르기·AtlasPacker 겹침/UV/왕복을 검증한다.
#include "TestFramework.h"

#include "mye/asset/AtlasPacker.h"
#include "mye/asset/SpriteImporter.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/core/Json.h"

#include <cmath>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::asset;

namespace {

// ---------------------------------------------------------------------------
// 결정적 8방향 걷기 시트 Aseprite --data JSON 생성 (aseprite.exe 불요).
//   8방향 × 4프레임 = 32프레임, 프레임 32×32, 시트 4행(방향 쌍)×8열 그리드로 배치.
//   태그 8개(walk_down/up/left/right/down_left ...), 각 4프레임. pivot 슬라이스 1개.
//   프레임에 footstep 이벤트는 클립 레벨(AnimEventMarker)로 M3-B 가 붙이므로 여기선 미포함.
// ---------------------------------------------------------------------------
struct DirDef {
    const char* name;
    int32_t     dir;   // 0..7 (행 = dir, 열 = 프레임 0..3)
};

std::string BuildEightDirWalkJson() {
    const int32_t frameW = 32, frameH = 32;
    const int32_t framesPerDir = 4;
    const std::vector<DirDef> dirs = {
        {"walk_down", 0},      {"walk_down_left", 1}, {"walk_left", 2},  {"walk_up_left", 3},
        {"walk_up", 4},        {"walk_up_right", 5},  {"walk_right", 6}, {"walk_down_right", 7},
    };
    const int32_t cols = framesPerDir;             // 4열
    const int32_t rows = static_cast<int32_t>(dirs.size());  // 8행
    const int32_t sheetW = cols * frameW;          // 128
    const int32_t sheetH = rows * frameH;          // 256

    std::string json = "{\n  \"frames\": [\n";
    // 프레임 순서: dir 0..7, 각 dir 내 frame 0..3 → 인덱스 = dir*4 + f.
    for (int32_t d = 0; d < rows; ++d) {
        for (int32_t f = 0; f < cols; ++f) {
            const int32_t x = f * frameW;
            const int32_t y = d * frameH;
            const int32_t dur = 100 + f * 10;   // per-frame duration 다양화(ms)
            json += "    { \"frame\": { \"x\": " + std::to_string(x) +
                    ", \"y\": " + std::to_string(y) +
                    ", \"w\": " + std::to_string(frameW) +
                    ", \"h\": " + std::to_string(frameH) +
                    " }, \"duration\": " + std::to_string(dur) + " }";
            const bool last = (d == rows - 1 && f == cols - 1);
            json += last ? "\n" : ",\n";
        }
    }
    json += "  ],\n";
    json += "  \"meta\": {\n";
    json += "    \"size\": { \"w\": " + std::to_string(sheetW) +
            ", \"h\": " + std::to_string(sheetH) + " },\n";
    // frameTags
    json += "    \"frameTags\": [\n";
    for (size_t i = 0; i < dirs.size(); ++i) {
        const int32_t from = static_cast<int32_t>(i) * framesPerDir;
        const int32_t to = from + framesPerDir - 1;
        json += std::string("      { \"name\": \"") + dirs[i].name +
                "\", \"from\": " + std::to_string(from) +
                ", \"to\": " + std::to_string(to) + ", \"direction\": \"forward\" }";
        json += (i + 1 < dirs.size()) ? ",\n" : "\n";
    }
    json += "    ],\n";
    // slices: pivot at (16, 30) in sheet-absolute for first key — 프레임 로컬 (16,30) 기대.
    json += "    \"slices\": [\n";
    json += "      { \"name\": \"pivot\", \"keys\": [ { \"frame\": 0, "
            "\"bounds\": { \"x\": 0, \"y\": 0, \"w\": 32, \"h\": 32 }, "
            "\"pivot\": { \"x\": 16, \"y\": 30 } } ] }\n";
    json += "    ]\n";
    json += "  }\n}\n";
    return json;
}

bool RectsOverlap(const RectInt& a, const RectInt& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

// ===========================================================================
// ParseAsepriteJson — 프레임·태그·pivot 정확성
// ===========================================================================
MYE_TEST(AsepriteParseFramesAndTags) {
    const std::string js = BuildEightDirWalkJson();
    auto parsed = json::Parse(js);
    MYE_EXPECT(parsed.HasValue());
    if (!parsed) return;

    auto data = ParseAsepriteJson(parsed.Value());
    MYE_EXPECT(data.HasValue());
    if (!data) return;

    const AsepriteData& ad = data.Value();
    // 32 프레임.
    MYE_EXPECT(ad.frames.size() == 32);
    MYE_EXPECT(ad.frameDurations.size() == 32);
    // 시트 크기.
    MYE_EXPECT(ad.sheetSize.x == 128 && ad.sheetSize.y == 256);
    // 프레임 0: (0,0,32,32), duration 100ms → 0.1s.
    MYE_EXPECT((ad.frames[0].rect == RectInt{0, 0, 32, 32}));
    MYE_EXPECT_NEAR(ad.frameDurations[0], 0.100f, 1e-4f);
    MYE_EXPECT_NEAR(ad.frameDurations[3], 0.130f, 1e-4f);
    // 프레임 5 (dir1, frame1): x=32, y=32.
    MYE_EXPECT((ad.frames[5].rect == RectInt{32, 32, 32, 32}));

    // 8개 태그 → 8개 클립, 각 4프레임.
    MYE_EXPECT(ad.clips.size() == 8);
    if (ad.clips.size() == 8) {
        MYE_EXPECT(ad.clips[0].name == "walk_down");
        MYE_EXPECT(ad.clips[0].frameIndices.size() == 4);
        MYE_EXPECT(ad.clips[0].frameIndices[0] == 0 && ad.clips[0].frameIndices[3] == 3);
        MYE_EXPECT(ad.clips[0].direction == AnimationClipData::Direction::Forward);
        // 클립별 duration 이 프레임 duration 과 정합.
        MYE_EXPECT_NEAR(ad.clips[0].frameDurations[0], 0.100f, 1e-4f);
        MYE_EXPECT(ad.clips[6].name == "walk_right");
        MYE_EXPECT(ad.clips[6].frameIndices[0] == 24);
    }
}

MYE_TEST(AsepritePivotSliceToFrameLocal) {
    const std::string js = BuildEightDirWalkJson();
    auto parsed = json::Parse(js);
    if (!parsed) { MYE_EXPECT(false); return; }
    auto data = ParseAsepriteJson(parsed.Value());
    if (!data) { MYE_EXPECT(false); return; }
    const AsepriteData& ad = data.Value();

    // pivot 슬라이스 존재.
    MYE_EXPECT(ad.slices.size() == 1);
    if (ad.slices.size() == 1) {
        MYE_EXPECT(ad.slices[0].name == "pivot");
        MYE_EXPECT(ad.slices[0].hasPivot);
        MYE_EXPECT((ad.slices[0].pivot == Vec2i{16, 30}));
    }
    // 프레임 0 pivot: 시트 절대 (16,30) - 프레임 rect (0,0) = (16,30) 로컬.
    MYE_EXPECT(ad.frames[0].pivotInPixels);
    MYE_EXPECT_NEAR(ad.frames[0].pivot.x, 16.0f, 1e-4f);
    MYE_EXPECT_NEAR(ad.frames[0].pivot.y, 30.0f, 1e-4f);
}

MYE_TEST(AsepriteTagRangeClamp) {
    // to 가 프레임 수를 초과하면 클램프 + 경고.
    const std::string js =
        "{ \"frames\": [ "
        "{ \"frame\": {\"x\":0,\"y\":0,\"w\":8,\"h\":8}, \"duration\": 50 }, "
        "{ \"frame\": {\"x\":8,\"y\":0,\"w\":8,\"h\":8}, \"duration\": 50 } ], "
        "\"meta\": { \"size\": {\"w\":16,\"h\":8}, "
        "\"frameTags\": [ { \"name\":\"bad\", \"from\":0, \"to\":99, \"direction\":\"pingpong\" } ] } }";
    auto parsed = json::Parse(js);
    MYE_EXPECT(parsed.HasValue());
    if (!parsed) return;
    auto data = ParseAsepriteJson(parsed.Value());
    MYE_EXPECT(data.HasValue());
    if (!data) return;
    const AsepriteData& ad = data.Value();
    MYE_EXPECT(ad.clips.size() == 1);
    if (ad.clips.size() == 1) {
        MYE_EXPECT(ad.clips[0].frameIndices.size() == 2);   // 99 → 클램프 to 1
        MYE_EXPECT(ad.clips[0].direction == AnimationClipData::Direction::PingPong);
    }
    MYE_EXPECT(!ad.warnings.empty());
}

// ===========================================================================
// SliceGrid — 균등 그리드 셀 rect·UV
// ===========================================================================
MYE_TEST(SpriteSheetSliceGridBasic) {
    SpriteSheetImportSettings s;
    s.frameSize = {32, 32};
    // 128×256 시트 → 4열 × 8행 = 32 셀.
    auto frames = SpriteSheetImporter::SliceGrid({128, 256}, s);
    MYE_EXPECT(frames.size() == 32);
    if (frames.size() == 32) {
        MYE_EXPECT((frames[0].rect == RectInt{0, 0, 32, 32}));
        // row-major: 인덱스 4 는 두 번째 행 시작 (0, 32).
        MYE_EXPECT((frames[4].rect == RectInt{0, 32, 32, 32}));
        MYE_EXPECT((frames[31].rect == RectInt{96, 224, 32, 32}));
        // UV: 프레임 0 → (0,0, 32/128, 32/256).
        MYE_EXPECT_NEAR(frames[0].uv.w, 32.0f / 128.0f, 1e-5f);
        MYE_EXPECT_NEAR(frames[0].uv.h, 32.0f / 256.0f, 1e-5f);
    }
}

MYE_TEST(SpriteSheetSliceGridMarginSpacing) {
    SpriteSheetImportSettings s;
    s.frameSize = {16, 16};
    s.margin = {2, 2};
    s.spacing = {1, 1};
    // 사용 폭 = (2 margin) + N*16 + (N-1)*1. 시트 폭 53 → (53-2+1)/(17) = 3 셀.
    auto frames = SpriteSheetImporter::SliceGrid({53, 53}, s);
    // 3×3 = 9 프레임.
    MYE_EXPECT(frames.size() == 9);
    if (frames.size() == 9) {
        MYE_EXPECT((frames[0].rect == RectInt{2, 2, 16, 16}));
        MYE_EXPECT((frames[1].rect == RectInt{19, 2, 16, 16}));   // 2 + 17
        MYE_EXPECT((frames[3].rect == RectInt{2, 19, 16, 16}));   // 다음 행
    }
}

MYE_TEST(SpriteSheetSliceGridSingleFrame) {
    SpriteSheetImportSettings s;
    s.frameSize = {0, 0};   // 단일 프레임
    auto frames = SpriteSheetImporter::SliceGrid({64, 48}, s);
    MYE_EXPECT(frames.size() == 1);
    if (!frames.empty()) {
        MYE_EXPECT((frames[0].rect == RectInt{0, 0, 64, 48}));
    }
}

// ===========================================================================
// AtlasPacker — 겹침 없음·UV 정확·리트리브 왕복·페이지 분할
// ===========================================================================
MYE_TEST(AtlasPackerNoOverlapAndUv) {
    std::vector<AtlasInput> inputs;
    // 다양한 크기 20개.
    for (uint64_t i = 0; i < 20; ++i) {
        const int32_t w = 16 + static_cast<int32_t>(i % 5) * 8;
        const int32_t h = 16 + static_cast<int32_t>((i * 3) % 4) * 8;
        inputs.push_back({i, {w, h}});
    }
    AtlasPackSettings settings;
    settings.maxPageSize = {256, 256};
    settings.padding = 2;
    settings.extrude = 1;

    auto result = AtlasPacker::Pack(inputs, settings);
    MYE_EXPECT(result.placements.size() == inputs.size());

    // id 매칭: placements[i].id == inputs[i].id (원본 순서 보존).
    for (size_t i = 0; i < inputs.size(); ++i) {
        MYE_EXPECT(result.placements[i].id == inputs[i].id);
    }

    // 배치된 각 rect 크기 == 입력 크기.
    for (size_t i = 0; i < inputs.size(); ++i) {
        MYE_EXPECT(result.placements[i].rect.w == inputs[i].size.x);
        MYE_EXPECT(result.placements[i].rect.h == inputs[i].size.y);
    }

    // 같은 페이지 내 이미지 rect 는 겹치지 않는다(padding/extrude 로 최소 간격 보장).
    for (size_t a = 0; a < result.placements.size(); ++a) {
        for (size_t b = a + 1; b < result.placements.size(); ++b) {
            if (result.placements[a].page != result.placements[b].page) continue;
            MYE_EXPECT(!RectsOverlap(result.placements[a].rect, result.placements[b].rect));
        }
    }

    // 겹침뿐 아니라 extrude/padding 간격도 확보되는지: 같은 페이지 rect 는 1px 이상 떨어짐.
    for (size_t a = 0; a < result.placements.size(); ++a) {
        for (size_t b = a + 1; b < result.placements.size(); ++b) {
            if (result.placements[a].page != result.placements[b].page) continue;
            RectInt ea = result.placements[a].rect;
            ea.x -= 1; ea.y -= 1; ea.w += 2; ea.h += 2;   // 1px extrude 영역
            // 확장한 a 가 b 와 겹치면 extrude 침범 — 실패.
            MYE_EXPECT(!RectsOverlap(ea, result.placements[b].rect));
        }
    }

    // UV 왕복: rect 를 페이지 크기로 정규화한 값과 uv 가 일치.
    for (const AtlasPlacement& pl : result.placements) {
        MYE_EXPECT(pl.page < result.pageSizes.size());
        if (pl.page >= result.pageSizes.size()) continue;
        const Vec2i ps = result.pageSizes[pl.page];
        const float pw = static_cast<float>(ps.x);
        const float ph = static_cast<float>(ps.y);
        MYE_EXPECT_NEAR(pl.uv.x, static_cast<float>(pl.rect.x) / pw, 1e-5f);
        MYE_EXPECT_NEAR(pl.uv.y, static_cast<float>(pl.rect.y) / ph, 1e-5f);
        MYE_EXPECT_NEAR(pl.uv.w, static_cast<float>(pl.rect.w) / pw, 1e-5f);
        MYE_EXPECT_NEAR(pl.uv.h, static_cast<float>(pl.rect.h) / ph, 1e-5f);
        // UV 범위 0..1.
        MYE_EXPECT(pl.uv.x >= 0.0f && pl.uv.x + pl.uv.w <= 1.0001f);
        MYE_EXPECT(pl.uv.y >= 0.0f && pl.uv.y + pl.uv.h <= 1.0001f);
    }

    // 페이지가 maxPageSize 를 넘지 않는다.
    for (const Vec2i& ps : result.pageSizes) {
        MYE_EXPECT(ps.x <= 256 && ps.y <= 256);
    }
}

MYE_TEST(AtlasPackerPageOverflow) {
    // maxPageSize 를 작게 잡아 여러 페이지로 분할되게 한다.
    std::vector<AtlasInput> inputs;
    for (uint64_t i = 0; i < 10; ++i) {
        inputs.push_back({i, {60, 60}});   // 60+padding+extrude ~ 66; 128 페이지에 2×2=4개/페이지
    }
    AtlasPackSettings settings;
    settings.maxPageSize = {128, 128};
    settings.padding = 1;
    settings.extrude = 1;

    auto result = AtlasPacker::Pack(inputs, settings);
    MYE_EXPECT(result.placements.size() == 10);
    // 여러 페이지 사용.
    MYE_EXPECT(result.pageSizes.size() >= 2);
    MYE_EXPECT(!result.overflowed);   // 각 셀은 페이지에 들어감

    // 페이지별 겹침 없음 재확인.
    for (size_t a = 0; a < result.placements.size(); ++a) {
        for (size_t b = a + 1; b < result.placements.size(); ++b) {
            if (result.placements[a].page != result.placements[b].page) continue;
            MYE_EXPECT(!RectsOverlap(result.placements[a].rect, result.placements[b].rect));
        }
    }
}

MYE_TEST(AtlasPackerOversizedOverflow) {
    // 페이지보다 큰 스프라이트 → overflowed=true, 나머지는 정상 배치.
    std::vector<AtlasInput> inputs = {
        {0, {32, 32}},
        {1, {500, 500}},   // 256 페이지 초과
        {2, {32, 32}},
    };
    AtlasPackSettings settings;
    settings.maxPageSize = {256, 256};
    auto result = AtlasPacker::Pack(inputs, settings);
    MYE_EXPECT(result.overflowed);
    MYE_EXPECT(result.placements.size() == 3);
    // 정상 항목은 유효 UV.
    MYE_EXPECT(result.placements[0].rect.w == 32);
    MYE_EXPECT(result.placements[2].rect.w == 32);
}

MYE_TEST(AtlasPackerPowerOfTwoPages) {
    std::vector<AtlasInput> inputs = {{0, {30, 30}}, {1, {30, 30}}};
    AtlasPackSettings settings;
    settings.maxPageSize = {256, 256};
    settings.powerOfTwo = true;
    auto result = AtlasPacker::Pack(inputs, settings);
    MYE_EXPECT(result.pageSizes.size() == 1);
    if (!result.pageSizes.empty()) {
        // 페이지 크기는 2의 거듭제곱.
        auto isPow2 = [](int32_t v) { return v > 0 && (v & (v - 1)) == 0; };
        MYE_EXPECT(isPow2(result.pageSizes[0].x));
        MYE_EXPECT(isPow2(result.pageSizes[0].y));
    }
}

MYE_TEST(AtlasPackerEmptyInput) {
    auto result = AtlasPacker::Pack({}, {});
    MYE_EXPECT(result.placements.empty());
    MYE_EXPECT(result.pageSizes.empty());
    MYE_EXPECT(!result.overflowed);
}
