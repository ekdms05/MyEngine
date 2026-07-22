// mye/text/BitmapFont.cpp — 비트맵 도트 폰트(BMFont .fnt 텍스트) (M5-A)
//
// BMFont 텍스트 포맷(.fnt)을 파싱해 글리프 레코드·메트릭을 구성하고, R8 페이지 시트에서
//   서브렉트를 잘라 R8 로 반환한다(FT 힌팅 불필요 — 시트에 이미 구워짐). IFont 로 노출되어
//   FreeTypeFace 와 상호 교체 가능. 비트맵 폰트는 자유 스케일 안 함(정수배만 허용).
#include "mye/text/BitmapFont.h"

#include <charconv>
#include <string_view>

namespace mye::text {
namespace {

// "key=value" 토큰에서 정수 value 추출. 없으면 def.
int IntAttr(std::string_view line, std::string_view key, int def = 0) {
    // key= 뒤의 정수(부호 허용). 공백/줄끝까지.
    std::string_view pat = key;
    size_t pos = 0;
    while ((pos = line.find(pat, pos)) != std::string_view::npos) {
        // key 앞이 단어 경계인지 확인(예: "x=" 가 "offsetx=" 매칭 방지).
        const bool boundary = (pos == 0) || line[pos - 1] == ' ' || line[pos - 1] == '\t';
        const size_t after = pos + pat.size();
        if (boundary && after < line.size() && line[after] == '=') {
            size_t v = after + 1;
            // value 파싱.
            size_t e = v;
            if (e < line.size() && (line[e] == '-' || line[e] == '+')) ++e;
            while (e < line.size() && line[e] >= '0' && line[e] <= '9') ++e;
            int out = def;
            std::from_chars(line.data() + v, line.data() + e, out);
            return out;
        }
        pos = after;
    }
    return def;
}

bool LineStartsWith(std::string_view line, std::string_view tag) {
    return line.substr(0, tag.size()) == tag &&
           (line.size() == tag.size() || line[tag.size()] == ' ' || line[tag.size()] == '\t');
}

} // namespace

Expected<std::unique_ptr<BitmapFont>, Error>
BitmapFont::CreateFromBmfont(FontId id, std::string_view fntText,
                             std::span<const std::byte> pageR8, Vec2i pageSize) {
    if (pageSize.x <= 0 || pageSize.y <= 0)
        return Error{"BitmapFont::CreateFromBmfont: invalid page size", 1};
    if (pageR8.size() < static_cast<size_t>(pageSize.x) * pageSize.y)
        return Error{"BitmapFont::CreateFromBmfont: page pixels too small", 2};

    auto bf = std::make_unique<BitmapFont>();
    bf->m_id = id;
    bf->m_pageSize = pageSize;
    bf->m_pageR8.resize(pageR8.size());
    for (size_t i = 0; i < pageR8.size(); ++i)
        bf->m_pageR8[i] = static_cast<uint8_t>(pageR8[i]);

    int lineHeight = 0, baseAscent = 0;

    // 줄 단위 파싱.
    size_t i = 0;
    const size_t n = fntText.size();
    while (i < n) {
        size_t e = fntText.find('\n', i);
        if (e == std::string_view::npos) e = n;
        std::string_view line = fntText.substr(i, e - i);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        i = e + 1;

        if (LineStartsWith(line, "common")) {
            lineHeight = IntAttr(line, "lineHeight", 0);
            baseAscent = IntAttr(line, "base", 0);
        } else if (LineStartsWith(line, "info")) {
            bf->m_nativePixelSize = static_cast<uint16_t>(IntAttr(line, "size", 0));
        } else if (LineStartsWith(line, "char") && !LineStartsWith(line, "chars")) {
            BitmapGlyphRecord rec{};
            const int cpv = IntAttr(line, "id", -1);
            if (cpv < 0) continue;
            rec.sheetPos = Vec2i{IntAttr(line, "x"), IntAttr(line, "y")};
            rec.size = Vec2i{IntAttr(line, "width"), IntAttr(line, "height")};
            const int xoff = IntAttr(line, "xoffset");
            const int yoff = IntAttr(line, "yoffset");
            // BMFont yoffset 은 셀 상단에서의 하강 → bearingY = base - yoffset.
            rec.bearing = Vec2i{xoff, baseAscent - yoff};
            rec.advance = static_cast<int16_t>(IntAttr(line, "xadvance"));
            bf->m_glyphs.emplace(static_cast<char32_t>(cpv), rec);
        }
    }

    if (bf->m_nativePixelSize == 0)
        bf->m_nativePixelSize = static_cast<uint16_t>(lineHeight > 0 ? lineHeight : pageSize.y);

    bf->m_metrics.ascent = static_cast<int16_t>(baseAscent);
    bf->m_metrics.descent = static_cast<int16_t>(lineHeight - baseAscent > 0 ? lineHeight - baseAscent : 0);
    bf->m_metrics.lineGap = 0;
    bf->m_metrics.lineHeight = static_cast<int16_t>(lineHeight > 0 ? lineHeight
                                                                   : bf->m_metrics.ascent + bf->m_metrics.descent);
    return bf;
}

FontMetrics BitmapFont::metrics(uint16_t pixelSize) const {
    // 정수배 스케일만: 요청 크기가 native 의 정수배면 메트릭도 그 배율.
    if (m_nativePixelSize == 0 || pixelSize == 0 || pixelSize == m_nativePixelSize)
        return m_metrics;
    if (pixelSize % m_nativePixelSize == 0) {
        const int16_t s = static_cast<int16_t>(pixelSize / m_nativePixelSize);
        FontMetrics m{};
        m.ascent = static_cast<int16_t>(m_metrics.ascent * s);
        m.descent = static_cast<int16_t>(m_metrics.descent * s);
        m.lineGap = static_cast<int16_t>(m_metrics.lineGap * s);
        m.lineHeight = static_cast<int16_t>(m_metrics.lineHeight * s);
        return m;
    }
    return m_metrics;   // 비정수배 요청 → native 유지(자유 스케일 안 함)
}

bool BitmapFont::hasGlyph(char32_t cp) const {
    return m_glyphs.find(cp) != m_glyphs.end();
}

RasterBitmap BitmapFont::rasterize(char32_t cp, uint16_t /*pixelSize*/,
                                   GlyphStyle /*style*/, HintMode /*hint*/) {
    RasterBitmap rb{};
    auto it = m_glyphs.find(cp);
    if (it == m_glyphs.end()) return rb;
    const BitmapGlyphRecord& r = it->second;
    rb.advance = r.advance;
    rb.bearing = r.bearing;

    const uint32_t w = static_cast<uint32_t>(r.size.x > 0 ? r.size.x : 0);
    const uint32_t h = static_cast<uint32_t>(r.size.y > 0 ? r.size.y : 0);
    if (w == 0 || h == 0) { rb.valid = true; return rb; }   // 공백류

    // 시트에서 서브렉트를 m_scratch 로 크롭(R8).
    m_scratch.assign(static_cast<size_t>(w) * h, 0);
    const int32_t pw = m_pageSize.x;
    for (uint32_t y = 0; y < h; ++y) {
        const int32_t sy = r.sheetPos.y + static_cast<int32_t>(y);
        if (sy < 0 || sy >= m_pageSize.y) continue;
        for (uint32_t x = 0; x < w; ++x) {
            const int32_t sx = r.sheetPos.x + static_cast<int32_t>(x);
            if (sx < 0 || sx >= pw) continue;
            m_scratch[y * w + x] = m_pageR8[static_cast<size_t>(sy) * pw + sx];
        }
    }
    rb.pixels = m_scratch.data();
    rb.width = w;
    rb.height = h;
    rb.rowPitch = w;
    rb.valid = true;
    return rb;
}

} // namespace mye::text
