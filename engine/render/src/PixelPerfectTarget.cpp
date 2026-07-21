// mye/render/PixelPerfectTarget.cpp — 내부 RT + 정수배 업스케일 블릿 구현 (docs/02 §해상도)
#include "mye/render/PixelPerfectTarget.h"

#include "mye/render/Camera2D.h"   // kInternalWidth/Height 상수
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"
#include "mye/rhi/ShaderCompiler.h"

#include <algorithm>
#include <cstring>

namespace mye::render {
namespace {

constexpr const char* kLog = "PixelPerfect";

// 풀스크린 삼각형 업스케일 블릿 셰이더.
// VS: SV_VertexID로 화면 덮는 삼각형 생성. UV = uvScale*base + uvOffset.
// PS: 내부 RT를 포인트 샘플. premultiplied 그대로 출력.
constexpr const char* kBlitHlsl = R"hlsl(
cbuffer BlitConstants : register(b0) {
    float2 gUvScale;    // 보통 (1,1)
    float2 gUvOffset;   // 서브픽셀 잔차(UV 단위)
};

Texture2D    gTex     : register(t0);
SamplerState gSampler : register(s0);

struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VsOut vs_main(uint vid : SV_VertexID) {
    VsOut o;
    // 큰 삼각형: (0,0)(2,0)(0,2) in UV → clip 전체 커버
    float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0),(2,0),(0,2)
    o.uv = uv * gUvScale + gUvOffset;
    // UV(좌상단 원점, +V 아래) → clip(-1..1, +Y 위): x=uv*2-1, y=1-uv*2
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    return gTex.Sample(gSampler, i.uv);
}
)hlsl";

rhi::ShaderHandle CompileStage(rhi::IDevice& dev, rhi::ShaderStage stage,
                               const char* entry, const char* name) {
    rhi::ShaderCompileDesc d{};
    d.source = kBlitHlsl;
    d.entryPoint = entry;
    d.stage = stage;
    d.debugName = name;
    auto bc = rhi::CompileShaderFxc(d);
    if (!bc) {
        MYE_LOG_FATAL(kLog, "blit shader compile failed ({}): {}", name, bc.GetError().message);
        return {};
    }
    return dev.CreateShader(stage, bc.Value().data);
}

struct BlitConstants {
    float uvScaleX = 1.0f, uvScaleY = 1.0f;
    float uvOffsetX = 0.0f, uvOffsetY = 0.0f;
};

} // namespace

void PixelPerfectTarget::Init(rhi::IDevice& device, const PixelPerfectDesc& desc) {
    if (m_initialized) return;
    m_device = &device;
    m_desc = desc;
    if (m_desc.internalWidth == 0)  m_desc.internalWidth = kInternalWidth;
    if (m_desc.internalHeight == 0) m_desc.internalHeight = kInternalHeight;
    namespace rhi = mye::rhi;

    // 컬러 RT (Sampled|RenderTarget|CopySrc)
    rhi::TextureDesc colorDesc{};
    colorDesc.dim = rhi::TextureDim::Tex2D;
    colorDesc.format = m_desc.colorFormat;
    colorDesc.width = m_desc.internalWidth;
    colorDesc.height = m_desc.internalHeight;
    colorDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget |
                      rhi::TextureUsage::CopySrc;
    colorDesc.debugName = "pixelperfect.color";
    m_color = device.CreateTexture(colorDesc, nullptr);

    // 뎁스(선택). 샘플 안 함 → DepthStencil 전용.
    if (m_desc.useDepth) {
        rhi::TextureDesc depthDesc{};
        depthDesc.dim = rhi::TextureDim::Tex2D;
        depthDesc.format = m_desc.depthFormat;
        depthDesc.width = m_desc.internalWidth;
        depthDesc.height = m_desc.internalHeight;
        depthDesc.usage = rhi::TextureUsage::DepthStencil;
        depthDesc.debugName = "pixelperfect.depth";
        m_depth = device.CreateTexture(depthDesc, nullptr);
    }

    // 블릿 샘플러(포인트·클램프)
    rhi::SamplerDesc smpDesc{};
    smpDesc.filter = rhi::Filter::Point;
    smpDesc.u = smpDesc.v = smpDesc.w = rhi::AddressMode::Clamp;
    smpDesc.debugName = "pixelperfect.sampler";
    m_sampler = device.CreateSampler(smpDesc);

    // 블릿 상수 버퍼(b0)
    rhi::BufferDesc cbDesc{};
    cbDesc.size = sizeof(BlitConstants);
    cbDesc.usage = rhi::BufferUsage::Uniform;
    cbDesc.access = rhi::CpuAccess::WriteDiscardRing;
    cbDesc.debugName = "pixelperfect.blitCB";
    m_blitConstants = device.CreateBuffer(cbDesc, nullptr);

    // 셰이더
    m_blitVs = CompileStage(device, rhi::ShaderStage::Vertex, "vs_main", "blit.vs");
    m_blitPs = CompileStage(device, rhi::ShaderStage::Pixel,  "ps_main", "blit.ps");

    // BindGroupLayout: b0(VS+PS), t0(PS), s0(PS) — 단일 그룹(slot0)
    const rhi::BindGroupLayoutEntry entries[] = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStageFlags::VertexBit | rhi::ShaderStageFlags::PixelBit},
        {0, rhi::BindingType::SampledTexture, rhi::ShaderStageFlags::PixelBit},
        {0, rhi::BindingType::Sampler,        rhi::ShaderStageFlags::PixelBit},
    };
    rhi::BindGroupLayoutDesc layoutDesc{};
    layoutDesc.entries = entries;
    layoutDesc.debugName = "blit.layout";
    m_blitLayout = device.CreateBindGroupLayout(layoutDesc);

    rhi::BindGroupEntry bg[3];
    bg[0].binding = 0; bg[0].type = rhi::BindingType::UniformBuffer;  bg[0].buffer = m_blitConstants;
    bg[1].binding = 0; bg[1].type = rhi::BindingType::SampledTexture; bg[1].texture = m_color;
    bg[2].binding = 0; bg[2].type = rhi::BindingType::Sampler;        bg[2].sampler = m_sampler;
    rhi::BindGroupDesc bgDesc{};
    bgDesc.layout = m_blitLayout;
    bgDesc.entries = {bg, 3};
    bgDesc.debugName = "blit.bindGroup";
    m_blitBindGroup = device.CreateBindGroup(bgDesc);

    // 블릿 파이프라인(정점 버퍼 없음 — SV_VertexID). 블렌드 없음(불투명 복사).
    const rhi::BindGroupLayoutHandle layouts[] = {m_blitLayout};
    rhi::GraphicsPipelineDesc pDesc{};
    pDesc.vs = m_blitVs;
    pDesc.ps = m_blitPs;
    pDesc.vertexLayout.attributes = {};       // 정점 입력 없음
    pDesc.vertexLayout.stride = 0;
    pDesc.topology = rhi::PrimitiveTopology::TriangleList;
    pDesc.blend = rhi::BlendStateDesc::Opaque();
    pDesc.raster.cull = rhi::CullMode::None;
    pDesc.rtFormats.colorCount = 1;
    pDesc.rtFormats.colorFormats[0] = m_desc.backbufferFormat;
    pDesc.rtFormats.depthStencilFormat = rhi::Format::Unknown;
    pDesc.bindGroupLayouts = layouts;
    pDesc.debugName = "blit.pso";
    m_blitPipeline = device.CreateGraphicsPipeline(pDesc);

    m_initialized = m_color.IsValid() && m_blitPipeline.IsValid() &&
                    m_blitBindGroup.IsValid() && m_blitConstants.IsValid();
    if (!m_initialized) {
        MYE_LOG_ERROR(kLog, "PixelPerfectTarget::Init failed (resource creation error)");
    }
}

void PixelPerfectTarget::Shutdown() {
    if (!m_device) return;
    auto& d = *m_device;
    if (m_blitPipeline.IsValid())  d.Destroy(m_blitPipeline);
    if (m_blitBindGroup.IsValid()) d.Destroy(m_blitBindGroup);
    if (m_blitLayout.IsValid())    d.Destroy(m_blitLayout);
    if (m_blitPs.IsValid())        d.Destroy(m_blitPs);
    if (m_blitVs.IsValid())        d.Destroy(m_blitVs);
    if (m_blitConstants.IsValid()) d.Destroy(m_blitConstants);
    if (m_sampler.IsValid())       d.Destroy(m_sampler);
    if (m_depth.IsValid())         d.Destroy(m_depth);
    if (m_color.IsValid())         d.Destroy(m_color);
    m_blitPipeline = {}; m_blitBindGroup = {}; m_blitLayout = {};
    m_blitPs = {}; m_blitVs = {}; m_blitConstants = {}; m_sampler = {};
    m_depth = {}; m_color = {};
    m_initialized = false;
    m_device = nullptr;
}

rhi::TextureHandle PixelPerfectTarget::ColorTarget() const { return m_color; }
rhi::TextureHandle PixelPerfectTarget::DepthTarget() const { return m_depth; }

void PixelPerfectTarget::BeginScenePass(rhi::ICommandContext& ctx, Color clearColor) {
    namespace rhi = mye::rhi;
    rhi::RenderPassColorAttachment color{};
    color.texture = m_color;
    color.loadOp = rhi::LoadOp::Clear;
    color.clearColor = clearColor;

    rhi::RenderPassDepthStencilAttachment depth{};
    if (m_depth.IsValid()) {
        depth.texture = m_depth;
        depth.depthLoadOp = rhi::LoadOp::Clear;
        depth.clearDepth = 1.0f;
        depth.stencilLoadOp = rhi::LoadOp::DontCare;
    }

    rhi::RenderPassBeginDesc pass{};
    pass.colorAttachments = {&color, 1};
    pass.depthStencil = m_depth.IsValid() ? &depth : nullptr;
    pass.debugName = "pixelperfect.scene";
    ctx.BeginRenderPass(pass);

    rhi::Viewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width = static_cast<float>(m_desc.internalWidth);
    vp.height = static_cast<float>(m_desc.internalHeight);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    ctx.SetViewport(vp);
}

void PixelPerfectTarget::EndScenePass(rhi::ICommandContext& ctx) {
    ctx.EndRenderPass();
}

UpscaleLayout PixelPerfectTarget::ComputeLayout(Vec2i internalSize, Vec2i windowSize) {
    UpscaleLayout out{};
    if (internalSize.x <= 0 || internalSize.y <= 0 || windowSize.x <= 0 || windowSize.y <= 0) {
        out.scale = 1;
        out.destRect = {0, 0, internalSize.x, internalSize.y};
        return out;
    }
    const int32_t sx = windowSize.x / internalSize.x;
    const int32_t sy = windowSize.y / internalSize.y;
    int32_t scale = std::min(sx, sy);
    if (scale < 1) scale = 1;      // 창이 내부 해상도보다 작아도 최소 1배(크롭)

    const int32_t dw = internalSize.x * scale;
    const int32_t dh = internalSize.y * scale;
    const int32_t ox = (windowSize.x - dw) / 2;   // 레터박스 중앙
    const int32_t oy = (windowSize.y - dh) / 2;

    out.scale = static_cast<uint32_t>(scale);
    out.destRect = {ox, oy, dw, dh};
    return out;
}

void PixelPerfectTarget::Blit(rhi::ICommandContext& ctx, rhi::TextureHandle backbuffer,
                              Vec2i windowSize, Vec2 subpixelOffset) {
    namespace rhi = mye::rhi;
    if (!m_initialized || !backbuffer.IsValid()) return;

    const UpscaleLayout layout = ComputeLayout(
        {static_cast<int32_t>(m_desc.internalWidth), static_cast<int32_t>(m_desc.internalHeight)},
        windowSize);

    // 백버퍼를 검은색으로 클리어(레터박스 바) 후 destRect 뷰포트에 풀스크린 삼각형.
    rhi::RenderPassColorAttachment color{};
    color.texture = backbuffer;
    color.loadOp = rhi::LoadOp::Clear;
    color.clearColor = Color::Black();

    rhi::RenderPassBeginDesc pass{};
    pass.colorAttachments = {&color, 1};
    pass.debugName = "pixelperfect.blit";
    ctx.BeginRenderPass(pass);

    // 서브픽셀 잔차(내부 RT 픽셀, +Y 아래) → UV 오프셋. UV는 +V 아래이므로 부호 그대로.
    BlitConstants cb{};
    cb.uvScaleX = 1.0f; cb.uvScaleY = 1.0f;
    cb.uvOffsetX = subpixelOffset.x / static_cast<float>(m_desc.internalWidth);
    cb.uvOffsetY = subpixelOffset.y / static_cast<float>(m_desc.internalHeight);
    if (void* p = ctx.MapDynamic(m_blitConstants, sizeof(BlitConstants))) {
        std::memcpy(p, &cb, sizeof(BlitConstants));
        ctx.Unmap(m_blitConstants);
    }

    rhi::Viewport vp{};
    vp.x = static_cast<float>(layout.destRect.x);
    vp.y = static_cast<float>(layout.destRect.y);
    vp.width = static_cast<float>(layout.destRect.w);
    vp.height = static_cast<float>(layout.destRect.h);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    ctx.SetViewport(vp);

    ctx.SetPipeline(m_blitPipeline);
    ctx.SetBindGroup(rhi::kBindGroupSlotPerFrame, m_blitBindGroup);
    ctx.Draw(3, 0);

    ctx.EndRenderPass();
}

} // namespace mye::render
