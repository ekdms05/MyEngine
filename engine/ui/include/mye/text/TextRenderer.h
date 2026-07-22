// mye/text/TextRenderer.h — 글리프 쿼드를 SpriteBatch 로 배치 (docs/06 §2)
//
// 소유: 06 (M5-A). TextLayout 결과(PositionedGlyph[])를 render::SpriteBatch::Submit 으로
//   글리프 쿼드로 제출한다. 드로우 순서: 그림자 → 아웃라인 → 본체(셰이더 트릭 대신 배처 3패스 —
//   docs/06 §2, 픽셀 정확). 정수 픽셀 스냅. 스크린 스페이스(UI 레이어)에서 사용.
//
// FontRegistry: FontFace 들을 소유하고 FontId 발급·폴백 체인 관리. IFontProvider 구현.
//   TextLayout 이 요구하는 폰트 해석기이자, GlyphAtlas 래스터의 IFont 소스.
#pragma once

#include "mye/text/TextTypes.h"
#include "mye/text/FontFace.h"
#include "mye/text/TextLayout.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mye::render { class SpriteBatch; }
namespace mye::rhi { class ICommandContext; }

namespace mye::text {

class GlyphAtlas;

// 폰트 등록소. FontFace 소유 + FontId 발급 + 폴백 체인. IFontProvider 로 레이아웃에 주입.
class FontRegistry final : public IFontProvider {
public:
    FontRegistry() = default;
    ~FontRegistry() = default;
    FontRegistry(const FontRegistry&) = delete;
    FontRegistry& operator=(const FontRegistry&) = delete;

    // TTF/OTF blob 등록 → FontId 발급. debugName 은 로그·조회용(선택).
    Expected<FontId, Error> RegisterTtf(std::span<const std::byte> ttfBlob,
                                        std::string_view debugName = {}, int faceIndex = 0);

    // 폴백 체인 설정: primary 에 글리프 없으면 fallback 순차 조회(한글→CJK→이모지 등).
    void SetFallbackChain(FontId primary, std::span<const FontId> fallbacks);

    // 기본 폰트(FontId 미지정 스타일의 폴백). 첫 등록 폰트가 기본이 된다.
    FontId defaultFont() const { return m_default; }
    void   setDefaultFont(FontId id) { m_default = id; }

    IFont* ResolveFont(FontId id) const override;

    // codepoint 를 실제로 그릴 수 있는 폰트를 폴백 체인 포함으로 찾는다(GlyphAtlas 가 호출).
    IFont* ResolveForCodepoint(FontId primary, char32_t cp) const;

private:
    struct Registered {
        std::unique_ptr<FreeTypeFace> face;
        std::vector<FontId> fallbacks;
    };
    std::vector<Registered> m_fonts;   // index = FontId.v
    FontId m_default{};
};

// 텍스트 렌더러. 레이아웃 결과를 SpriteBatch 로 제출한다(스크린 레이어).
//   배처는 호출자가 Begin() 한 상태여야 하며, TextRenderer 는 Submit 만 한다(End 는 호출자).
class TextRenderer {
public:
    TextRenderer() = default;

    // layout 의 글리프들을 SpriteBatch 로 제출. origin = 레이아웃 로컬 원점의 스크린 좌표(픽셀).
    //   sortY: UI 레이어 정렬 키(같은 UI 요소는 동일값 권장). atlas 로 그림자/아웃라인 uv 재조회.
    //   글리프의 world 좌표는 origin + pos (정수 스냅). SpriteBatch 는 unit 좌표계이므로
    //   pixelsPerUnit=1 스크린 카메라(UiRenderer 가 셋업)를 전제로 픽셀=unit 로 제출한다.
    void Submit(render::SpriteBatch& batch, const TextLayout& layout, Vec2i origin,
                float sortY = 0.0f);

    // 단발 편의: 레이아웃을 즉석 생성해 바로 제출(캐시 불필요한 짧은 텍스트).
    void DrawText(rhi::ICommandContext& ctx, render::SpriteBatch& batch, GlyphAtlas& atlas,
                  const IFontProvider& fonts, std::string_view richText,
                  const TextStyle& style, Vec2i origin, const LayoutParams& params = {});

private:
    TextLayout m_scratch;   // DrawText 즉석 레이아웃
};

} // namespace mye::text
