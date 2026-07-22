// mye/ui/UiRenderer.h — UI 트리를 SpriteBatch 스크린 레이어로 렌더 (docs/06 §1 렌더링)
//
// 소유: 06 (M5-A). 위젯 트리를 순회하며 UiDrawContext 를 통해 02 SpriteBatch 로 제출한다.
//   스크린 스페이스 카메라(픽셀=unit, 좌상단 원점 → SpriteBatch 좌표 변환)를 셋업하고, 시저
//   클리핑(ScrollView)·정수 픽셀 스냅을 강제한다. UI 전용 렌더 패스는 없다(기존 배처 재사용).
//
// 좌표 변환 정본: UI 픽셀 좌표(좌상단 원점, +Y 아래) → SpriteBatch viewProj.
//   화면 (0,0)=좌상단, (W,H)=우하단 픽셀을 커버하는 정사영을 만든다(OrthoOffCenter 기반).
//   글리프·스프라이트는 픽셀 단위로 제출되며 배처가 그대로 그린다(PPU 개입 없음 — UI 는 1:1 픽셀).
#pragma once

#include "mye/core/Math.h"
#include "mye/ui/UiDrawContext.h"

#include <cstdint>
#include <vector>

namespace mye::render { class SpriteBatch; }
namespace mye::rhi    { class IDevice; class ICommandContext; }
namespace mye::text   { class GlyphAtlas; class TextRenderer; class IFontProvider; }

namespace mye::ui {

class Widget;

class UiRenderer {
public:
    UiRenderer() = default;
    ~UiRenderer() = default;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // device: 1x1 화이트 텍스처(DrawRect 용) 등 내부 리소스 생성. 배처/아틀라스/텍스트는 외부 소유.
    void Init(rhi::IDevice& device);
    void Shutdown();

    // 스크린 픽셀 크기 갱신(리사이즈 시). viewProj 재계산.
    void SetScreenSize(Vec2i pixels);

    // 한 프레임 UI 렌더: batch.Begin(스크린 viewProj) → 트리 순회 draw → batch.End(ctx).
    //   활성 RenderPass 안에서 호출(배처 End 계약). fonts 는 텍스트 폰트 해석기.
    void Render(rhi::ICommandContext& ctx, render::SpriteBatch& batch,
                text::GlyphAtlas& atlas, text::TextRenderer& textRenderer,
                const text::IFontProvider& fonts, Widget& root);

    // UI 픽셀 좌표(좌상단 원점) → SpriteBatch 스크린 카메라 viewProj.
    const Mat4& ViewProj() const { return m_viewProj; }
    // 1x1 화이트 텍스처(단색 rect 채움용) — DrawContext 가 사용.
    rhi::TextureHandle WhiteTexture() const { return m_whiteTex; }

private:
    void RecomputeViewProj();

    rhi::IDevice*      m_device = nullptr;
    rhi::TextureHandle m_whiteTex{};
    Vec2i              m_screen{960, 540};   // 기준 해상도(960×540, M1 확정)
    Mat4               m_viewProj{};
};

} // namespace mye::ui
