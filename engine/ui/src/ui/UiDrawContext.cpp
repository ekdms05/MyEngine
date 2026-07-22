// mye/ui/UiDrawContext.cpp — 드로우 파사드 (배처 제출·9-slice·시저 CPU 클립)
//
// 시저: SpriteBatch 는 프레임당 End 1회 계약이라 렌더 패스 중간에 배처를 flush 하지 않는다.
//   대신 DrawContext 가 현재 시저 사각형으로 쿼드를 CPU 클리핑(교집합·UV 비례 재계산)해 제출한다.
//   창 내부 클립(타이틀바·리스트)에 충분하며 드로우콜 분할이 없다. 텍스트는 시저 밖 글리프를 스킵.
#include "mye/ui/UiDrawContext.h"

#include "mye/render/SpriteBatch.h"
#include "mye/rhi/Rhi.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/TextRenderer.h"
#include "mye/text/TextLayout.h"

#include <algorithm>
#include <cmath>

namespace mye::ui {

namespace {

// 두 사각형 교집합. 비면 w/h <= 0.
UiRect Intersect(const UiRect& a, const UiRect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    return UiRect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

} // namespace

UiRect UiDrawContext::CurrentClip() const {
    if (m_scissorStack.empty())
        return UiRect{0, 0, static_cast<float>(m_screen.x), static_cast<float>(m_screen.y)};
    return m_scissorStack.back();
}

// dst 사각형을 현재 클립으로 잘라 배처에 제출(uv 를 비례 재계산). 완전히 밖이면 미제출.
void UiDrawContext::DrawSprite(const UiRect& rect, const UiSprite& sprite, Color tint) {
    if (!sprite.texture.IsValid()) return;

    UiRect dst = rect;
    Rect   uv  = sprite.uv;
    const UiRect clip = CurrentClip();
    if (!m_scissorStack.empty()) {
        const UiRect c = Intersect(dst, clip);
        if (c.w <= 0.0f || c.h <= 0.0f) return;
        // uv 를 잘린 만큼 비례 이동/축소.
        if (dst.w > 0.0f && dst.h > 0.0f) {
            const float uL = (c.x - dst.x) / dst.w;
            const float uT = (c.y - dst.y) / dst.h;
            const float uR = (c.x + c.w - dst.x) / dst.w;
            const float uB = (c.y + c.h - dst.y) / dst.h;
            uv = Rect{uv.x + uv.w * uL, uv.y + uv.h * uT,
                      uv.w * (uR - uL), uv.h * (uB - uT)};
        }
        dst = c;
    }

    render::SpriteDraw d{};
    d.position = Vec2{dst.x, dst.y};
    d.size     = Vec2{dst.w, dst.h};
    d.uv       = uv;
    d.texture  = sprite.texture;
    d.tint     = tint;
    d.pivot    = Vec2{0.0f, 0.0f};   // UI 좌상단 원점
    m_batch.Submit(d);
}

void UiDrawContext::DrawRect(const UiRect& rect, Color color) {
    if (!m_whiteTex.IsValid()) return;
    UiSprite s{};
    s.texture = m_whiteTex;
    s.uv = Rect{0, 0, 1, 1};
    DrawSprite(rect, s, color);
}

// 9-slice: 3×3 패치. 모서리는 원본 픽셀 크기 고정, 에지는 한 축 스트레치, 중앙은 양축 스트레치.
//   integerScale: 모서리를 정수배(픽셀아트) — MVP 는 1배 고정(원본 픽셀). 소스 UV 는 nativeSize 기준.
void UiDrawContext::DrawNineSlice(const UiRect& rect, const UiSprite& sprite,
                                  const NineSlice& slice, Color tint) {
    if (!sprite.texture.IsValid()) return;
    if (!slice.Enabled() || sprite.nativeSize.x <= 0 || sprite.nativeSize.y <= 0) {
        DrawSprite(rect, sprite, tint);
        return;
    }

    const float texW = static_cast<float>(sprite.nativeSize.x);
    const float texH = static_cast<float>(sprite.nativeSize.y);

    const float l = static_cast<float>(slice.left);
    const float r = static_cast<float>(slice.right);
    const float t = static_cast<float>(slice.top);
    const float b = static_cast<float>(slice.bottom);

    // 목적지 열/행 경계(x0<x1<x2<x3, y0<y1<y2<y3). 중앙이 음수면 모서리 겹침 방지로 클램프.
    float x[4] = {rect.x, rect.x + l, rect.x + rect.w - r, rect.x + rect.w};
    float y[4] = {rect.y, rect.y + t, rect.y + rect.h - b, rect.y + rect.h};
    x[1] = std::min(x[1], x[3]); x[2] = std::max(x[1], x[2]);
    y[1] = std::min(y[1], y[3]); y[2] = std::max(y[1], y[2]);

    // 소스 UV 열/행(정규화). sprite.uv 서브렉트 내부 비율.
    const float su[4] = {0.0f, l / texW, 1.0f - r / texW, 1.0f};
    const float sv[4] = {0.0f, t / texH, 1.0f - b / texH, 1.0f};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float dx = x[col], dy = y[row];
            const float dw = x[col + 1] - x[col];
            const float dh = y[row + 1] - y[row];
            if (dw <= 0.0f || dh <= 0.0f) continue;

            UiSprite patch = sprite;
            patch.uv = Rect{
                sprite.uv.x + sprite.uv.w * su[col],
                sprite.uv.y + sprite.uv.h * sv[row],
                sprite.uv.w * (su[col + 1] - su[col]),
                sprite.uv.h * (sv[row + 1] - sv[row]),
            };
            DrawSprite(UiRect{dx, dy, dw, dh}, patch, tint);
        }
    }
}

void UiDrawContext::DrawText(const UiRect& rect, std::string_view richText,
                             const text::TextStyle& style, const text::LayoutParams& params) {
    m_text.DrawText(m_ctx, m_batch, m_atlas, m_fonts, richText, style,
                    Vec2i{static_cast<int32_t>(std::round(rect.x)),
                          static_cast<int32_t>(std::round(rect.y))}, params, TextClip());
}

void UiDrawContext::DrawTextLayout(const text::TextLayout& layout, Vec2 origin) {
    m_text.Submit(m_batch, layout,
                  Vec2i{static_cast<int32_t>(std::round(origin.x)),
                        static_cast<int32_t>(std::round(origin.y))},
                  0.0f, TextClip());
}

// 현재 시저 클립을 TextRenderer::ClipRect 로 변환(스택 비면 전체 화면).
text::TextRenderer::ClipRect UiDrawContext::TextClip() const {
    const UiRect c = CurrentClip();
    return text::TextRenderer::ClipRect{c.x, c.y, c.w, c.h};
}

void UiDrawContext::PushScissor(const UiRect& rect) {
    // 교집합 누적(중첩 창·리스트).
    UiRect r = rect;
    if (!m_scissorStack.empty()) r = Intersect(r, m_scissorStack.back());
    // 정수 픽셀·화면 클램프.
    const UiRect screen{0, 0, static_cast<float>(m_screen.x), static_cast<float>(m_screen.y)};
    r = Intersect(r, screen);
    r.x = std::round(r.x); r.y = std::round(r.y);
    r.w = std::round(r.w); r.h = std::round(r.h);
    m_scissorStack.push_back(r);
}

void UiDrawContext::PopScissor() {
    if (!m_scissorStack.empty()) m_scissorStack.pop_back();
}

void UiDrawContext::ApplyScissor(const UiRect&) {
    // CPU 클립 방식이라 GPU 시저는 사용하지 않음(배처 프레임 계약 보존). 예약.
}

} // namespace mye::ui
