// mye/text/FontFace.cpp — FreeType 폰트 페이스 구현 (M5-A)
//
// FreeType 로 TTF/OTF blob 을 열어 (codepoint, pixelSize, style) → R8 래스터 비트맵을 생산한다.
// FT 헤더는 이 구현 TU 에만 포함(소비자 노출 금지). FT_Library 는 프로세스 공유 싱글턴.
//
// 스타일:
//   - Normal/Light/Mono 힌트 모드 → FT_LOAD_TARGET_*  (Mono 는 1bpp → R8(0/255) 확장).
//   - GlyphStyle::Outline → FT_Stroker 로 아웃라인만 남긴 글리프를 별도 슬롯에 래스터.
#include "mye/text/FontFace.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_OUTLINE_H

#include <mutex>

namespace mye::text {
namespace {

// FT_Library 프로세스 싱글턴. FreeType 라이브러리 오브젝트는 스레드 세이프가 아니므로
//   래스터 경로 전체를 이 뮤텍스로 직렬화한다(텍스트는 렌더 스레드 단일 소비가 기본).
struct FreeTypeLib {
    FT_Library lib = nullptr;
    std::mutex mtx;
    FreeTypeLib() { FT_Init_FreeType(&lib); }
    ~FreeTypeLib() { if (lib) FT_Done_FreeType(lib); }
};

FreeTypeLib& Lib() {
    static FreeTypeLib s;
    return s;
}

// 26.6 고정소수 → 정수 픽셀(반올림).
inline int16_t Round266(FT_Pos v) {
    return static_cast<int16_t>((v + 32) >> 6);
}

FT_Int32 LoadFlagsFor(HintMode hint) {
    switch (hint) {
        case HintMode::Mono:  return FT_LOAD_TARGET_MONO;
        case HintMode::Light: return FT_LOAD_TARGET_LIGHT;
        case HintMode::Normal:
        default:              return FT_LOAD_TARGET_NORMAL;
    }
}

FT_Render_Mode RenderModeFor(HintMode hint) {
    return hint == HintMode::Mono ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL;
}

} // namespace

FreeTypeFace::~FreeTypeFace() {
    if (m_face) {
        std::lock_guard<std::mutex> g(Lib().mtx);
        FT_Done_Face(reinterpret_cast<FT_Face>(m_face));
        m_face = nullptr;
    }
}

Expected<std::unique_ptr<FreeTypeFace>, Error>
FreeTypeFace::Create(FontId id, std::span<const std::byte> ttfBlob, int faceIndex) {
    if (ttfBlob.empty())
        return Error{"FreeTypeFace::Create: empty TTF blob", 1};

    auto face = std::make_unique<FreeTypeFace>();
    face->m_id = id;
    face->m_blob.assign(ttfBlob.begin(), ttfBlob.end());

    std::lock_guard<std::mutex> g(Lib().mtx);
    if (!Lib().lib)
        return Error{"FreeTypeFace::Create: FT_Init_FreeType failed", 2};

    FT_Face ft = nullptr;
    const FT_Error err = FT_New_Memory_Face(
        Lib().lib,
        reinterpret_cast<const FT_Byte*>(face->m_blob.data()),
        static_cast<FT_Long>(face->m_blob.size()),
        static_cast<FT_Long>(faceIndex),
        &ft);
    if (err != 0 || ft == nullptr)
        return Error{"FreeTypeFace::Create: FT_New_Memory_Face failed", 3};

    // 유니코드 charmap 선택(기본이 아닐 수 있음 — 심볼 폰트 대비).
    FT_Select_Charmap(ft, FT_ENCODING_UNICODE);

    face->m_face = ft;
    return face;
}

FontMetrics FreeTypeFace::metrics(uint16_t pixelSize) const {
    FontMetrics m{};
    FT_Face ft = reinterpret_cast<FT_Face>(m_face);
    if (!ft || pixelSize == 0) {
        // 폴백 근사(폰트 없음): 0.8/0.2 비율.
        m.ascent  = static_cast<int16_t>(pixelSize * 0.8f);
        m.descent = static_cast<int16_t>(pixelSize * 0.2f);
        m.lineHeight = static_cast<int16_t>(m.ascent + m.descent);
        return m;
    }
    std::lock_guard<std::mutex> gl(Lib().mtx);
    FT_Set_Pixel_Sizes(ft, 0, pixelSize);
    const FT_Size_Metrics& sm = ft->size->metrics;
    m.ascent  = Round266(sm.ascender);          // 양수
    m.descent = static_cast<int16_t>(-Round266(sm.descender));  // sm.descender 는 음수
    // lineGap = height - (ascent + descent). 음수면 0 클램프.
    const int16_t height = Round266(sm.height);
    int16_t gap = static_cast<int16_t>(height - (m.ascent + m.descent));
    if (gap < 0) gap = 0;
    m.lineGap = gap;
    m.lineHeight = static_cast<int16_t>(m.ascent + m.descent + m.lineGap);
    return m;
}

bool FreeTypeFace::hasGlyph(char32_t cp) const {
    FT_Face ft = reinterpret_cast<FT_Face>(m_face);
    if (!ft) return false;
    std::lock_guard<std::mutex> gl(Lib().mtx);
    return FT_Get_Char_Index(ft, static_cast<FT_ULong>(cp)) != 0;
}

RasterBitmap FreeTypeFace::rasterize(char32_t cp, uint16_t pixelSize,
                                     GlyphStyle style, HintMode hint) {
    RasterBitmap rb{};
    FT_Face ft = reinterpret_cast<FT_Face>(m_face);
    if (!ft || pixelSize == 0) return rb;

    std::lock_guard<std::mutex> gl(Lib().mtx);

    if (m_lastPixelSize != pixelSize) {
        if (FT_Set_Pixel_Sizes(ft, 0, pixelSize) != 0) return rb;
        m_lastPixelSize = pixelSize;
    }

    const FT_UInt gi = FT_Get_Char_Index(ft, static_cast<FT_ULong>(cp));
    if (gi == 0) {
        // 글리프 없음. 폴백 체인이 상위에서 처리하므로 실패로 반환.
        return rb;
    }

    const bool outline = HasFlag(style, GlyphStyle::Outline);
    // 아웃라인은 스케일러블 아웃라인이 필요 → 항상 NORMAL 로드(비트맵 전용 폰트는 아웃라인 불가).
    const FT_Int32 loadFlags = outline ? FT_LOAD_DEFAULT : LoadFlagsFor(hint);

    if (FT_Load_Glyph(ft, gi, loadFlags) != 0) return rb;

    FT_GlyphSlot slot = ft->glyph;

    // advance 는 스타일과 무관하게 본체 것을 쓴다(26.6 → px 반올림).
    rb.advance = Round266(slot->advance.x);

    if (outline) {
        // FT_Stroker 로 아웃라인 스트로크만 남긴 비트맵 생성.
        if (slot->format != FT_GLYPH_FORMAT_OUTLINE) {
            // 아웃라인 불가(비트맵 폰트 등) → 실패(공백 아님, 상위가 본체만 그림).
            return rb;
        }
        FT_Glyph glyph = nullptr;
        if (FT_Get_Glyph(slot, &glyph) != 0) return rb;

        FT_Stroker stroker = nullptr;
        if (FT_Stroker_New(Lib().lib, &stroker) != 0) {
            FT_Done_Glyph(glyph);
            return rb;
        }
        const FT_Fixed radius = static_cast<FT_Fixed>(m_outlineWidthPx * 64.0f);
        FT_Stroker_Set(stroker, radius, FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
        // StrokeBorder(outside) — 본체 바깥 테두리만. destroy=1 로 원본 교체.
        FT_Glyph_StrokeBorder(&glyph, stroker, 0 /*inside=false → outside*/, 1);
        FT_Stroker_Done(stroker);

        if (FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, 1) != 0) {
            FT_Done_Glyph(glyph);
            return rb;
        }
        FT_BitmapGlyph bmpGlyph = reinterpret_cast<FT_BitmapGlyph>(glyph);
        const FT_Bitmap& bmp = bmpGlyph->bitmap;

        const uint32_t w = bmp.width, h = bmp.rows;
        m_scratch.assign(static_cast<size_t>(w) * h, 0);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                m_scratch[y * w + x] = bmp.buffer[y * bmp.pitch + x];

        rb.pixels = m_scratch.data();
        rb.width = w;
        rb.height = h;
        rb.rowPitch = w;
        rb.bearing = Vec2i{bmpGlyph->left, bmpGlyph->top};
        rb.valid = true;
        FT_Done_Glyph(glyph);
        return rb;
    }

    // 본체 래스터.
    if (FT_Render_Glyph(slot, RenderModeFor(hint)) != 0) {
        // 렌더 실패지만 공백류(advance>0, 픽셀 없음)로 취급.
        rb.valid = true;
        rb.bearing = Vec2i{slot->bitmap_left, slot->bitmap_top};
        return rb;
    }

    const FT_Bitmap& bmp = slot->bitmap;
    const uint32_t w = bmp.width, h = bmp.rows;

    if (w == 0 || h == 0) {
        // 공백(스페이스 등): 픽셀 없음이지만 유효.
        rb.valid = true;
        rb.bearing = Vec2i{slot->bitmap_left, slot->bitmap_top};
        return rb;
    }

    m_scratch.assign(static_cast<size_t>(w) * h, 0);

    if (bmp.pixel_mode == FT_PIXEL_MODE_MONO) {
        // 1bpp → R8(0/255) 확장.
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = bmp.buffer + static_cast<ptrdiff_t>(y) * bmp.pitch;
            for (uint32_t x = 0; x < w; ++x) {
                const uint8_t bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
                m_scratch[y * w + x] = bit ? 255 : 0;
            }
        }
    } else {
        // FT_PIXEL_MODE_GRAY (R8 그레이) — pitch 는 부호 있음(음수=상하 반전) 대비.
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = bmp.buffer + static_cast<ptrdiff_t>(y) * bmp.pitch;
            for (uint32_t x = 0; x < w; ++x)
                m_scratch[y * w + x] = row[x];
        }
    }

    rb.pixels = m_scratch.data();
    rb.width = w;
    rb.height = h;
    rb.rowPitch = w;
    rb.bearing = Vec2i{slot->bitmap_left, slot->bitmap_top};
    rb.valid = true;
    return rb;
}

} // namespace mye::text
