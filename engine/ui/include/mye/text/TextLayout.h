// mye/text/TextLayout.h — 리치텍스트 → 셰이핑 → 줄바꿈 → PositionedGlyph[] (docs/06 §2 레이아웃 엔진)
//
// 소유: 06 (M5-A). RichTextParser 로 런을 얻어 단순 advance 셰이핑(HarfBuzz 미도입 — 한글/라틴
//   advance 로 충분), 한글 char-wrap + 라틴 word-wrap 혼합 줄바꿈(금칙 처리), 정렬을 수행하고
//   결과 PositionedGlyph[]·LinkRegion[]·InlineIcon[] 를 캐시한다. 결과는 레이아웃 로컬 픽셀 좌표.
//
// 금칙(kinsoku): 행두 금칙(닫는 괄호·구두점 `.,!?」)』` 등 줄 첫머리 불가),
//   행말 금칙(여는 괄호 줄 끝 불가). 간단한 문자표로 처리(docs/06 §2).
//
// 아틀라스 결합: 레이아웃은 GlyphAtlas 를 통해 각 글리프의 uv·size 를 얻는다. 따라서 Set 은
//   활성 프레임의 커맨드 컨텍스트를 요구한다(미스 글리프 즉시 래스터·업로드). measure 만 필요하면
//   MeasureOnly 경로(advance/metrics 만, 아틀라스 업로드 없이)를 쓴다.
#pragma once

#include "mye/text/TextTypes.h"
#include "mye/text/RichText.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::rhi { class ICommandContext; }

namespace mye::text {

class GlyphAtlas;
class IFontProvider;   // FontId → IFont* 해석 (아래)

// FontId 로 IFont 를 조회하는 인터페이스. FontRegistry(TextRenderer 소유)가 구현.
class IFontProvider {
public:
    virtual ~IFontProvider() = default;
    // FontId → IFont*(FontFace.h). 미등록·폴백 실패 시 nullptr.
    virtual class IFont* ResolveFont(FontId id) const = 0;
};

class TextLayout {
public:
    TextLayout() = default;

    // 리치 텍스트를 배치한다. atlas 미스 글리프는 ctx 로 즉시 래스터·업로드된다.
    //   base: 태그 없는 텍스트의 기본 스타일. params: 폭·정렬·줄바꿈·줄간격.
    void Set(rhi::ICommandContext& ctx, GlyphAtlas& atlas, const IFontProvider& fonts,
             std::string_view richText, const TextStyle& base, const LayoutParams& params);

    // 측정 전용(아틀라스 업로드 없음): 줄바꿈·크기만 계산. UI measure 패스용.
    //   글리프 uv/atlasTexture 는 채워지지 않으므로 렌더에 쓰지 말 것.
    Vec2i Measure(const IFontProvider& fonts, std::string_view richText,
                  const TextStyle& base, const LayoutParams& params);

    std::span<const PositionedGlyph> glyphs() const { return m_glyphs; }
    std::span<const LinkRegion>      links()  const { return m_links; }
    std::span<const InlineIcon>      icons()  const { return m_icons; }
    Vec2i measuredSize() const { return m_size; }
    uint16_t lineCount() const { return m_lineCount; }

    // 링크 페이로드 문자열 조회(LinkRegion.payloadOffset/Length → string_view).
    std::string_view linkPayload(const LinkRegion& r) const;
    std::string_view iconName(const InlineIcon& i) const;

    void Clear();

private:
    std::vector<PositionedGlyph> m_glyphs;
    std::vector<LinkRegion>      m_links;
    std::vector<InlineIcon>      m_icons;
    std::string                  m_stringPool;   // 링크 페이로드·아이콘 이름 저장
    Vec2i    m_size{};
    uint16_t m_lineCount = 0;
};

// 금칙 판정 헬퍼(공개 — 테스트·채팅에서 재사용).
bool IsLineStartForbidden(char32_t cp);   // 행두 금칙(줄 첫머리 불가)
bool IsLineEndForbidden(char32_t cp);     // 행말 금칙(줄 끝 불가)
// 한글 음절(U+AC00..U+D7A3) 또는 CJK 여부(char-wrap 대상 판정).
bool IsCjkBreakable(char32_t cp);

} // namespace mye::text
