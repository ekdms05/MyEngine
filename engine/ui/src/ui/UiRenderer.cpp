// mye/ui/UiRenderer.cpp — UI 스크린 레이어 렌더 (M5-A 골격; 화이트텍스처·viewProj·시저는 구현 에이전트)
#include "mye/ui/UiRenderer.h"
#include "mye/ui/Widget.h"

#include "mye/render/SpriteBatch.h"
#include "mye/rhi/Rhi.h"

namespace mye::ui {

void UiRenderer::Init(rhi::IDevice& device) {
    m_device = &device;
    // 1x1 화이트 텍스처(단색 rect 채움용). RGBA8, {255,255,255,255}.
    const uint32_t white = 0xFFFFFFFFu;
    rhi::TextureDesc desc{};
    desc.dim = rhi::TextureDim::Tex2D;
    desc.format = rhi::Format::RGBA8Unorm;
    desc.width = 1;
    desc.height = 1;
    desc.mips = 1;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
    desc.debugName = "ui_white_1x1";
    rhi::TextureInitData init{};
    init.data = &white;
    init.rowPitch = 4;
    m_whiteTex = device.CreateTexture(desc, &init);
    RecomputeViewProj();
}

void UiRenderer::Shutdown() {
    if (m_device && m_whiteTex.IsValid()) m_device->Destroy(m_whiteTex);
    m_whiteTex = {};
    m_device = nullptr;
}

void UiRenderer::SetScreenSize(Vec2i pixels) {
    m_screen = pixels;
    RecomputeViewProj();
}

void UiRenderer::RecomputeViewProj() {
    // UI 픽셀 좌표(좌상단 원점, +Y 아래) → NDC. 화면 (0,0)=좌상단, (W,H)=우하단.
    //   OrthoOffCenter(left=0, right=W, bottom=H, top=0) 로 +Y 아래를 매핑(LH, 깊이 0~1).
    const float w = static_cast<float>(m_screen.x);
    const float h = static_cast<float>(m_screen.y);
    m_viewProj = Mat4::OrthoOffCenterLH(0.0f, w, h, 0.0f, 0.0f, 1.0f);
}

void UiRenderer::Render(rhi::ICommandContext& ctx, render::SpriteBatch& batch,
                        text::GlyphAtlas& atlas, text::TextRenderer& textRenderer,
                        const text::IFontProvider& fonts, Widget& root) {
    batch.Begin(m_viewProj);
    UiDrawContext dc(batch, ctx, atlas, textRenderer, fonts);
    dc.SetWhiteTexture(m_whiteTex);
    dc.SetScreenSize(m_screen);
    root.draw(dc);
    batch.End(ctx);
}

} // namespace mye::ui
