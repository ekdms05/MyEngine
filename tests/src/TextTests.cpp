// TextTests.cpp — M5-A 한글 텍스트 렌더링 헤드리스 검증 (FontFace·GlyphAtlas·RichText·TextLayout)
//
// 검증:
//   - FreeType FontFace: 생성 TTF blob 로드, 한글/ASCII 글리프 실제 래스터(non-zero 픽셀).
//   - RichText: {color}{outline}{shadow}{icon}{link} 태그 파싱 + 스타일 스택 + 이스케이프.
//   - TextLayout: 한글 문자열 레이아웃(글리프 수·advance·정렬), char-wrap 줄바꿈 위치, 금칙.
//   - 아틀라스: (GPU 가용 시) GetOrRasterize 로 R8 페이지에 글리프 업로드 + 캐시 히트.
//   - 아틀라스 BMP 덤프: CPU 래스터 비트맵을 BMP 로 저장해 한글 글리프 픽셀 육안 확인.
//
// 폰트는 TestFontGen 으로 in-memory 생성(대용량 시스템 폰트 벤더링 회피, 라이선스 청정).
//   각 글리프는 "채워진 사각형" 아웃라인이라 래스터 시 확정적으로 non-zero 픽셀을 낸다.
#include "TestFramework.h"
#include "TestFontGen.h"

#include "mye/text/FontFace.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/RichText.h"
#include "mye/text/TextLayout.h"
#include "mye/text/TextRenderer.h"

#include "mye/rhi/Rhi.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

using namespace mye;

namespace {

// 테스트에 쓸 한글 음절 목록(연속 구간 + 산발).
const std::vector<uint32_t> kHangul = {
    U'안', U'녕', U'하', U'세', U'요',        // 안녕하세요
    U'모', U'험', U'가', U'님',              // 모험가님
    U'대', U'화', U'상', U'자',              // 대화상자
    U'가', U'나', U'다', U'라', U'마',        // 가나다라마
};

std::span<const std::byte> AsBytes(const std::vector<uint8_t>& v) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(v.data()), v.size());
}

// R8 비트맵을 8bit 그레이스케일 BMP(파일)로 덤프. non-zero 확인 겸 육안 검사용.
void DumpR8Bmp(const char* path, const uint8_t* pixels, uint32_t w, uint32_t h,
               uint32_t rowPitch) {
    if (w == 0 || h == 0) return;
    FILE* f = nullptr;
#ifdef _MSC_VER
    if (::fopen_s(&f, path, "wb") != 0) f = nullptr;
#else
    f = std::fopen(path, "wb");
#endif
    if (!f) return;
    const uint32_t rowBytes = (w + 3) & ~3u;           // 8bpp 행 4바이트 정렬
    const uint32_t imgSize = rowBytes * h;
    const uint32_t paletteBytes = 256 * 4;
    const uint32_t dataOff = 14 + 40 + paletteBytes;
    const uint32_t fileSize = dataOff + imgSize;
    auto w16 = [&](uint16_t v){ std::fputc(v & 0xFF, f); std::fputc((v >> 8) & 0xFF, f); };
    auto w32 = [&](uint32_t v){ for (int i=0;i<4;++i) std::fputc((v>>(i*8))&0xFF, f); };
    // BITMAPFILEHEADER
    std::fputc('B', f); std::fputc('M', f);
    w32(fileSize); w16(0); w16(0); w32(dataOff);
    // BITMAPINFOHEADER
    w32(40); w32(w); w32(h); w16(1); w16(8); w32(0);
    w32(imgSize); w32(2835); w32(2835); w32(256); w32(256);
    // grayscale palette
    for (int i = 0; i < 256; ++i) { std::fputc(i, f); std::fputc(i, f); std::fputc(i, f); std::fputc(0, f); }
    // pixels bottom-up
    std::vector<uint8_t> row(rowBytes, 0);
    for (int32_t y = int32_t(h) - 1; y >= 0; --y) {
        std::memset(row.data(), 0, rowBytes);
        for (uint32_t x = 0; x < w; ++x) row[x] = pixels[y * rowPitch + x];
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
}

uint32_t CountNonZero(const uint8_t* px, uint32_t w, uint32_t h, uint32_t pitch) {
    uint32_t c = 0;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            if (px[y * pitch + x] != 0) ++c;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// FontFace: FreeType 로드 + 래스터
// ---------------------------------------------------------------------------
MYE_TEST(TextFontFaceLoadAndMetrics) {
    auto ttf = testfont::MakeTestFont(kHangul);
    auto faceRes = text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf));
    MYE_EXPECT(faceRes.HasValue());
    if (!faceRes.HasValue()) return;
    auto face = std::move(faceRes).Value();

    // 메트릭: ascent/descent/lineHeight 가 양수.
    text::FontMetrics m = face->metrics(24);
    MYE_EXPECT(m.ascent > 0);
    MYE_EXPECT(m.descent > 0);
    MYE_EXPECT(m.lineHeight >= m.ascent + m.descent);

    // 한글·ASCII 글리프 존재.
    MYE_EXPECT(face->hasGlyph(U'안'));
    MYE_EXPECT(face->hasGlyph(U'A'));
    MYE_EXPECT(!face->hasGlyph(U'あ'));   // 히라가나 あ — 미포함
}

MYE_TEST(TextFontFaceRasterHangulNonZero) {
    auto ttf = testfont::MakeTestFont(kHangul);
    auto face = std::move(text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf)).Value());

    // 한글 '안' 을 24px 로 래스터 → 실제 픽셀이 나와야 한다.
    text::RasterBitmap rb = face->rasterize(U'안', 24, text::GlyphStyle::Normal,
                                            text::HintMode::Normal);
    MYE_EXPECT(rb.valid);
    MYE_EXPECT(rb.pixels != nullptr);
    MYE_EXPECT(rb.width > 0 && rb.height > 0);
    MYE_EXPECT(rb.advance > 0);
    if (rb.pixels) {
        const uint32_t nz = CountNonZero(rb.pixels, rb.width, rb.height, rb.rowPitch);
        MYE_EXPECT(nz > 0);   // 채워진 사각형 → 다수 non-zero
        // BMP 덤프(육안 확인용). 실패해도 테스트에 영향 없음.
        DumpR8Bmp("text_glyph_an.bmp", rb.pixels, rb.width, rb.height, rb.rowPitch);
    }

    // 스페이스: 유효하나 픽셀 없음, advance>0.
    text::RasterBitmap sp = face->rasterize(U' ', 24, text::GlyphStyle::Normal,
                                            text::HintMode::Normal);
    MYE_EXPECT(sp.valid);
    MYE_EXPECT(sp.advance > 0);
}

MYE_TEST(TextFontFaceOutlineRaster) {
    auto ttf = testfont::MakeTestFont(kHangul);
    auto face = std::move(text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf)).Value());
    face->setOutlineWidth(1.5f);
    text::RasterBitmap rb = face->rasterize(U'안', 32, text::GlyphStyle::Outline,
                                            text::HintMode::Normal);
    // 아웃라인 글리프도 래스터되어 non-zero(스트로크 테두리).
    MYE_EXPECT(rb.valid);
    if (rb.valid && rb.pixels)
        MYE_EXPECT(CountNonZero(rb.pixels, rb.width, rb.height, rb.rowPitch) > 0);
}

MYE_TEST(TextFontFaceMonoHint) {
    auto ttf = testfont::MakeTestFont(kHangul);
    auto face = std::move(text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf)).Value());
    // Mono 힌트: 1bpp → R8(0/255) 확장. 모든 픽셀은 0 또는 255.
    text::RasterBitmap rb = face->rasterize(U'가', 16, text::GlyphStyle::Normal,
                                            text::HintMode::Mono);
    MYE_EXPECT(rb.valid);
    if (rb.pixels) {
        bool binary = true;
        for (uint32_t y = 0; y < rb.height && binary; ++y)
            for (uint32_t x = 0; x < rb.width; ++x) {
                const uint8_t v = rb.pixels[y * rb.rowPitch + x];
                if (v != 0 && v != 255) { binary = false; break; }
            }
        MYE_EXPECT(binary);
    }
}

// ---------------------------------------------------------------------------
// RichText 태그 파싱
// ---------------------------------------------------------------------------
MYE_TEST(TextRichColorTag) {
    text::TextStyle base;
    base.color = Color::White();
    auto p = text::RichTextParser::Parse("보통 {color=#FF0000}빨강{/color} 끝", base);
    // 3개 텍스트 런: "보통 " / "빨강" / " 끝".
    MYE_EXPECT(p.runs.size() == 3);
    MYE_EXPECT(!p.hadError);
    if (p.runs.size() == 3) {
        MYE_EXPECT(p.runs[0].color == Color::White());
        MYE_EXPECT_NEAR(p.runs[1].color.r, 1.0f, 0.01f);
        MYE_EXPECT_NEAR(p.runs[1].color.g, 0.0f, 0.01f);
        MYE_EXPECT(p.runs[2].color == Color::White());   // 닫은 뒤 복원
    }
}

MYE_TEST(TextRichNestedAndOutlineShadow) {
    text::TextStyle base;
    auto p = text::RichTextParser::Parse(
        "{color=#00FF00}초록{outline=#000000}테두리{/outline}{/color}", base);
    // 런: "초록"(초록·아웃라인X) / "테두리"(초록·아웃라인O)
    MYE_EXPECT(p.runs.size() == 2);
    if (p.runs.size() == 2) {
        MYE_EXPECT(!p.runs[0].outline.has_value());
        MYE_EXPECT(p.runs[1].outline.has_value());
        MYE_EXPECT_NEAR(p.runs[1].color.g, 1.0f, 0.01f);
    }
}

MYE_TEST(TextRichIconAndLink) {
    text::TextStyle base;
    auto p = text::RichTextParser::Parse(
        "아이템 {icon=potion} {link=item:1234}물약{/link} 획득", base);
    bool hasIcon = false, hasLinkBegin = false, hasLinkEnd = false;
    for (auto& r : p.runs) {
        if (r.kind == text::RunKind::Icon) { hasIcon = true; MYE_EXPECT(r.iconName == "potion"); }
        if (r.kind == text::RunKind::LinkBegin) { hasLinkBegin = true; MYE_EXPECT(r.linkPayload == "item:1234"); }
        if (r.kind == text::RunKind::LinkEnd) hasLinkEnd = true;
    }
    MYE_EXPECT(hasIcon);
    MYE_EXPECT(hasLinkBegin);
    MYE_EXPECT(hasLinkEnd);
}

MYE_TEST(TextRichEscapeAndStrip) {
    text::TextStyle base;
    auto p = text::RichTextParser::Parse("리터럴 {{brace}} 표시", base);
    // '{{' → '{' 리터럴. '}' 는 태그 밖이므로 그대로.
    std::string joined;
    for (auto& r : p.runs) if (r.kind == text::RunKind::Text) joined += r.utf8Text;
    MYE_EXPECT(joined.find('{') != std::string::npos);

    // StripTags: 태그 제거, 이스케이프 복원.
    std::string stripped = text::RichTextParser::StripTags(
        "{color=#FF0000}빨강{/color}과 {{리터럴}}");
    MYE_EXPECT(stripped.find("빨강") != std::string::npos);
    MYE_EXPECT(stripped.find("color") == std::string::npos);   // 태그 사라짐
    MYE_EXPECT(stripped.find('{') != std::string::npos);       // {{ → {
}

// ---------------------------------------------------------------------------
// TextLayout (measure 경로 — 아틀라스 불필요)
// ---------------------------------------------------------------------------
MYE_TEST(TextLayoutHangulMeasure) {
    auto ttf = testfont::MakeTestFont(kHangul);
    text::FontRegistry reg;
    auto idRes = reg.RegisterTtf(AsBytes(ttf), "test");
    MYE_EXPECT(idRes.HasValue());
    if (!idRes.HasValue()) return;

    text::TextStyle base;
    base.font = idRes.Value();
    base.size = 24;

    text::LayoutParams params;
    params.wrap = text::WrapMode::None;

    text::TextLayout layout;
    Vec2i sz = layout.Measure(reg, "안녕하세요", base, params);
    // 5음절 → 폭 = 5 * advance(24px 에서 1024em advance → 24px). 높이 = lineHeight.
    MYE_EXPECT(sz.x > 0);
    MYE_EXPECT(sz.y > 0);
    // advance 는 em(1024) 기준 → 24px 에서 24. 5글자 ≈ 120.
    MYE_EXPECT(sz.x >= 24 * 4);   // 넉넉한 하한
    MYE_EXPECT(layout.lineCount() == 1);
}

MYE_TEST(TextLayoutCharWrap) {
    auto ttf = testfont::MakeTestFont(kHangul);
    text::FontRegistry reg;
    text::TextStyle base;
    base.font = reg.RegisterTtf(AsBytes(ttf)).Value();
    base.size = 24;

    // 한 글자 advance ≈ 24px. maxWidth=80 → 줄당 약 3글자 → 5글자는 2줄.
    text::LayoutParams params;
    params.maxWidth = 80.0f;
    params.wrap = text::WrapMode::Char;

    text::TextLayout layout;
    layout.Measure(reg, "안녕하세요", base, params);
    MYE_EXPECT(layout.lineCount() >= 2);   // char-wrap 으로 줄바꿈 발생
}

MYE_TEST(TextLayoutExplicitNewline) {
    auto ttf = testfont::MakeTestFont(kHangul);
    text::FontRegistry reg;
    text::TextStyle base;
    base.font = reg.RegisterTtf(AsBytes(ttf)).Value();
    base.size = 20;
    text::LayoutParams params;
    params.wrap = text::WrapMode::None;
    text::TextLayout layout;
    layout.Measure(reg, "가나\n다라마", base, params);
    MYE_EXPECT(layout.lineCount() == 2);
}

MYE_TEST(TextLayoutKinsokuNoStartForbidden) {
    // 금칙: 닫는 괄호가 줄 첫머리에 오지 않아야 한다.
    MYE_EXPECT(text::IsLineStartForbidden(U')'));
    MYE_EXPECT(text::IsLineStartForbidden(U'。'));
    MYE_EXPECT(!text::IsLineStartForbidden(U'가'));
    MYE_EXPECT(text::IsLineEndForbidden(U'('));
    MYE_EXPECT(text::IsCjkBreakable(U'한'));
    MYE_EXPECT(!text::IsCjkBreakable(U'A'));
}

// ---------------------------------------------------------------------------
// GlyphAtlas (GPU 경로 — 디바이스 가용 시). 캐시 히트·업로드·페이지 검증.
// ---------------------------------------------------------------------------
MYE_TEST(TextGlyphAtlasRasterAndCache) {
    auto devRes = rhi::CreateDevice(rhi::Backend::DX11, {});
    if (!devRes.HasValue()) {
        std::printf("    [skip] no DX11 device (headless CI without GPU)\n");
        return;   // GPU 없으면 스킵(테스트 실패 아님)
    }
    auto device = std::move(devRes).Value();

    auto ttf = testfont::MakeTestFont(kHangul);
    auto face = std::move(text::FreeTypeFace::Create(text::FontId{0}, AsBytes(ttf)).Value());

    text::GlyphAtlas atlas;
    text::GlyphAtlasConfig cfg;
    cfg.pageSize = 256;
    cfg.maxPages = 2;
    atlas.Init(*device, cfg);
    atlas.BeginFrame(1);

    rhi::ICommandContext& ctx = device->GetImmediateContext();

    text::GlyphKey key{text::FontId{0}, U'안', 24, text::GlyphStyle::Normal};
    const text::Glyph* g1 = atlas.GetOrRasterize(ctx, *face, key);
    MYE_EXPECT(g1 != nullptr);
    if (g1) {
        MYE_EXPECT(g1->valid);
        MYE_EXPECT(g1->atlasTexture.IsValid());
        MYE_EXPECT(g1->size.x > 0 && g1->size.y > 0);
        MYE_EXPECT(g1->uv.w > 0.0f && g1->uv.h > 0.0f);
    }
    const uint32_t uploadsAfterFirst = atlas.Stats().uploadsThisFrame;
    MYE_EXPECT(uploadsAfterFirst == 1);

    // 같은 키 재조회 → 캐시 히트(업로드 증가 없음).
    const text::Glyph* g2 = atlas.GetOrRasterize(ctx, *face, key);
    MYE_EXPECT(g2 != nullptr);
    MYE_EXPECT(atlas.Stats().uploadsThisFrame == uploadsAfterFirst);   // 히트 → 업로드 그대로

    // 여러 글리프 → 페이지 1개 안에 shelf-packing.
    for (uint32_t cp : kHangul)
        atlas.GetOrRasterize(ctx, *face, text::GlyphKey{text::FontId{0}, cp, 24, text::GlyphStyle::Normal});
    MYE_EXPECT(atlas.PageCount() >= 1);
    MYE_EXPECT(atlas.Stats().glyphCount > 1);

    atlas.Shutdown();
}

MYE_TEST(TextLayoutSetWithAtlas) {
    auto devRes = rhi::CreateDevice(rhi::Backend::DX11, {});
    if (!devRes.HasValue()) {
        std::printf("    [skip] no DX11 device\n");
        return;
    }
    auto device = std::move(devRes).Value();

    auto ttf = testfont::MakeTestFont(kHangul);
    text::FontRegistry reg;
    text::FontId fid = reg.RegisterTtf(AsBytes(ttf)).Value();

    text::GlyphAtlas atlas;
    atlas.Init(*device);
    atlas.BeginFrame(1);
    rhi::ICommandContext& ctx = device->GetImmediateContext();

    text::TextStyle base;
    base.font = fid;
    base.size = 24;
    text::LayoutParams params;
    params.wrap = text::WrapMode::None;

    text::TextLayout layout;
    layout.Set(ctx, atlas, reg, "안녕하세요", base, params);
    // 5음절 모두 배치되고 아틀라스 텍스처·uv 를 가진다.
    MYE_EXPECT(layout.glyphs().size() == 5);
    uint32_t withTex = 0;
    for (const auto& pg : layout.glyphs())
        if (pg.atlasTexture.IsValid() && pg.uv.w > 0.0f) ++withTex;
    MYE_EXPECT(withTex == 5);

    atlas.Shutdown();
}

// 리치텍스트 + 레이아웃 통합: 링크 히트렉트가 생성되는지(measure 경로).
MYE_TEST(TextLayoutLinkRegions) {
    auto ttf = testfont::MakeTestFont(kHangul);
    text::FontRegistry reg;
    text::TextStyle base;
    base.font = reg.RegisterTtf(AsBytes(ttf)).Value();
    base.size = 24;
    text::LayoutParams params;

    // Measure 경로는 결과를 저장하지 않으므로, 링크 검증은 Set 경로에서. 디바이스 없으면 스킵.
    auto devRes = rhi::CreateDevice(rhi::Backend::DX11, {});
    if (!devRes.HasValue()) {
        std::printf("    [skip] no DX11 device (link regions need Set path)\n");
        return;
    }
    auto device = std::move(devRes).Value();
    text::GlyphAtlas atlas;
    atlas.Init(*device);
    atlas.BeginFrame(1);
    rhi::ICommandContext& ctx = device->GetImmediateContext();

    text::TextLayout layout;
    layout.Set(ctx, atlas, reg, "{link=npc:1}대화{/link}", base, params);
    MYE_EXPECT(layout.links().size() >= 1);
    if (layout.links().size() >= 1) {
        std::string_view payload = layout.linkPayload(layout.links()[0]);
        MYE_EXPECT(payload == "npc:1");
        MYE_EXPECT(layout.links()[0].rect.w > 0.0f);
    }
    atlas.Shutdown();
}
