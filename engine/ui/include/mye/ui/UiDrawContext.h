// mye/ui/UiDrawContext.h — 위젯 draw() 가 받는 드로우 컨텍스트 (docs/06 §1 렌더링)
//
// 소유: 06 (M5-A). UI 는 02 SpriteBatch 를 스크린 스페이스 레이어에서 사용한다. UI 전용 렌더
//   패스를 갖지 않는다. 클리핑(ScrollView)은 배처/커맨드컨텍스트의 시저 렉트로 처리(02 요구사항).
//   정수 픽셀 스냅은 이 컨텍스트의 헬퍼가 강제한다.
//
// draw() 는 이 컨텍스트로 스프라이트·9-slice·텍스트를 제출한다. 실제 배처 Begin/End·시저 스택
//   관리는 UiRenderer 가 소유하고, DrawContext 는 그 얇은 파사드(위젯이 배처 세부를 몰라도 되게).
#pragma once

#include "mye/core/Math.h"
#include "mye/ui/UiTypes.h"
#include "mye/text/TextTypes.h"
#include "mye/text/TextRenderer.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace mye::render { class SpriteBatch; }
namespace mye::rhi    { class ICommandContext; }
namespace mye::text   { class GlyphAtlas; class TextRenderer; class IFontProvider; class TextLayout; }

namespace mye::ui {

// 스프라이트 참조(에셋 확정 전 계약 표면). 텍스처 핸들 + 정규화 uv. SpriteRef(asset) 로 승격 예정.
struct UiSprite {
    rhi::TextureHandle texture{};
    Rect               uv{0, 0, 1, 1};
    Vec2i              nativeSize{};   // 9-slice 계산용 소스 픽셀 크기
};

// 위젯이 그리기에 쓰는 파사드. UiRenderer 가 생성·주입.
class UiDrawContext {
public:
    UiDrawContext(render::SpriteBatch& batch, rhi::ICommandContext& ctx,
                  text::GlyphAtlas& atlas, text::TextRenderer& textRenderer,
                  const text::IFontProvider& fonts)
        : m_batch(batch), m_ctx(ctx), m_atlas(atlas),
          m_text(textRenderer), m_fonts(fonts) {}

    // --- 저수준 드로우(픽셀 좌표, 좌상단 원점 — 내부에서 스크린 카메라로 변환·정수 스냅) ---
    void DrawRect(const UiRect& rect, Color color);                       // 단색 채움(1x1 화이트 텍스처)
    void DrawSprite(const UiRect& rect, const UiSprite& sprite, Color tint = Color::White());
    void DrawNineSlice(const UiRect& rect, const UiSprite& sprite,
                       const NineSlice& slice, Color tint = Color::White());
    void DrawText(const UiRect& rect, std::string_view richText,
                  const text::TextStyle& style, const text::LayoutParams& params);
    // 미리 레이아웃된 텍스트 제출(Label 캐시 경로).
    void DrawTextLayout(const text::TextLayout& layout, Vec2 origin);

    // --- 시저 클리핑(ScrollView) — 스택 push/pop. 교집합 누적. ---
    void PushScissor(const UiRect& rect);
    void PopScissor();

    // 접근자(고급 위젯이 직접 배처 사용 시).
    render::SpriteBatch&   Batch() { return m_batch; }
    rhi::ICommandContext&  Ctx()   { return m_ctx; }
    text::GlyphAtlas&      Atlas() { return m_atlas; }
    const text::IFontProvider& Fonts() const { return m_fonts; }

    // 단색 채움(DrawRect)용 1x1 화이트 텍스처. UiRenderer 가 주입.
    void SetWhiteTexture(rhi::TextureHandle t) { m_whiteTex = t; }
    // 화면 크기(시저 클램프·전체 클립 기본값). UiRenderer 가 주입.
    void SetScreenSize(Vec2i px) { m_screen = px; }

    // 현재 유효 클립 사각형(디버그·컬링). 스택 비면 전체 화면.
    UiRect CurrentClip() const;

private:
    void ApplyScissor(const UiRect& r);   // 배처 flush → SetScissor(정수 클램프)
    text::TextRenderer::ClipRect TextClip() const;   // CurrentClip → 텍스트 시저(글리프 CPU 클립)

    render::SpriteBatch&        m_batch;
    rhi::ICommandContext&       m_ctx;
    text::GlyphAtlas&           m_atlas;
    text::TextRenderer&         m_text;
    const text::IFontProvider&  m_fonts;
    rhi::TextureHandle          m_whiteTex{};
    Vec2i                       m_screen{960, 540};
    std::vector<UiRect>         m_scissorStack;
};

} // namespace mye::ui
