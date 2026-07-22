// mye/text/TextRenderer.cpp — 글리프 쿼드 배치 + FontRegistry (M5-A 스텁)
#include "mye/text/TextRenderer.h"
#include "mye/text/GlyphAtlas.h"

#include "mye/render/SpriteBatch.h"

namespace mye::text {

// ---------------------------------------------------------------------------
// FontRegistry
// ---------------------------------------------------------------------------
Expected<FontId, Error> FontRegistry::RegisterTtf(std::span<const std::byte> ttfBlob,
                                                  std::string_view /*debugName*/, int faceIndex) {
    const FontId id{static_cast<uint32_t>(m_fonts.size())};
    auto faceRes = FreeTypeFace::Create(id, ttfBlob, faceIndex);
    if (!faceRes) return faceRes.GetError();
    Registered reg;
    reg.face = std::move(faceRes).Value();
    m_fonts.push_back(std::move(reg));
    if (!m_default.IsValid()) m_default = id;
    return id;
}

void FontRegistry::SetFallbackChain(FontId primary, std::span<const FontId> fallbacks) {
    if (primary.v >= m_fonts.size()) return;
    m_fonts[primary.v].fallbacks.assign(fallbacks.begin(), fallbacks.end());
}

IFont* FontRegistry::ResolveFont(FontId id) const {
    if (!id.IsValid()) id = m_default;
    if (id.v >= m_fonts.size()) return nullptr;
    return m_fonts[id.v].face.get();
}

IFont* FontRegistry::ResolveForCodepoint(FontId primary, char32_t cp) const {
    if (!primary.IsValid()) primary = m_default;
    if (primary.v >= m_fonts.size()) return nullptr;
    const Registered& reg = m_fonts[primary.v];
    if (reg.face && reg.face->hasGlyph(cp)) return reg.face.get();
    for (FontId fb : reg.fallbacks) {
        if (fb.v < m_fonts.size() && m_fonts[fb.v].face &&
            m_fonts[fb.v].face->hasGlyph(cp))
            return m_fonts[fb.v].face.get();
    }
    return reg.face.get();   // 폴백 실패 시 primary(누락 글리프 → .notdef)
}

// ---------------------------------------------------------------------------
// TextRenderer
// ---------------------------------------------------------------------------
namespace {

// 본체 글리프 하나를 SpriteBatch 로 제출(정수 픽셀 스냅). pivot=좌상단(UI 픽셀 좌표).
void SubmitGlyphQuad(render::SpriteBatch& batch, const PositionedGlyph& g,
                     Vec2i origin, Vec2i offset, Color tint, float sortY) {
    render::SpriteDraw d{};
    d.position = Vec2{static_cast<float>(origin.x + g.pos.x + offset.x),
                      static_cast<float>(origin.y + g.pos.y + offset.y)};
    d.size    = Vec2{static_cast<float>(g.size.x), static_cast<float>(g.size.y)};
    d.uv      = g.uv;
    d.texture = g.atlasTexture;
    d.tint    = tint;
    d.sortY   = sortY;
    d.pivot   = Vec2{0.0f, 0.0f};
    batch.Submit(d);
}

} // namespace

void TextRenderer::Submit(render::SpriteBatch& batch, const TextLayout& layout,
                          Vec2i origin, float sortY) {
    // 본체 패스. 그림자/아웃라인은 스타일 정보를 요구하므로 DrawText(스타일 보유) 경로가
    //   전담한다(3패스). 캐시된 레이아웃만 가진 Submit 은 본체만 픽셀 스냅으로 제출한다.
    for (const PositionedGlyph& g : layout.glyphs()) {
        if (!g.atlasTexture.IsValid() || g.size.x == 0 || g.size.y == 0) continue;
        SubmitGlyphQuad(batch, g, origin, Vec2i{0, 0}, g.color, sortY);
    }
}

void TextRenderer::DrawText(rhi::ICommandContext& ctx, render::SpriteBatch& batch,
                            GlyphAtlas& atlas, const IFontProvider& fonts,
                            std::string_view richText, const TextStyle& style,
                            Vec2i origin, const LayoutParams& params) {
    m_scratch.Set(ctx, atlas, fonts, richText, style, params);

    // 3패스: 그림자 → (아웃라인은 별도 글리프 변형 필요 — 후속) → 본체.
    //   그림자는 본체 글리프를 오프셋+색으로 한 번 더 그린다(docs/06 §2, 픽셀 정확).
    if (style.shadow) {
        const ShadowDesc& sh = *style.shadow;
        for (const PositionedGlyph& g : m_scratch.glyphs()) {
            if (!g.atlasTexture.IsValid() || g.size.x == 0 || g.size.y == 0) continue;
            SubmitGlyphQuad(batch, g, origin, sh.offset, sh.color, 0.0f);
        }
    }
    Submit(batch, m_scratch, origin);
}

} // namespace mye::text
