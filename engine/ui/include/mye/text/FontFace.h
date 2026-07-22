// mye/text/FontFace.h — FreeType 폰트 페이스 래퍼 + IFont 인터페이스 (docs/06 §2)
//
// 소유: 06 (M5-A). FreeType FT_Face 를 감싸 (codepoint, pixelSize, style) → 래스터 비트맵을
//   생산한다. FT 헤더는 이 파일에 노출하지 않는다(구현 .cpp 에만) — 소비자는 순수 mye 타입만 본다.
//
// IFont: FreeType 폰트(FreeTypeFace)와 비트맵 도트 폰트(BitmapFont)의 공통 인터페이스.
//   GlyphAtlas 가 IFont 를 통해 캐시 미스 시 래스터라이즈를 요청한다.
#pragma once

#include "mye/text/TextTypes.h"
#include "mye/core/Base.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mye::text {

// 래스터 결과(아틀라스에 적재하기 전의 CPU 비트맵). R8(그레이) 또는 확장된 MONO.
//   pixels 는 rowPitch*height 바이트. 소유는 호출자에게 없음 — FontFace 내부 스크래치 버퍼를
//   가리키므로 다음 RasterizeGlyph 호출 전까지만 유효(즉시 아틀라스로 복사할 것).
struct RasterBitmap {
    const uint8_t* pixels = nullptr;  // R8 그레이 (0=투명, 255=불투명)
    uint32_t width = 0, height = 0;
    uint32_t rowPitch = 0;            // 바이트/행 (>= width)
    Vec2i    bearing{};               // bearingX, bearingY
    int16_t  advance = 0;
    bool     valid = false;
};

// 폰트 공통 인터페이스. GlyphAtlas·워밍업 경로가 소비.
class IFont {
public:
    virtual ~IFont() = default;

    virtual FontId  id() const = 0;
    virtual FontMetrics metrics(uint16_t pixelSize) const = 0;

    // 코드포인트가 이 폰트에 글리프로 존재하는가(폴백 체인 판정용).
    virtual bool hasGlyph(char32_t cp) const = 0;

    // 캐시 미스 시 래스터라이즈. 결과 버퍼는 다음 호출까지만 유효(위 주석).
    //   실패(글리프 없음·래스터 오류) 시 valid=false. 공백류는 valid=true·pixels=null·advance>0.
    virtual RasterBitmap rasterize(char32_t cp, uint16_t pixelSize,
                                   GlyphStyle style, HintMode hint) = 0;
};

// FreeType 기반 폰트 페이스. TTF/OTF blob 을 소유(복사)해 FT_Face 를 연다.
//   blob 소유 이유: FreeType 은 스트림을 지연 참조하므로 페이스 수명 동안 살아있어야 한다.
class FreeTypeFace final : public IFont {
public:
    FreeTypeFace() = default;
    ~FreeTypeFace() override;
    FreeTypeFace(const FreeTypeFace&) = delete;
    FreeTypeFace& operator=(const FreeTypeFace&) = delete;

    // TTF/OTF 바이트(에셋 FontImporter blob 또는 파일 로드)로 페이스 생성. blob 은 내부 복사.
    //   faceIndex: 컬렉션(.ttc) 내 페이스 인덱스(기본 0). outlinePx: 아웃라인 스트로크 폭(스타일용).
    static Expected<std::unique_ptr<FreeTypeFace>, Error>
    Create(FontId id, std::span<const std::byte> ttfBlob, int faceIndex = 0);

    FontId id() const override { return m_id; }
    FontMetrics metrics(uint16_t pixelSize) const override;
    bool hasGlyph(char32_t cp) const override;
    RasterBitmap rasterize(char32_t cp, uint16_t pixelSize,
                           GlyphStyle style, HintMode hint) override;

    // 아웃라인 스타일의 스트로크 폭(픽셀). 기본 1. GlyphStyle::Outline 래스터 시 적용.
    void setOutlineWidth(float px) { m_outlineWidthPx = px; }

private:
    FontId  m_id{};
    float   m_outlineWidthPx = 1.0f;
    std::vector<std::byte> m_blob;   // FT_Face 가 참조하는 원본 바이트(수명 유지)
    void*   m_face = nullptr;        // FT_Face (불투명 — 구현에서 캐스팅)
    uint16_t m_lastPixelSize = 0;    // FT_Set_Pixel_Sizes 캐싱
    std::vector<uint8_t> m_scratch;  // rasterize 결과 R8 버퍼(MONO 확장 포함)
};

} // namespace mye::text
