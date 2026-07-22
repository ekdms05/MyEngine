// mye/text/TextLayout.cpp — 리치텍스트 → 셰이핑 → 줄바꿈 → PositionedGlyph[] (M5-A)
//
// RichTextParser 로 런을 얻어 UTF-8 디코드 → 단순 advance 셰이핑 → 한글 char-wrap + 라틴
//   word-wrap 혼합 줄바꿈(금칙 처리) → 정렬. 결과는 레이아웃 로컬 픽셀 좌표.
//   Set 은 아틀라스 미스 글리프를 ctx 로 즉시 래스터·업로드한다. Measure 는 advance 만.
#include "mye/text/TextLayout.h"
#include "mye/text/FontFace.h"
#include "mye/text/GlyphAtlas.h"

#include <vector>

namespace mye::text {

// --- 금칙·CJK 판정 ---
bool IsLineStartForbidden(char32_t cp) {
    switch (cp) {
        case U'.': case U',': case U'!': case U'?': case U':': case U';':
        case U')': case U']': case U'}': case U'>':
        case U'」': case U'』': case U'）': case U'］': case U'｝':
        case U'。': case U'，': case U'、': case U'．': case U'？': case U'！':
        case U'·': case U'…': case U'‥': case U'～':
            return true;
        default: return false;
    }
}

bool IsLineEndForbidden(char32_t cp) {
    switch (cp) {
        case U'(': case U'[': case U'{': case U'<':
        case U'「': case U'『': case U'（': case U'［': case U'｛':
        case U'‘': case U'“':
            return true;
        default: return false;
    }
}

bool IsCjkBreakable(char32_t cp) {
    return (cp >= 0xAC00 && cp <= 0xD7A3) ||   // 한글 완성형
           (cp >= 0x1100 && cp <= 0x11FF) ||   // 한글 자모
           (cp >= 0x3040 && cp <= 0x30FF) ||   // 가나
           (cp >= 0x3130 && cp <= 0x318F) ||   // 호환 자모
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK 한자
           (cp >= 0xFF00 && cp <= 0xFFEF);      // 전각
}

namespace {

// UTF-8 한 코드포인트 디코드. 반환: 소비한 바이트 수(잘못된 시퀀스면 1, cp=U+FFFD).
size_t DecodeUtf8(std::string_view s, size_t i, char32_t& cp) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) { cp = c0; return 1; }
    auto cont = [&](size_t k) -> bool {
        return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    if ((c0 & 0xE0) == 0xC0 && cont(1)) {
        cp = ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        cp = ((c0 & 0x0Fu) << 12) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        cp = ((c0 & 0x07u) << 18) |
             ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
        return 4;
    }
    cp = 0xFFFD;
    return 1;
}

inline bool IsSpace(char32_t cp) { return cp == U' ' || cp == U'\t'; }

// 셰이핑 단계에서 만드는 중간 아이템(문자 하나 또는 링크/아이콘 경계).
struct ShapeItem {
    enum class Kind : uint8_t { Glyph, LinkBegin, LinkEnd, Icon } kind = Kind::Glyph;
    char32_t codepoint = 0;
    int32_t  advance = 0;      // 이 아이템의 수평 진행(글리프 advance 또는 아이콘 폭)
    // 글리프 메타(measure 단계에서 채움; Set 에서 아틀라스 조회로 uv 보강).
    Vec2i    size{};
    Vec2i    bearing{};
    Rect     uv{};
    rhi::TextureHandle atlasTexture{};
    Color    color = Color::White();
    uint16_t pixelSize = 0;
    FontId   fontId{};   // 이 글리프를 실제로 그리는 폰트(폴백 해석 결과 — 아틀라스 키 일치용)
    // 문자열 풀 참조(링크/아이콘).
    uint32_t poolOffset = 0, poolLength = 0;
    bool     breakable = false;   // 이 문자 앞에서 줄바꿈 가능(CJK 또는 공백 뒤)
    bool     isSpace = false;
};

} // namespace

// 공통 코어: 셰이핑 + 줄바꿈 + 정렬. atlas!=null 이면 미스 글리프 업로드(Set), null 이면 measure.
static void LayoutCore(std::vector<PositionedGlyph>& outGlyphs,
                       std::vector<LinkRegion>& outLinks,
                       std::vector<InlineIcon>& outIcons,
                       std::string& stringPool,
                       Vec2i& outSize, uint16_t& outLineCount,
                       rhi::ICommandContext* ctx, GlyphAtlas* atlas,
                       const IFontProvider& fonts,
                       std::string_view richText, const TextStyle& base,
                       const LayoutParams& params) {
    outGlyphs.clear();
    outLinks.clear();
    outIcons.clear();
    stringPool.clear();
    outSize = {};
    outLineCount = 0;

    IFont* baseFont = fonts.ResolveFont(base.font);
    FontMetrics fm = baseFont ? baseFont->metrics(base.size) : FontMetrics{};
    if (fm.lineHeight == 0) fm.lineHeight = static_cast<int16_t>(base.size);
    const int32_t lineAdvance = fm.lineHeight + params.lineSpacing;

    RichTextParse parsed = RichTextParser::Parse(richText, base);

    // 1) 셰이핑: 런 순회 → 문자별 ShapeItem. 아틀라스 조회는 여기서(Set) 또는 스킵(Measure).
    std::vector<ShapeItem> items;
    items.reserve(richText.size());

    const int32_t spaceAdvance = base.size / 3 + 1;   // 폰트 없을 때 폴백 공백폭

    for (const TextRun& run : parsed.runs) {
        if (run.kind == RunKind::LinkBegin) {
            ShapeItem it;
            it.kind = ShapeItem::Kind::LinkBegin;
            it.poolOffset = static_cast<uint32_t>(stringPool.size());
            stringPool.append(run.linkPayload);
            it.poolLength = static_cast<uint32_t>(run.linkPayload.size());
            items.push_back(it);
            continue;
        }
        if (run.kind == RunKind::LinkEnd) {
            ShapeItem it;
            it.kind = ShapeItem::Kind::LinkEnd;
            items.push_back(it);
            continue;
        }
        if (run.kind == RunKind::Icon) {
            ShapeItem it;
            it.kind = ShapeItem::Kind::Icon;
            it.poolOffset = static_cast<uint32_t>(stringPool.size());
            stringPool.append(run.iconName);
            it.poolLength = static_cast<uint32_t>(run.iconName.size());
            it.size = Vec2i{base.size, base.size};   // 아이콘 자리 = 1em 정사각(해석은 상위)
            it.advance = base.size;
            it.breakable = true;
            items.push_back(it);
            continue;
        }
        // Text 런: UTF-8 디코드.
        std::string_view t = run.utf8Text;
        size_t i = 0;
        while (i < t.size()) {
            char32_t cp = 0;
            i += DecodeUtf8(t, i, cp);
            if (cp == U'\r') continue;

            ShapeItem it;
            it.kind = ShapeItem::Kind::Glyph;
            it.codepoint = cp;
            it.color = run.color;
            it.pixelSize = base.size;

            if (cp == U'\n') {
                it.advance = 0;
                items.push_back(it);   // 강제 줄바꿈 마커
                continue;
            }
            if (cp == U'\t') {
                it.advance = spaceAdvance * params.tabWidth;
                it.isSpace = true;
                it.breakable = true;
                items.push_back(it);
                continue;
            }

            // 폰트 선택: 코드포인트별 폴백 해석(base.font 에 없는 한글 등은 등록 폴백 폰트로).
            //   선택 결과 폰트 id 를 아이템에 저장해 아틀라스 조회 키가 셰이핑과 일치하게 한다.
            IFont* font = fonts.ResolveForCodepoint(base.font, cp);
            if (!font) font = baseFont;
            it.fontId = font ? font->id() : base.font;
            if (font) {
                RasterBitmap probe{};
                // measure/셰이핑 단계에서는 metrics·advance 만 필요 → rasterize 로 advance 취득.
                // Set 경로에서는 아틀라스가 다시 rasterize(캐시)하지만, advance/size 는 여기서 확정.
                probe = font->rasterize(cp, base.size, GlyphStyle::Normal, base.hint);
                if (probe.valid) {
                    it.advance = probe.advance;
                    it.size = Vec2i{static_cast<int32_t>(probe.width),
                                    static_cast<int32_t>(probe.height)};
                    it.bearing = probe.bearing;
                } else {
                    it.advance = spaceAdvance;   // 누락 글리프 → 공백폭
                }
            } else {
                it.advance = (cp == U' ') ? spaceAdvance : base.size / 2;
            }

            it.isSpace = IsSpace(cp);
            it.breakable = it.isSpace || IsCjkBreakable(cp);
            items.push_back(it);
        }
    }

    // 2) 아틀라스 조회(Set 경로): 각 글리프 uv/atlasTexture 채우기.
    if (atlas && ctx) {
        for (ShapeItem& it : items) {
            if (it.kind != ShapeItem::Kind::Glyph) continue;
            if (it.codepoint == U'\n' || it.isSpace || it.size.x == 0 || it.size.y == 0)
                continue;
            // 셰이핑에서 확정한 폰트(폴백 포함)로 조회 — base.font 로 재해석하면 폴백 글리프가
            //   base 폰트 아틀라스 키로 어긋나 빈칸으로 렌더된다(한글 in 라틴 폰트 버그).
            IFont* font = fonts.ResolveFont(it.fontId);
            if (!font) continue;
            GlyphKey key{font->id(), it.codepoint, base.size, GlyphStyle::Normal};
            const Glyph* g = atlas->GetOrRasterize(*ctx, *font, key);
            if (g && g->valid) {
                it.uv = g->uv;
                it.atlasTexture = g->atlasTexture;
                it.size = g->size;
                it.bearing = g->bearing;
                it.advance = g->advance;
            }
        }
    }

    // 3) 줄바꿈 + 배치.
    const float maxW = params.maxWidth;
    const bool wrap = (params.wrap != WrapMode::None) && maxW > 0.0f;

    // 각 줄에 담긴 (item index) 목록으로 배치.
    struct LineOut { size_t begin; size_t end; int32_t width; };
    std::vector<LineOut> lines;

    size_t lineBegin = 0;
    int32_t penX = 0;
    int32_t lineWidth = 0;
    // word-wrap 을 위해 마지막 "줄바꿈 가능 위치"(공백 뒤 or CJK 앞)를 추적.
    size_t lastBreak = std::string::npos;
    int32_t lastBreakWidth = 0;

    auto commitLine = [&](size_t end, int32_t width) {
        lines.push_back(LineOut{lineBegin, end, width});
    };

    for (size_t idx = 0; idx < items.size(); ++idx) {
        ShapeItem& it = items[idx];

        // 링크/아이콘 경계는 advance 처리(아이콘은 폭 있음).
        if (it.kind == ShapeItem::Kind::LinkBegin || it.kind == ShapeItem::Kind::LinkEnd) {
            continue;   // 배치 좌표는 4단계에서 별도 계산
        }

        // 강제 줄바꿈.
        if (it.kind == ShapeItem::Kind::Glyph && it.codepoint == U'\n') {
            commitLine(idx, lineWidth);
            lineBegin = idx + 1;
            penX = 0; lineWidth = 0;
            lastBreak = std::string::npos; lastBreakWidth = 0;
            continue;
        }

        const int32_t adv = it.advance;

        // 줄바꿈 판정: 현재 아이템을 놓으면 폭 초과?
        if (wrap && penX > 0 && penX + adv > static_cast<int32_t>(maxW) && !it.isSpace) {
            size_t breakAt;
            int32_t committedWidth;
            const bool cjkHere = IsCjkBreakable(it.codepoint) ||
                                 it.kind == ShapeItem::Kind::Icon;

            // 행두 금칙: 이 문자가 줄 첫머리에 못 오면 앞 문자와 함께 이전 기회로.
            if ((params.wrap == WrapMode::Char || cjkHere) &&
                !IsLineStartForbidden(it.codepoint)) {
                // char-wrap: 여기(현재 문자 앞)에서 끊는다.
                breakAt = idx;
                committedWidth = lineWidth;
            } else if (lastBreak != std::string::npos) {
                // word-wrap: 마지막 공백/브레이크 위치에서 끊는다.
                breakAt = lastBreak;
                committedWidth = lastBreakWidth;
            } else {
                // 브레이크 기회 없음 → 강제로 현재 문자 앞에서 끊음(오버플로우 방지).
                breakAt = idx;
                committedWidth = lineWidth;
            }

            commitLine(breakAt, committedWidth);
            lineBegin = breakAt;
            // 새 줄에서 penX/width 재계산: breakAt..idx 사이 선행 공백은 스킵.
            penX = 0; lineWidth = 0;
            lastBreak = std::string::npos; lastBreakWidth = 0;
            // breakAt 이후 선행 공백 제거(줄 시작 공백 트림).
            size_t s = breakAt;
            while (s < idx && items[s].isSpace) { ++s; }
            lineBegin = s;
            // s..idx-1 재적산(끊긴 뒤 새 줄에 남은 선행 글리프들의 폭 누적).
            for (size_t k = s; k < idx; ++k) {
                if (items[k].kind == ShapeItem::Kind::Glyph ||
                    items[k].kind == ShapeItem::Kind::Icon) {
                    penX += items[k].advance;
                    lineWidth = penX;
                }
            }
        }

        // 브레이크 후보 갱신: 공백 뒤 또는 CJK 문자 앞.
        if (it.isSpace) {
            // 공백 자체는 다음 문자가 브레이크 지점.
            penX += adv;
            lineWidth = penX;   // 줄 끝 공백은 정렬 시 트림 가능하나 MVP 는 포함
            lastBreak = idx + 1;
            lastBreakWidth = penX;
            continue;
        }
        if (IsCjkBreakable(it.codepoint) && !IsLineEndForbidden(it.codepoint)) {
            lastBreak = idx;
            lastBreakWidth = penX;
        }

        penX += adv;
        lineWidth = penX;
    }
    commitLine(items.size(), lineWidth);

    // 4) 각 줄을 순회하며 PositionedGlyph 배치 + 정렬 + 링크/아이콘 히트렉트.
    int32_t maxLineWidth = 0;
    for (const LineOut& ln : lines)
        if (ln.width > maxLineWidth) maxLineWidth = ln.width;
    const int32_t alignRef = (maxW > 0.0f) ? static_cast<int32_t>(maxW) : maxLineWidth;

    int32_t cursorY = 0;
    uint16_t lineIndex = 0;
    const int32_t baseline = fm.ascent;

    // 링크 경계 추적(줄을 넘나들 수 있음 → 줄별 히트렉트 분할).
    bool inLink = false;
    uint32_t linkPoolOff = 0, linkPoolLen = 0;
    int32_t linkX0 = 0, linkY0 = 0, linkX1 = 0;

    for (const LineOut& ln : lines) {
        // 정렬 오프셋.
        int32_t alignX = 0;
        if (params.align == TextAlign::Center) alignX = (alignRef - ln.width) / 2;
        else if (params.align == TextAlign::Right) alignX = (alignRef - ln.width);
        if (alignX < 0) alignX = 0;

        int32_t x = alignX;
        for (size_t idx = ln.begin; idx < ln.end; ++idx) {
            const ShapeItem& it = items[idx];
            if (it.kind == ShapeItem::Kind::LinkBegin) {
                inLink = true;
                linkPoolOff = it.poolOffset; linkPoolLen = it.poolLength;
                linkX0 = x; linkY0 = cursorY; linkX1 = x;
                continue;
            }
            if (it.kind == ShapeItem::Kind::LinkEnd) {
                if (inLink) {
                    LinkRegion lr;
                    lr.rect = Rect{static_cast<float>(linkX0), static_cast<float>(linkY0),
                                   static_cast<float>(linkX1 - linkX0),
                                   static_cast<float>(lineAdvance)};
                    lr.payloadOffset = linkPoolOff;
                    lr.payloadLength = linkPoolLen;
                    outLinks.push_back(lr);
                    inLink = false;
                }
                continue;
            }
            if (it.kind == ShapeItem::Kind::Icon) {
                InlineIcon ic;
                ic.pos = Vec2i{x, cursorY + (baseline - it.size.y)};
                if (ic.pos.y < cursorY) ic.pos.y = cursorY;
                ic.size = it.size;
                ic.nameOffset = it.poolOffset;
                ic.nameLength = it.poolLength;
                outIcons.push_back(ic);
                x += it.advance;
                linkX1 = x;
                continue;
            }
            // Glyph.
            if (it.codepoint == U'\n') continue;
            if (!it.isSpace && it.size.x > 0 && it.size.y > 0) {
                PositionedGlyph pg;
                pg.codepoint = it.codepoint;
                // 좌상단 = penX + bearingX, baseline - bearingY.
                pg.pos = Vec2i{x + it.bearing.x, cursorY + (baseline - it.bearing.y)};
                pg.size = it.size;
                pg.uv = it.uv;
                pg.atlasTexture = it.atlasTexture;
                pg.color = it.color;
                pg.pixelSize = it.pixelSize;
                pg.style = GlyphStyle::Normal;
                pg.lineIndex = lineIndex;
                outGlyphs.push_back(pg);
            }
            x += it.advance;
            linkX1 = x;
        }
        // 줄 끝에서 링크가 열려 있으면 이 줄분 히트렉트 커밋(다음 줄 계속).
        if (inLink) {
            LinkRegion lr;
            lr.rect = Rect{static_cast<float>(linkX0), static_cast<float>(linkY0),
                           static_cast<float>(linkX1 - linkX0),
                           static_cast<float>(lineAdvance)};
            lr.payloadOffset = linkPoolOff;
            lr.payloadLength = linkPoolLen;
            outLinks.push_back(lr);
            // 다음 줄 시작에서 재개(x 는 다음 줄 정렬로 리셋됨).
            linkX0 = 0; linkY0 = cursorY + lineAdvance; linkX1 = 0;
        }
        cursorY += lineAdvance;
        ++lineIndex;
    }

    outLineCount = lineIndex;
    outSize = Vec2i{maxLineWidth, cursorY};
}

void TextLayout::Set(rhi::ICommandContext& ctx, GlyphAtlas& atlas,
                     const IFontProvider& fonts, std::string_view richText,
                     const TextStyle& base, const LayoutParams& params) {
    LayoutCore(m_glyphs, m_links, m_icons, m_stringPool, m_size, m_lineCount,
               &ctx, &atlas, fonts, richText, base, params);
}

Vec2i TextLayout::Measure(const IFontProvider& fonts, std::string_view richText,
                          const TextStyle& base, const LayoutParams& params) {
    std::vector<PositionedGlyph> g;
    std::vector<LinkRegion> l;
    std::vector<InlineIcon> ic;
    std::string pool;
    Vec2i size{};
    uint16_t lc = 0;
    LayoutCore(g, l, ic, pool, size, lc, nullptr, nullptr, fonts, richText, base, params);
    // Measure 는 결과를 저장하지 않고 크기만 반환(계약: measure 전용, 렌더 데이터 미보강).
    m_size = size;
    m_lineCount = lc;
    return size;
}

std::string_view TextLayout::linkPayload(const LinkRegion& r) const {
    if (r.payloadOffset + r.payloadLength > m_stringPool.size()) return {};
    return std::string_view(m_stringPool).substr(r.payloadOffset, r.payloadLength);
}

std::string_view TextLayout::iconName(const InlineIcon& i) const {
    if (i.nameOffset + i.nameLength > m_stringPool.size()) return {};
    return std::string_view(m_stringPool).substr(i.nameOffset, i.nameLength);
}

void TextLayout::Clear() {
    m_glyphs.clear();
    m_links.clear();
    m_icons.clear();
    m_stringPool.clear();
    m_size = {};
    m_lineCount = 0;
}

} // namespace mye::text
