// mye/render/SpriteBatch.cpp — 스프라이트 배처 구현 (docs/02 §스프라이트 배칭·도트 셰이더)
#include "mye/render/SpriteBatch.h"

#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"
#include "mye/rhi/ShaderCompiler.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace mye::render {
namespace {

constexpr const char* kLog = "SpriteBatch";

// 초기 정점 버퍼 용량(스프라이트 수). 부족하면 2배씩 재할당.
constexpr uint32_t kInitialSprites = 2048;
constexpr uint32_t kVertsPerSprite = 6;   // 두 삼각형

// 스프라이트 uber-shader (M1: 틴트 + 히트 플래시(화이트 블렌드)).
// premultiplied 파이프라인: 텍스처는 premultiplied RGBA. 틴트는 straight color로 받아
// 셰이더에서 premultiply. 플래시는 알파에 비례한 화이트로 lerp(premultiplied 유지).
constexpr const char* kSpriteHlsl = R"hlsl(
cbuffer FrameConstants : register(b0) {
    row_major float4x4 gViewProj;
};

Texture2D    gTex     : register(t0);
SamplerState gSampler : register(s0);

struct VsIn {
    float2 pos   : POSITION;    // 월드(unit)
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;      // straight 틴트 rgba
    float  flash : TEXCOORD1;   // 히트 플래시 강도 0..1
};
struct VsOut {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    float  flash : TEXCOORD1;
};

VsOut vs_main(VsIn i) {
    VsOut o;
    o.pos   = mul(float4(i.pos, 0.0, 1.0), gViewProj);
    o.uv    = i.uv;
    o.color = i.color;
    o.flash = i.flash;
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    float4 texel = gTex.Sample(gSampler, i.uv);   // premultiplied RGBA
    // straight 틴트 → premultiplied 곱: rgb *= tint.rgb*tint.a, a *= tint.a
    float4 tint = i.color;
    float4 outc = texel * float4(tint.rgb * tint.a, tint.a);
    // 히트 플래시: 알파에 비례한 화이트로 블렌드(premultiplied 유지, 알파 보존)
    outc.rgb = lerp(outc.rgb, outc.a.xxx, saturate(i.flash));
    return outc;
}
)hlsl";

rhi::ShaderHandle CompileStage(rhi::IDevice& dev, rhi::ShaderStage stage,
                               const char* entry, const char* name) {
    rhi::ShaderCompileDesc d{};
    d.source = kSpriteHlsl;
    d.entryPoint = entry;
    d.stage = stage;
    d.debugName = name;
    auto bc = rhi::CompileShaderFxc(d);
    if (!bc) {
        MYE_LOG_FATAL(kLog, "sprite shader compile failed ({}): {}", name, bc.GetError().message);
        return {};
    }
    return dev.CreateShader(stage, bc.Value().data);
}

} // namespace

void SpriteBatch::Init(rhi::IDevice& device, rhi::Format colorFormat, bool depthEnabled) {
    if (m_initialized) return;
    m_device = &device;
    namespace rhi = mye::rhi;

    // 셰이더
    m_vs = CompileStage(device, rhi::ShaderStage::Vertex, "vs_main", "sprite.vs");
    m_ps = CompileStage(device, rhi::ShaderStage::Pixel,  "ps_main", "sprite.ps");

    // 상수 버퍼(b0: viewProj). 동적(WriteDiscardRing)으로 매 프레임 갱신.
    rhi::BufferDesc cbDesc{};
    cbDesc.size = sizeof(Mat4);
    cbDesc.usage = rhi::BufferUsage::Uniform;
    cbDesc.access = rhi::CpuAccess::WriteDiscardRing;
    cbDesc.debugName = "sprite.frameCB";
    m_constantBuffer = device.CreateBuffer(cbDesc, nullptr);

    // 샘플러(포인트·클램프 — 픽셀아트)
    rhi::SamplerDesc smpDesc{};
    smpDesc.filter = rhi::Filter::Point;
    smpDesc.u = smpDesc.v = smpDesc.w = rhi::AddressMode::Clamp;
    smpDesc.debugName = "sprite.sampler";
    m_sampler = device.CreateSampler(smpDesc);

    // BindGroupLayout: slot0(PerFrame) b0=viewProj(VS), slot2(PerMaterial) t0+s0(PS)
    const rhi::BindGroupLayoutEntry frameEntries[] = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStageFlags::VertexBit},
    };
    rhi::BindGroupLayoutDesc frameLayoutDesc{};
    frameLayoutDesc.entries = frameEntries;
    frameLayoutDesc.debugName = "sprite.frameLayout";
    m_frameLayout = device.CreateBindGroupLayout(frameLayoutDesc);

    const rhi::BindGroupLayoutEntry matEntries[] = {
        {0, rhi::BindingType::SampledTexture, rhi::ShaderStageFlags::PixelBit},
        {0, rhi::BindingType::Sampler,        rhi::ShaderStageFlags::PixelBit},
    };
    rhi::BindGroupLayoutDesc matLayoutDesc{};
    matLayoutDesc.entries = matEntries;
    matLayoutDesc.debugName = "sprite.materialLayout";
    m_materialLayout = device.CreateBindGroupLayout(matLayoutDesc);

    // PerFrame bind group(상수 버퍼 고정)
    rhi::BindGroupEntry fbEntry{};
    fbEntry.binding = 0;
    fbEntry.type = rhi::BindingType::UniformBuffer;
    fbEntry.buffer = m_constantBuffer;
    rhi::BindGroupDesc fbDesc{};
    fbDesc.layout = m_frameLayout;
    fbDesc.entries = {&fbEntry, 1};
    fbDesc.debugName = "sprite.frameBindGroup";
    m_frameBindGroup = device.CreateBindGroup(fbDesc);

    // 파이프라인 정점 레이아웃. flash는 스칼라이나 R32Float 포맷이 없어 TEXCOORD1을
    // RG32Float(x=flash, y=패딩)로 선언한다(Vertex.flash 뒤 4바이트 여유 필요 없음 —
    // 마지막 필드이지만 stride는 sizeof(Vertex)라 다음 정점 첫 4바이트를 y로 읽지 않도록
    // 아래에서 Vertex에 flashPad를 두어 안전하게 처리).
    const rhi::VertexAttribute attrs2[] = {
        {"POSITION", 0, rhi::Format::RG32Float,   offsetof(Vertex, x),     0},
        {"TEXCOORD", 0, rhi::Format::RG32Float,   offsetof(Vertex, u),     0},
        {"COLOR",    0, rhi::Format::RGBA32Float, offsetof(Vertex, r),     0},
        {"TEXCOORD", 1, rhi::Format::RG32Float,   offsetof(Vertex, flash), 0},
    };

    const rhi::BindGroupLayoutHandle layouts[] = {m_frameLayout, {}, m_materialLayout};

    rhi::GraphicsPipelineDesc pDesc{};
    pDesc.vs = m_vs;
    pDesc.ps = m_ps;
    pDesc.vertexLayout.attributes = attrs2;
    pDesc.vertexLayout.stride = sizeof(Vertex);
    pDesc.topology = rhi::PrimitiveTopology::TriangleList;
    pDesc.blend = rhi::BlendStateDesc::Premultiplied();
    pDesc.raster.cull = rhi::CullMode::None;
    pDesc.depthStencil.depthTest = false;      // M1: Y-sort(CPU)만, 깊이 트릭 없음
    pDesc.depthStencil.depthWrite = false;
    pDesc.rtFormats.colorCount = 1;
    pDesc.rtFormats.colorFormats[0] = colorFormat;
    pDesc.rtFormats.depthStencilFormat =
        depthEnabled ? rhi::Format::D24UnormS8Uint : rhi::Format::Unknown;
    pDesc.bindGroupLayouts = layouts;
    pDesc.debugName = "sprite.pso";
    m_pipeline = device.CreateGraphicsPipeline(pDesc);

    // 초기 정점 버퍼
    EnsureCapacity(kInitialSprites);

    m_pending.reserve(kInitialSprites);
    m_initialized = m_pipeline.IsValid() && m_constantBuffer.IsValid() &&
                    m_vertexBuffer.IsValid() && m_frameBindGroup.IsValid();
    if (!m_initialized) {
        MYE_LOG_ERROR(kLog, "SpriteBatch::Init failed (resource creation error)");
    }
}

void SpriteBatch::EnsureCapacity(uint32_t spriteCount) {
    if (spriteCount <= m_vbCapacitySprites) return;
    uint32_t cap = m_vbCapacitySprites ? m_vbCapacitySprites : kInitialSprites;
    while (cap < spriteCount) cap *= 2;

    if (m_vertexBuffer.IsValid()) m_device->Destroy(m_vertexBuffer);

    namespace rhi = mye::rhi;
    rhi::BufferDesc vbDesc{};
    vbDesc.size = static_cast<uint64_t>(cap) * kVertsPerSprite * sizeof(Vertex);
    vbDesc.usage = rhi::BufferUsage::Vertex;
    vbDesc.access = rhi::CpuAccess::WriteDiscardRing;
    vbDesc.debugName = "sprite.vb";
    m_vertexBuffer = m_device->CreateBuffer(vbDesc, nullptr);
    m_vbCapacitySprites = cap;
}

void SpriteBatch::Shutdown() {
    if (!m_device) return;
    auto& d = *m_device;
    if (m_pipeline.IsValid())       d.Destroy(m_pipeline);
    if (m_frameBindGroup.IsValid()) d.Destroy(m_frameBindGroup);
    if (m_materialLayout.IsValid()) d.Destroy(m_materialLayout);
    if (m_frameLayout.IsValid())    d.Destroy(m_frameLayout);
    if (m_ps.IsValid())             d.Destroy(m_ps);
    if (m_vs.IsValid())             d.Destroy(m_vs);
    if (m_sampler.IsValid())        d.Destroy(m_sampler);
    if (m_constantBuffer.IsValid()) d.Destroy(m_constantBuffer);
    if (m_vertexBuffer.IsValid())   d.Destroy(m_vertexBuffer);
    m_pipeline = {}; m_frameBindGroup = {}; m_materialLayout = {}; m_frameLayout = {};
    m_ps = {}; m_vs = {}; m_sampler = {}; m_constantBuffer = {}; m_vertexBuffer = {};
    m_vbCapacitySprites = 0;
    m_pending.clear();
    m_initialized = false;
    m_device = nullptr;
}

void SpriteBatch::Begin(const Mat4& viewProj) {
    m_viewProj = viewProj;
    m_pending.clear();
    m_stats = {};
}

void SpriteBatch::Submit(const SpriteDraw& draw) {
    if (!draw.texture.IsValid()) return;
    m_pending.push_back(draw);
}

void SpriteBatch::End(rhi::ICommandContext& ctx) {
    m_stats = {};
    if (!m_initialized || m_pending.empty()) return;

    // 1) CPU Y-sort — 안정 정렬(sortY 오름차순: 작은 Y = 뒤, 큰 Y = 앞 은 소비자 규약이므로
    //    여기서는 sortY 오름차순으로 그린다. 발밑 Y가 클수록 화면 아래=앞 → 나중에 그림).
    std::stable_sort(m_pending.begin(), m_pending.end(),
                     [](const SpriteDraw& a, const SpriteDraw& b) { return a.sortY < b.sortY; });

    const uint32_t spriteCount = static_cast<uint32_t>(m_pending.size());
    EnsureCapacity(spriteCount);

    // 2) 정점 채우기 (동적 버퍼 맵)
    const uint64_t bytes = static_cast<uint64_t>(spriteCount) * kVertsPerSprite * sizeof(Vertex);
    void* mapped = ctx.MapDynamic(m_vertexBuffer, bytes);
    if (!mapped) {
        MYE_LOG_ERROR(kLog, "SpriteBatch::End: MapDynamic failed");
        return;
    }
    Vertex* vtx = static_cast<Vertex*>(mapped);

    for (const SpriteDraw& s : m_pending) {
        // 정규화 UV(좌상단 원점, +V 아래).
        const float u0 = s.uv.x;
        const float v0 = s.uv.y;
        const float u1 = s.uv.x + s.uv.w;
        const float v1 = s.uv.y + s.uv.h;

        // 월드 크기(unit). 0,0이면 1×1 unit(=1타일) 기본.
        Vec2 sizeUnit = s.size;
        if (sizeUnit.x == 0.0f && sizeUnit.y == 0.0f) sizeUnit = {1.0f, 1.0f};

        // 정규화 피벗(0..1). position이 피벗 지점에 놓임. pivot.y는 +V(아래) 기준.
        const float left   = s.position.x - s.pivot.x * sizeUnit.x;
        const float right  = left + sizeUnit.x;
        // 월드 +Y 위: 상단(top)은 pivot 위쪽 (pivot.y*h). pivot.y=1(하단)이면 top=position.y+h.
        const float top    = s.position.y + s.pivot.y * sizeUnit.y;
        const float bottom = top - sizeUnit.y;

        const Color c = s.tint;
        const float f = s.flashAmount;

        auto put = [&](Vertex& out, float px, float py, float pu, float pv) {
            out.x = px; out.y = py; out.u = pu; out.v = pv;
            out.r = c.r; out.g = c.g; out.b = c.b; out.a = c.a;
            out.flash = f;
        };
        // 두 삼각형(좌상,좌하,우상 / 우상,좌하,우하). v0=상단(top 월드), v1=하단(bottom).
        put(vtx[0], left,  top,    u0, v0);
        put(vtx[1], left,  bottom, u0, v1);
        put(vtx[2], right, top,    u1, v0);
        put(vtx[3], right, top,    u1, v0);
        put(vtx[4], left,  bottom, u0, v1);
        put(vtx[5], right, bottom, u1, v1);
        vtx += kVertsPerSprite;
    }
    ctx.Unmap(m_vertexBuffer);

    // 3) 상수 버퍼(viewProj) 갱신
    if (void* cb = ctx.MapDynamic(m_constantBuffer, sizeof(Mat4))) {
        std::memcpy(cb, &m_viewProj, sizeof(Mat4));
        ctx.Unmap(m_constantBuffer);
    }

    // 4) 파이프라인·프레임 바인드 그룹
    ctx.SetPipeline(m_pipeline);
    ctx.SetBindGroup(rhi::kBindGroupSlotPerFrame, m_frameBindGroup);
    ctx.SetVertexBuffer(0, m_vertexBuffer, 0);

    // 5) 텍스처가 바뀌는 지점마다 드로우콜 분할(정렬 유지). 텍스처별 머티리얼 바인드 그룹은
    //    임시 생성(M1 단순화 — 텍스처 수가 적다고 가정). 프레임 지연 파괴 큐로 정리.
    uint32_t runStart = 0;
    rhi::TextureHandle runTex = m_pending[0].texture;

    auto flushRun = [&](uint32_t start, uint32_t end, rhi::TextureHandle tex) {
        if (end <= start) return;
        // 머티리얼 바인드 그룹(t0=텍스처, s0=샘플러)
        rhi::BindGroupEntry mat[2];
        mat[0].binding = 0; mat[0].type = rhi::BindingType::SampledTexture; mat[0].texture = tex;
        mat[1].binding = 0; mat[1].type = rhi::BindingType::Sampler;        mat[1].sampler = m_sampler;
        rhi::BindGroupDesc bgd{};
        bgd.layout = m_materialLayout;
        bgd.entries = {mat, 2};
        bgd.debugName = "sprite.matBG";
        rhi::BindGroupHandle bg = m_device->CreateBindGroup(bgd);
        ctx.SetBindGroup(rhi::kBindGroupSlotPerMaterial, bg);
        ctx.Draw((end - start) * kVertsPerSprite, start * kVertsPerSprite);
        m_device->Destroy(bg);   // 지연 파괴 큐 — 이번 프레임 GPU 사용 후 안전 해제
        ++m_stats.drawCalls;
    };

    for (uint32_t i = 1; i < spriteCount; ++i) {
        if (!(m_pending[i].texture == runTex)) {
            flushRun(runStart, i, runTex);
            runStart = i;
            runTex = m_pending[i].texture;
        }
    }
    flushRun(runStart, spriteCount, runTex);
    m_stats.sprites = spriteCount;
}

uint32_t SpriteBatch::DrawCallCount() const { return m_stats.drawCalls; }
uint32_t SpriteBatch::QuadCount() const { return m_stats.sprites; }

} // namespace mye::render
