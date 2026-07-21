// mye/render/HybridRenderer.cpp — 하이브리드 깊이 렌더러 구현 (docs/02 §하이브리드 깊이 정렬)
#include "mye/render/HybridRenderer.h"

#include "mye/render/Camera2D.h"
#include "mye/core/Log.h"
#include "mye/asset/Texture.h"
#include "mye/asset/Mesh.h"
#include "mye/scene/RenderExtract.h"
#include "mye/tilemap/Tilemap.h"
#include "mye/rhi/Rhi.h"
#include "mye/rhi/ShaderCompiler.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace mye::render {
namespace {

constexpr const char* kLog = "HybridRenderer";
constexpr uint32_t kInitialQuads = 4096;
constexpr uint32_t kVertsPerQuad = 6;

// ---------------------------------------------------------------------------
// 스프라이트/타일 셰이더 — 하이브리드 뎁스 cutout
// ---------------------------------------------------------------------------
// VS: 월드 XY를 카메라 viewProj로 변환하되, clip.z는 정점에 실린 인코딩 깊이를 그대로 사용
//     (perspective divide 없는 직교이므로 clip.w=1 → NDC.z = 인코딩 깊이). "flat/ramp depth".
// PS: alpha cutout(a<cutoff → discard) + premultiplied 틴트 + 히트 플래시. 뎁스 기록은 PSO가 담당.
constexpr const char* kQuadHlsl = R"hlsl(
cbuffer FrameConstants : register(b0) {
    row_major float4x4 gViewProj;
    float  gAlphaCutoff;
    float3 _pad0;
};

Texture2D    gTex     : register(t0);
SamplerState gSampler : register(s0);

struct VsIn {
    float3 pos   : POSITION;    // 월드 xy + 인코딩된 NDC 깊이(z)
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;      // straight 틴트
    float  flash : TEXCOORD1;
};
struct VsOut {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    float  flash : TEXCOORD1;
};

VsOut vs_main(VsIn i) {
    VsOut o;
    // XY만 viewProj로 변환. z는 인코딩된 깊이를 그대로(직교 → w=1). ortho viewProj의 z항은 무시.
    float4 clip = mul(float4(i.pos.xy, 0.0, 1.0), gViewProj);
    o.pos = float4(clip.xy, i.pos.z, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    o.flash = i.flash;
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    float4 texel = gTex.Sample(gSampler, i.uv);   // premultiplied RGBA
    // cutout: premultiplied이므로 알파 기준 컷. (straight 알파와 동일 판정)
    clip(texel.a - gAlphaCutoff);
    float4 tint = i.color;
    float4 outc = texel * float4(tint.rgb * tint.a, tint.a);
    outc.rgb = lerp(outc.rgb, outc.a.xxx, saturate(i.flash));
    return outc;
}
)hlsl";

// ---------------------------------------------------------------------------
// 3D 메시 셰이더 — 포워드(뎁스 테스트/기록), 방향광 1개 + 앰비언트, AnchorBiased 깊이
// ---------------------------------------------------------------------------
// VS: object→world→viewProj로 정상 3D 변환. depthMode에 따라 clip.z 재매핑:
//   - Geometry:     그대로.
//   - AnchorFlat:   앵커 flat depth(gAnchorDepth)로 전부 평탄화.
//   - AnchorBiased: gAnchorDepth + (viewZ - gAnchorViewZ)*ε 로 앵커 기준 축소 깊이.
//     viewZ는 뷰공간 z(정규화 전). ε(gBiasEps)가 밴드 침범을 막는다.
// clip.z 재매핑은 clip.w를 곱해 저장(래스터라이저 NDC.z = z/w 복원 위함).
constexpr const char* kMeshHlsl = R"hlsl(
cbuffer MeshConstants : register(b0) {
    row_major float4x4 gWorld;
    row_major float4x4 gView;
    row_major float4x4 gViewProj;
    float3 gLightDirWorld;  float gAmbient;
    float3 gLightColor;     float gDepthMode;    // 0=AnchorFlat 1=AnchorBiased 2=Geometry
    float  gAnchorDepth;    float gAnchorViewZ;  float gBiasEps;  float _pad;
    float4 gTint;
};

Texture2D    gTex     : register(t0);
SamplerState gSampler : register(s0);

struct VsIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VsOut {
    float4 pos : SV_Position;
    float3 nrmW : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VsOut vs_main(VsIn i) {
    VsOut o;
    float4 worldP = mul(float4(i.pos, 1.0), gWorld);
    float4 clip   = mul(worldP, gViewProj);
    float  viewZ  = mul(worldP, gView).z;

    float z = clip.z;                                  // Geometry 기본(NDC*w)
    if (gDepthMode < 0.5) {                            // AnchorFlat
        z = gAnchorDepth * clip.w;
    } else if (gDepthMode < 1.5) {                     // AnchorBiased
        float ndc = gAnchorDepth + (viewZ - gAnchorViewZ) * gBiasEps;
        z = saturate(ndc) * clip.w;
    }
    o.pos = float4(clip.xy, z, clip.w);
    o.nrmW = mul(i.nrm, (float3x3)gWorld);
    o.uv = i.uv;
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    float4 albedo = gTex.Sample(gSampler, i.uv) * gTint;
    clip(albedo.a - 0.5);                              // 메시도 cutout(단일 뎁스 도메인 정합)
    float3 N = normalize(i.nrmW);
    float3 L = normalize(-gLightDirWorld);
    float ndl = saturate(dot(N, L));
    float3 lit = albedo.rgb * (gAmbient + gLightColor * ndl);
    return float4(lit, albedo.a);
}
)hlsl";

rhi::ShaderHandle CompileStage(rhi::IDevice& dev, const char* src, rhi::ShaderStage stage,
                               const char* entry, const char* name) {
    rhi::ShaderCompileDesc d{};
    d.source = src;
    d.entryPoint = entry;
    d.stage = stage;
    d.debugName = name;
    auto bc = rhi::CompileShaderFxc(d);
    if (!bc) {
        MYE_LOG_FATAL(kLog, "shader compile failed ({}): {}", name, bc.GetError().message);
        return {};
    }
    return dev.CreateShader(stage, bc.Value().data);
}

} // namespace

// b0 레이아웃(스프라이트/타일). float4x4 + float + float3 pad = 80B.
struct HybridRenderer::FrameCB {
    Mat4  viewProj;
    float alphaCutoff;
    float pad[3];
};

// b0 레이아웃(메시). 셰이더 cbuffer와 바이트 정합.
struct HybridRenderer::MeshInstanceCB {
    Mat4  world;
    Mat4  view;
    Mat4  viewProj;
    float lightDir[3];  float ambient;
    float lightColor[3]; float depthMode;
    float anchorDepth;  float anchorViewZ;  float biasEps;  float pad0;
    float tint[4];
};

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------
void HybridRenderer::Init(rhi::IDevice& device, const HybridRendererDesc& desc) {
    if (m_initialized) return;
    m_device = &device;
    m_desc = desc;

    // 샘플러(포인트·클램프 — 픽셀아트).
    rhi::SamplerDesc smp{};
    smp.filter = rhi::Filter::Point;
    smp.u = smp.v = smp.w = rhi::AddressMode::Clamp;
    smp.debugName = "hybrid.sampler";
    m_sampler = device.CreateSampler(smp);

    InitSpriteTilePipeline();
    InitMeshPipeline();

    EnsureQuadCapacity(kInitialQuads);
    m_scratchQuads.reserve(kInitialQuads * kVertsPerQuad);

    m_initialized = m_quadPipeline.IsValid() && m_meshPipeline.IsValid() &&
                    m_frameBG.IsValid() && m_meshCbBG.IsValid();
    if (!m_initialized) {
        MYE_LOG_ERROR(kLog, "HybridRenderer::Init failed (resource creation error)");
    }
}

void HybridRenderer::InitSpriteTilePipeline() {
    auto& device = *m_device;
    namespace rhi = mye::rhi;

    m_quadVs = CompileStage(device, kQuadHlsl, rhi::ShaderStage::Vertex, "vs_main", "hybrid.quad.vs");
    m_quadPs = CompileStage(device, kQuadHlsl, rhi::ShaderStage::Pixel,  "ps_main", "hybrid.quad.ps");

    rhi::BufferDesc cb{};
    cb.size = sizeof(FrameCB);
    cb.usage = rhi::BufferUsage::Uniform;
    cb.access = rhi::CpuAccess::WriteDiscardRing;
    cb.debugName = "hybrid.frameCB";
    m_frameCB = device.CreateBuffer(cb, nullptr);

    const rhi::BindGroupLayoutEntry frameEntries[] = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStageFlags::All},
    };
    rhi::BindGroupLayoutDesc fld{};
    fld.entries = frameEntries;
    fld.debugName = "hybrid.frameLayout";
    m_frameLayout = device.CreateBindGroupLayout(fld);

    const rhi::BindGroupLayoutEntry texEntries[] = {
        {0, rhi::BindingType::SampledTexture, rhi::ShaderStageFlags::PixelBit},
        {0, rhi::BindingType::Sampler,        rhi::ShaderStageFlags::PixelBit},
    };
    rhi::BindGroupLayoutDesc tld{};
    tld.entries = texEntries;
    tld.debugName = "hybrid.texLayout";
    m_texLayout = device.CreateBindGroupLayout(tld);

    rhi::BindGroupEntry fbEntry{};
    fbEntry.binding = 0;
    fbEntry.type = rhi::BindingType::UniformBuffer;
    fbEntry.buffer = m_frameCB;
    rhi::BindGroupDesc fbd{};
    fbd.layout = m_frameLayout;
    fbd.entries = {&fbEntry, 1};
    fbd.debugName = "hybrid.frameBG";
    m_frameBG = device.CreateBindGroup(fbd);

    const rhi::VertexAttribute attrs[] = {
        {"POSITION", 0, rhi::Format::RGB32Float,  offsetof(QuadVertex, x),     0},
        {"TEXCOORD", 0, rhi::Format::RG32Float,   offsetof(QuadVertex, u),     0},
        {"COLOR",    0, rhi::Format::RGBA32Float, offsetof(QuadVertex, r),     0},
        {"TEXCOORD", 1, rhi::Format::RG32Float,   offsetof(QuadVertex, flash), 0},
    };
    const rhi::BindGroupLayoutHandle layouts[] = {m_frameLayout, {}, m_texLayout};

    rhi::GraphicsPipelineDesc p{};
    p.vs = m_quadVs;
    p.ps = m_quadPs;
    p.vertexLayout.attributes = attrs;
    p.vertexLayout.stride = sizeof(QuadVertex);
    p.topology = rhi::PrimitiveTopology::TriangleList;
    p.blend = rhi::BlendStateDesc::Premultiplied();
    p.raster.cull = rhi::CullMode::None;
    // 하이브리드 핵심: cutout이므로 뎁스 테스트/기록 ON. LessEqual(작은 z=앞).
    p.depthStencil.depthTest = true;
    p.depthStencil.depthWrite = true;
    p.depthStencil.depthFunc = rhi::CompareFunc::LessEqual;
    p.rtFormats.colorCount = 1;
    p.rtFormats.colorFormats[0] = m_desc.colorFormat;
    p.rtFormats.depthStencilFormat = m_desc.depthFormat;
    p.bindGroupLayouts = layouts;
    p.debugName = "hybrid.quad.pso";
    m_quadPipeline = device.CreateGraphicsPipeline(p);
}

void HybridRenderer::InitMeshPipeline() {
    auto& device = *m_device;
    namespace rhi = mye::rhi;

    m_meshVs = CompileStage(device, kMeshHlsl, rhi::ShaderStage::Vertex, "vs_main", "hybrid.mesh.vs");
    m_meshPs = CompileStage(device, kMeshHlsl, rhi::ShaderStage::Pixel,  "ps_main", "hybrid.mesh.ps");

    rhi::BufferDesc cb{};
    cb.size = sizeof(MeshInstanceCB);
    cb.usage = rhi::BufferUsage::Uniform;
    cb.access = rhi::CpuAccess::WriteDiscardRing;
    cb.debugName = "hybrid.meshCB";
    m_meshCB = device.CreateBuffer(cb, nullptr);

    const rhi::BindGroupLayoutEntry cbEntries[] = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStageFlags::All},
    };
    rhi::BindGroupLayoutDesc cld{};
    cld.entries = cbEntries;
    cld.debugName = "hybrid.meshCbLayout";
    m_meshCbLayout = device.CreateBindGroupLayout(cld);

    rhi::BindGroupEntry cbEntry{};
    cbEntry.binding = 0;
    cbEntry.type = rhi::BindingType::UniformBuffer;
    cbEntry.buffer = m_meshCB;
    rhi::BindGroupDesc bd{};
    bd.layout = m_meshCbLayout;
    bd.entries = {&cbEntry, 1};
    bd.debugName = "hybrid.meshCbBG";
    m_meshCbBG = device.CreateBindGroup(bd);

    // 정점 레이아웃은 04 asset::Mesh 정본(POSITION/NORMAL/TEXCOORD0, stride 32) 사용.
    const auto meshAttrs = asset::MeshVertexAttributes();
    // 메시 텍스처는 스프라이트와 같은 texLayout(slot2) 사용.
    const rhi::BindGroupLayoutHandle layouts[] = {m_meshCbLayout, {}, m_texLayout};

    rhi::GraphicsPipelineDesc p{};
    p.vs = m_meshVs;
    p.ps = m_meshPs;
    p.vertexLayout.attributes = meshAttrs;
    p.vertexLayout.stride = asset::kMeshVertexStride;
    p.topology = rhi::PrimitiveTopology::TriangleList;
    p.blend = rhi::BlendStateDesc::Opaque();        // 불투명 3D(cutout, 블렌드 없음)
    p.raster.cull = rhi::CullMode::Back;             // 3D 백페이스 컬
    p.depthStencil.depthTest = true;
    p.depthStencil.depthWrite = true;
    p.depthStencil.depthFunc = rhi::CompareFunc::LessEqual;
    p.rtFormats.colorCount = 1;
    p.rtFormats.colorFormats[0] = m_desc.colorFormat;
    p.rtFormats.depthStencilFormat = m_desc.depthFormat;
    p.bindGroupLayouts = layouts;
    p.debugName = "hybrid.mesh.pso";
    m_meshPipeline = device.CreateGraphicsPipeline(p);
}

void HybridRenderer::Shutdown() {
    if (!m_device) return;
    auto& d = *m_device;
    for (auto& [k, bg] : m_texCache) if (bg.IsValid()) d.Destroy(bg);
    m_texCache.clear();

    auto DBuf  = [&](rhi::BufferHandle& h)        { if (h.IsValid()) d.Destroy(h); h = {}; };
    auto DPipe = [&](rhi::PipelineHandle& h)      { if (h.IsValid()) d.Destroy(h); h = {}; };
    auto DSh   = [&](rhi::ShaderHandle& h)        { if (h.IsValid()) d.Destroy(h); h = {}; };
    auto DBG   = [&](rhi::BindGroupHandle& h)     { if (h.IsValid()) d.Destroy(h); h = {}; };
    auto DBGL  = [&](rhi::BindGroupLayoutHandle& h){ if (h.IsValid()) d.Destroy(h); h = {}; };

    DPipe(m_quadPipeline); DPipe(m_meshPipeline);
    DBG(m_frameBG); DBG(m_meshCbBG);
    DBGL(m_frameLayout); DBGL(m_texLayout); DBGL(m_meshCbLayout);
    DSh(m_quadVs); DSh(m_quadPs); DSh(m_meshVs); DSh(m_meshPs);
    DBuf(m_quadVB); DBuf(m_frameCB); DBuf(m_meshCB);
    if (m_sampler.IsValid()) { d.Destroy(m_sampler); m_sampler = {}; }

    m_quadCapacity = 0;
    m_scratchQuads.clear();
    m_initialized = false;
    m_device = nullptr;
}

// ---------------------------------------------------------------------------
// 자산 연결
// ---------------------------------------------------------------------------
void HybridRenderer::SetDirectionalLight(Vec3 dirWorld, Color color, float ambient) {
    m_lightDir = dirWorld;
    m_lightColor = color;
    m_ambient = ambient;
}

const asset::Texture* HybridRenderer::Resolve(asset::AssetGuid guid) const {
    if (!m_texResolver || !guid.IsValid()) return nullptr;
    return m_texResolver(m_texUser, guid);
}

const asset::Mesh* HybridRenderer::ResolveMesh(asset::AssetGuid guid) const {
    if (!m_meshResolver || !guid.IsValid()) return nullptr;
    return m_meshResolver(m_meshUser, guid);
}

// ---------------------------------------------------------------------------
// 버퍼·바인드그룹 헬퍼
// ---------------------------------------------------------------------------
void HybridRenderer::EnsureQuadCapacity(uint32_t quadCount) {
    if (quadCount <= m_quadCapacity) return;
    uint32_t cap = m_quadCapacity ? m_quadCapacity : kInitialQuads;
    while (cap < quadCount) cap *= 2;
    if (m_quadVB.IsValid()) m_device->Destroy(m_quadVB);
    namespace rhi = mye::rhi;
    rhi::BufferDesc vb{};
    vb.size = static_cast<uint64_t>(cap) * kVertsPerQuad * sizeof(QuadVertex);
    vb.usage = rhi::BufferUsage::Vertex;
    vb.access = rhi::CpuAccess::WriteDiscardRing;
    vb.debugName = "hybrid.quad.vb";
    m_quadVB = m_device->CreateBuffer(vb, nullptr);
    m_quadCapacity = cap;
}

rhi::BindGroupHandle HybridRenderer::GetOrCreateTextureBG(rhi::TextureHandle tex) {
    const uint64_t key = (static_cast<uint64_t>(tex.index) << 32) | static_cast<uint64_t>(tex.gen);
    if (auto it = m_texCache.find(key); it != m_texCache.end()) return it->second;
    rhi::BindGroupEntry e[2];
    e[0].binding = 0; e[0].type = rhi::BindingType::SampledTexture; e[0].texture = tex;
    e[1].binding = 0; e[1].type = rhi::BindingType::Sampler;        e[1].sampler = m_sampler;
    rhi::BindGroupDesc bd{};
    bd.layout = m_texLayout;
    bd.entries = {e, 2};
    bd.debugName = "hybrid.texBG";
    rhi::BindGroupHandle bg = m_device->CreateBindGroup(bd);
    m_texCache.emplace(key, bg);
    return bg;
}

// ---------------------------------------------------------------------------
// 뷰 정보
// ---------------------------------------------------------------------------
HybridViewInfo HybridRenderer::MakeViewInfo(const Camera2D& camera) {
    HybridViewInfo v{};
    v.view = camera.ViewMatrix();
    v.proj = camera.ProjectionMatrix();
    v.viewProj = camera.ViewProjection();
    // sortKeyY 정규화: 카메라 논리 중심 기준 세로 뷰 범위. 상단(가장 뒤)이 viewTopY.
    const Vec2 center = camera.Position();
    const float halfH = 0.5f * static_cast<float>(kInternalHeight) / (kPixelsPerUnit * camera.Zoom());
    // 여유 20%(경사·점프로 sortKeyY가 뷰 밖으로 나가도 saturate로 안정).
    const float pad = halfH * 0.2f;
    v.depth.viewTopY = center.y + halfH + pad;
    v.depth.viewRangeY = (halfH + pad) * 2.0f;
    v.viewportWidth = kInternalWidth;
    v.viewportHeight = kInternalHeight;
    return v;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
void HybridRenderer::Render(const scene::RenderProxyList& items, const Camera2D& camera,
                            rhi::ICommandContext& ctx) {
    Render(items, MakeViewInfo(camera), ctx);
}

void HybridRenderer::Render(const scene::RenderProxyList& items, const HybridViewInfo& view,
                            rhi::ICommandContext& ctx) {
    m_stats = {};
    if (!m_initialized) return;

    ctx.PushDebugMarker("HybridRenderer");

    // 프레임 상수(스프라이트/타일 viewProj + cutoff) 갱신 — 프레임당 1회.
    if (void* cb = ctx.MapDynamic(m_frameCB, sizeof(FrameCB))) {
        FrameCB f{};
        f.viewProj = view.viewProj;
        f.alphaCutoff = m_desc.alphaCutoff;
        std::memcpy(cb, &f, sizeof(FrameCB));
        ctx.Unmap(m_frameCB);
    }

    // 패스 순서(docs/02): (1) 불투명 3D 메시 → (2) 타일/스프라이트 cutout. 단일 뎁스버퍼 공유.
    DrawMeshes(items, view, ctx);
    CollectAndDrawSpritesTiles(items, view, ctx);

    ctx.PopDebugMarker();
}

// ---------------------------------------------------------------------------
// 3D 메시 패스 (불투명, 포워드)
// ---------------------------------------------------------------------------
void HybridRenderer::DrawMeshes(const scene::RenderProxyList& items, const HybridViewInfo& view,
                                rhi::ICommandContext& ctx) {
    bool any = false;
    for (const scene::RenderItem& it : items.items) {
        if (it.kind != scene::RenderItemKind::Mesh) continue;
        const asset::Mesh* mesh = ResolveMesh(it.mesh);
        if (!mesh || !mesh->vertexBuffer.IsValid()) continue;

        if (!any) {
            ctx.PushDebugMarker("Opaque3D");
            ctx.SetPipeline(m_meshPipeline);
            any = true;
        }

        // 앵커 지면Y → 인코딩 깊이(스프라이트와 동일 함수). AnchorFlat/AnchorBiased의 기준.
        const uint16_t sortLayer = it.sortLayer;
        const float anchorDepth = EncodeDepth(sortLayer, it.sortKeyY, it.orderInLayer, view.depth);

        // 앵커 지면Y의 뷰공간 z(월드 앵커점 = (transform 위치.xy 근사, sortKeyY)). 앵커 라인 기준.
        const Vec3 anchorWorld{it.worldTransform.m[3][0], it.sortKeyY, it.worldTransform.m[3][2]};
        const Vec3 anchorView = TransformPoint(anchorWorld, view.view);

        MeshInstanceCB c{};
        c.world = it.worldTransform;
        c.view = view.view;
        c.viewProj = view.viewProj;
        const Vec3 ld = m_lightDir;
        c.lightDir[0] = ld.x; c.lightDir[1] = ld.y; c.lightDir[2] = ld.z; c.ambient = m_ambient;
        c.lightColor[0] = m_lightColor.r; c.lightColor[1] = m_lightColor.g; c.lightColor[2] = m_lightColor.b;
        c.depthMode = static_cast<float>(it.depthMode);
        c.anchorDepth = anchorDepth;
        c.anchorViewZ = anchorView.z;
        c.biasEps = kAnchorBiasEpsilon;
        c.tint[0] = it.tint.r; c.tint[1] = it.tint.g; c.tint[2] = it.tint.b; c.tint[3] = it.tint.a;

        if (void* cb = ctx.MapDynamic(m_meshCB, sizeof(MeshInstanceCB))) {
            std::memcpy(cb, &c, sizeof(MeshInstanceCB));
            ctx.Unmap(m_meshCB);
        }
        ctx.SetBindGroup(rhi::kBindGroupSlotPerFrame, m_meshCbBG);

        // 알베도 텍스처(머티리얼 GUID → asset::Texture). 없으면 화이트 폴백 불가 → 스킵 대신 그대로.
        const asset::Texture* tex = Resolve(it.material.IsValid() ? it.material : it.texture);
        if (tex && tex->gpuTexture.IsValid()) {
            ctx.SetBindGroup(rhi::kBindGroupSlotPerMaterial, GetOrCreateTextureBG(tex->gpuTexture));
        }

        ctx.SetVertexBuffer(0, mesh->vertexBuffer, 0);
        if (mesh->indexBuffer.IsValid() && mesh->indexCount > 0) {
            ctx.SetIndexBuffer(mesh->indexBuffer, mesh->indexFormat);
            // 서브메시별 드로우(머티리얼 슬롯은 M2-C 단일 — 확장 여지). 없으면 전체 인덱스.
            if (!mesh->submeshes.empty()) {
                for (const asset::Submesh& sm : mesh->submeshes) {
                    if (sm.indexCount == 0) continue;
                    ctx.DrawIndexed(sm.indexCount, sm.firstIndex, sm.baseVertex);
                    ++m_stats.drawCalls;
                }
            } else {
                ctx.DrawIndexed(mesh->indexCount, 0, 0);
                ++m_stats.drawCalls;
            }
        } else {
            ctx.Draw(mesh->vertexCount, 0);
            ++m_stats.drawCalls;
        }
        ++m_stats.meshCount;
    }
    if (any) ctx.PopDebugMarker();
}

// ---------------------------------------------------------------------------
// 타일/스프라이트 패스 (cutout, 하이브리드 뎁스)
// ---------------------------------------------------------------------------
namespace {
// QuadVertex를 텍스처별로 묶어 그리기 위한 런 기록.
struct QuadRun { rhi::TextureHandle tex; uint32_t firstQuad; uint32_t quadCount; };
} // namespace

void HybridRenderer::CollectAndDrawSpritesTiles(const scene::RenderProxyList& items,
                                                const HybridViewInfo& view,
                                                rhi::ICommandContext& ctx) {
    m_scratchQuads.clear();

    // 텍스처별 런으로 쌓기 위해 (텍스처핸들, 정점범위)를 기록. 간단화: item 순서대로 append하고
    // 텍스처가 바뀌는 지점마다 런 분할(스프라이트/타일은 이미 sortLayer로 그룹핑돼 들어옴).
    std::vector<QuadRun> runs;
    rhi::TextureHandle curTex{};
    uint32_t runFirstQuad = 0;

    auto quadsSoFar = [&]() { return static_cast<uint32_t>(m_scratchQuads.size() / kVertsPerQuad); };

    auto flushRun = [&]() {
        const uint32_t now = quadsSoFar();
        if (now > runFirstQuad && curTex.IsValid()) {
            runs.push_back({curTex, runFirstQuad, now - runFirstQuad});
        }
        runFirstQuad = now;
    };

    for (const scene::RenderItem& it : items.items) {
        if (it.kind == scene::RenderItemKind::TilemapChunk) {
            // 타일 청크는 tilemap 텍스처(atlas)가 별도 — texture GUID 사용.
            const asset::Texture* tex = Resolve(it.texture);
            if (!tex || !tex->gpuTexture.IsValid()) continue;
            if (!(tex->gpuTexture == curTex)) { flushRun(); curTex = tex->gpuTexture; }
            BuildTileChunkQuads(it, view);
        } else if (it.kind == scene::RenderItemKind::Sprite ||
                   it.kind == scene::RenderItemKind::Billboard) {
            const asset::Texture* tex = Resolve(it.texture);
            if (!tex || !tex->gpuTexture.IsValid()) continue;
            if (!(tex->gpuTexture == curTex)) { flushRun(); curTex = tex->gpuTexture; }
            BuildSpriteQuad(it, view);
        }
    }
    flushRun();

    const uint32_t totalQuads = quadsSoFar();
    if (totalQuads == 0) return;

    ctx.PushDebugMarker("TilesSprites");
    EnsureQuadCapacity(totalQuads);

    const uint64_t bytes = static_cast<uint64_t>(totalQuads) * kVertsPerQuad * sizeof(QuadVertex);
    if (void* mapped = ctx.MapDynamic(m_quadVB, bytes)) {
        std::memcpy(mapped, m_scratchQuads.data(), static_cast<size_t>(bytes));
        ctx.Unmap(m_quadVB);
    } else {
        MYE_LOG_ERROR(kLog, "quad VB MapDynamic failed");
        ctx.PopDebugMarker();
        return;
    }

    ctx.SetPipeline(m_quadPipeline);
    ctx.SetBindGroup(rhi::kBindGroupSlotPerFrame, m_frameBG);
    ctx.SetVertexBuffer(0, m_quadVB, 0);

    for (const QuadRun& r : runs) {
        ctx.SetBindGroup(rhi::kBindGroupSlotPerMaterial, GetOrCreateTextureBG(r.tex));
        ctx.Draw(r.quadCount * kVertsPerQuad, r.firstQuad * kVertsPerQuad);
        ++m_stats.drawCalls;
    }

    m_stats.spriteCount += 0;  // 세부 카운트는 Build*에서 누적
    ctx.PopDebugMarker();
}

// ---------------------------------------------------------------------------
// 스프라이트/빌보드 → flat depth 쿼드
// ---------------------------------------------------------------------------
void HybridRenderer::BuildSpriteQuad(const scene::RenderItem& it, const HybridViewInfo& view) {
    const asset::Texture* tex = Resolve(it.texture);
    if (!tex) return;

    // flat depth: 발밑(sortKeyY) 기준 단일 인코딩 깊이. 쿼드 전체가 같은 z.
    const float z = EncodeDepth(it.sortLayer, it.sortKeyY, it.orderInLayer, view.depth);

    // 월드 위치(피벗 기준점). worldTransform translation row = m[3].
    const Vec2 pos{it.worldTransform.m[3][0], it.worldTransform.m[3][1]};

    // 크기(unit) = srcUV 픽셀크기/PPU. srcUV가 정규화(0..1)로 들어오면 텍스처 크기로 환산.
    // RenderExtract는 srcUV를 정규화(0..1)로 공급(기본 {0,0,1,1}=전체).
    const float texW = static_cast<float>(tex->width);
    const float texH = static_cast<float>(tex->height);
    const float srcPxW = it.srcUV.w * texW;
    const float srcPxH = it.srcUV.h * texH;
    Vec2 sizeUnit{srcPxW / kPixelsPerUnit, srcPxH / kPixelsPerUnit};
    if (sizeUnit.x <= 0.0f || sizeUnit.y <= 0.0f) sizeUnit = {1.0f, 1.0f};

    // 정규화 피벗(0..1) = pivotPx / 소스픽셀. 기본 발밑(하단 중앙).
    Vec2 pivot{0.5f, 1.0f};
    if (srcPxW > 0.0f && srcPxH > 0.0f && (it.pivotPx.x != 0.0f || it.pivotPx.y != 0.0f)) {
        pivot = {it.pivotPx.x / srcPxW, it.pivotPx.y / srcPxH};
    }

    float u0 = it.srcUV.x, v0 = it.srcUV.y;
    float u1 = it.srcUV.x + it.srcUV.w, v1 = it.srcUV.y + it.srcUV.h;
    if (it.flipX) std::swap(u0, u1);
    if (it.flipY) std::swap(v0, v1);

    const float left   = pos.x - pivot.x * sizeUnit.x;
    const float right  = left + sizeUnit.x;
    const float top    = pos.y + pivot.y * sizeUnit.y;   // +Y 위
    const float bottom = top - sizeUnit.y;

    const Color c = it.tint;
    const float f = it.flashAmount;
    auto push = [&](float px, float py, float pu, float pv) {
        m_scratchQuads.push_back({px, py, z, pu, pv, c.r, c.g, c.b, c.a, f, 0.0f});
    };
    push(left,  top,    u0, v0);
    push(left,  bottom, u0, v1);
    push(right, top,    u1, v0);
    push(right, top,    u1, v0);
    push(left,  bottom, u0, v1);
    push(right, bottom, u1, v1);
    ++m_stats.spriteCount;
}

// ---------------------------------------------------------------------------
// 타일 청크 → 쿼드(경사 램프 깊이)
// ---------------------------------------------------------------------------
void HybridRenderer::BuildTileChunkQuads(const scene::RenderItem& it, const HybridViewInfo& view) {
    if (!it.tilemap) return;
    const asset::Texture* tex = Resolve(it.texture);
    if (!tex) return;

    const tilemap::TileLayer* layer = it.tilemap->Layer(it.tileLayer);
    if (!layer) return;
    const tilemap::TileChunk* chunk = layer->FindChunk(it.chunkCoord);
    if (!chunk) return;

    // 청크 원점(월드 셀 좌표). 셀 (cx,cy) → 월드: x = cx * (48/48)=cx unit? PPU 48, 1타일=1unit.
    // Screen2D 타일→월드(docs/02): x = col*tileW/PPU, y = -row*tileH/PPU. 여기서 tileW=tileH=48px=1unit.
    const int baseCellX = it.chunkCoord.x * tilemap::kChunkSize;
    const int baseCellY = it.chunkCoord.y * tilemap::kChunkSize;

    const Color white = Color::White();

    chunk->ForEachColumn([&](int lx, int ly, const tilemap::TileColumn& col) {
        if (col.tile == tilemap::kNoTile) return;

        const int cellX = baseCellX + lx;
        const int cellY = baseCellY + ly;
        // 셀 월드 좌표(타일 하단-좌 기준). Screen2D: +Y 위이므로 row 증가 = -Y.
        const float wx = static_cast<float>(cellX);
        const float wyBase = -static_cast<float>(cellY);

        // 높이 오프셋(월드 Y): baseHeightLevel × 0.5 unit.
        const float heightY = tilemap::HeightLevelToWorldY(col.baseHeightLevel);

        // 경사 램프: 셀 남단(bottom)→북단(top)로 groundY가 연속 변화 → 깊이 기울기.
        // rise 레벨만큼 셀 위/아래 sortKeyY가 달라진다. SampleHeight로 셀 내 보간 지면Y 사용.
        const int rise = tilemap::SlopeRiseLevels(col.slope);
        const float riseY = tilemap::HeightLevelToWorldY(rise);

        // 타일 쿼드(1×1 unit). 하단 y0, 상단 y1(+Y).
        const float x0 = wx, x1 = wx + 1.0f;
        const float y0 = wyBase + heightY;          // 셀 하단(남단) 지면 Y
        const float y1 = wyBase + 1.0f + heightY;   // 셀 상단(북단)

        // 램프 깊이: 상단/하단의 sortKeyY를 각각 인코딩(경사면 위 캐릭터가 자연스레 끼어듦).
        // 평지(rise=0)는 상하 동일 = flat. 경사는 상단이 riseY만큼 더 높은 지면 → 더 뒤.
        const uint16_t sortLayer = it.sortLayer;
        const float sortKeyBottom = y0;
        const float sortKeyTop = y0 + riseY;   // 경사 상단 지면 Y

        const float zBottom = EncodeDepth(sortLayer, sortKeyBottom, it.orderInLayer, view.depth);
        const float zTop    = EncodeDepth(sortLayer, sortKeyTop,    it.orderInLayer, view.depth);

        // UV: 타일 아틀라스에서 tile id → 서브렉트. M2-C 최소: srcUV(정규화)를 그대로 쓰되
        // tile당 UV는 상위(추출)가 srcUV로 넘기지 않으므로 전체 텍스처(0..1) 사용(플레이스홀더).
        // 실제 아틀라스 서브렉트 매핑은 04 타일셋 에셋 연동 시 확장.
        const float u0 = it.srcUV.x, v0 = it.srcUV.y;
        const float u1 = it.srcUV.x + it.srcUV.w, v1 = it.srcUV.y + it.srcUV.h;

        auto push = [&](float px, float py, float pz, float pu, float pv) {
            m_scratchQuads.push_back({px, py, pz, pu, pv,
                                      white.r, white.g, white.b, white.a, 0.0f, 0.0f});
        };
        // v0(상단 UV)↔y1(월드 상단). 상단 정점은 zTop, 하단 정점은 zBottom(램프).
        push(x0, y1, zTop,    u0, v0);
        push(x0, y0, zBottom, u0, v1);
        push(x1, y1, zTop,    u1, v0);
        push(x1, y1, zTop,    u1, v0);
        push(x0, y0, zBottom, u0, v1);
        push(x1, y0, zBottom, u1, v1);
        ++m_stats.tileQuadCount;
    });
}

} // namespace mye::render
