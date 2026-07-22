// mye/text/BitmapFont.h — 비트맵 도트 폰트(BMFont/Aseprite 시트) (docs/06 §2 비트맵 폰트)
//
// 소유: 06 (M5-A 계약, 구현은 M5-B). 도트 느낌 폰트를 FreeType 폰트와 동일한 IFont 인터페이스로
//   지원한다. 글리프는 이미 시트에 구워진 비트맵이므로 rasterize 는 시트에서 서브렉트를 잘라
//   R8 로 반환한다(FT 힌팅 불필요). 한글 도트 폰트는 이 경로 또는 FT MONO 힌팅 둘 다 가능.
//
// 포맷: BMFont(.fnt 텍스트/바이너리) + 페이지 텍스처, 또는 Aseprite 격자 시트 + 코드포인트 매핑.
//   MVP 계약은 인터페이스 동결만 — 실제 파서·시트 로드는 M5-B.
#pragma once

#include "mye/text/TextTypes.h"
#include "mye/text/FontFace.h"
#include "mye/core/Base.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::text {

// 비트맵 글리프 시트 내 한 글리프의 배치 정보(픽셀).
struct BitmapGlyphRecord {
    Vec2i sheetPos{};    // 시트 내 좌상단 픽셀
    Vec2i size{};        // 글리프 픽셀 크기
    Vec2i bearing{};     // bearingX, bearingY
    int16_t advance = 0;
};

// 비트맵 폰트. IFont 로 노출되어 FreeTypeFace 와 상호 교체 가능.
class BitmapFont final : public IFont {
public:
    BitmapFont() = default;
    ~BitmapFont() override = default;
    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    // BMFont .fnt(텍스트) + R8/RGBA 페이지 픽셀로 생성. nativePixelSize = 이 시트가 구워진 크기.
    //   비트맵 폰트는 자유 스케일 안 함 — 요청 pixelSize != nativePixelSize 면 정수배만 허용.
    static Expected<std::unique_ptr<BitmapFont>, Error>
    CreateFromBmfont(FontId id, std::string_view fntText,
                     std::span<const std::byte> pageR8, Vec2i pageSize);

    FontId id() const override { return m_id; }
    FontMetrics metrics(uint16_t pixelSize) const override;
    bool hasGlyph(char32_t cp) const override;
    RasterBitmap rasterize(char32_t cp, uint16_t pixelSize,
                           GlyphStyle style, HintMode hint) override;

    uint16_t nativePixelSize() const { return m_nativePixelSize; }

private:
    FontId   m_id{};
    uint16_t m_nativePixelSize = 0;
    Vec2i    m_pageSize{};
    FontMetrics m_metrics{};
    std::vector<uint8_t> m_pageR8;   // 시트 픽셀(R8)
    std::vector<uint8_t> m_scratch;  // rasterize 반환용 잘라낸 서브렉트
    std::unordered_map<char32_t, BitmapGlyphRecord> m_glyphs;
};

} // namespace mye::text
