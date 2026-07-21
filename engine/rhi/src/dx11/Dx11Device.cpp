// Dx11Device.cpp — DX11 백엔드 구현 (docs/02 "DX11 백엔드 구현 전략")
//
//  - D3D11CreateDevice(FL 11_0, Debug 빌드 시 디버그 레이어 — 미설치 시 폴백)
//  - 핸들 풀(세대 검증) + kMaxFramesInFlight 프레임 지연 파괴 큐
//  - 상태 객체 캐시(블렌드·뎁스·래스터) + SetPipeline 분해 적용 + 상태 섀도잉
//  - DXGI FLIP_DISCARD 스왑체인 (sRGB는 RTV 뷰로 처리, vsync/티어링 토글)
//  - 종료 시 ReportLiveDeviceObjects (디버그 레이어 활성 시)
//  - CaptureBackbuffer: 스테이징 복사 → 32bpp BMP 저장 (M0 통합 자동 검증용)
#include "Dx11Device.h"

#include "mye/core/Log.h"

#include <d3d11sdklayers.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>

namespace mye::rhi {

Expected<std::unique_ptr<IDevice>, Error> CreateDevice(Backend backend, const DeviceCreateInfo& info) {
    switch (backend) {
    case Backend::DX11: {
        auto result = dx11::Dx11Device::Create(info);
        if (!result) return result.GetError();
        return std::unique_ptr<IDevice>(std::move(result.Value()));
    }
    }
    return Error{"unknown RHI backend", 1};
}

Expected<void, Error> CaptureBackbuffer(IDevice& device, TextureHandle backbuffer,
                                        std::string_view utf8BmpPath) {
    // 백엔드는 현재 DX11 단일(컴파일 타임 내장, docs/02) — CreateDevice가 만드는 구체 타입은 항상
    // Dx11Device다. 백엔드가 추가되면 백엔드별 내부 캡처 훅으로 이관한다. (dynamic_cast 금지 규약)
    return static_cast<dx11::Dx11Device&>(device).CaptureTextureToBmp(backbuffer, utf8BmpPath);
}

} // namespace mye::rhi

namespace mye::rhi::dx11 {

// ---------------------------------------------------------------------------
// 파일 로컬 유틸
// ---------------------------------------------------------------------------
namespace {

constexpr std::string_view kLog = "RHI";

// UTF-8 → UTF-16 (win32 W-API 경계 전용). core의 Widen은 App.cpp 스텁이라 자체 구현을 쓴다.
std::wstring WidenUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                              static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring out(static_cast<size_t>(wideLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), wideLen);
    return out;
}

std::string NarrowUtf16(const wchar_t* utf16) {
    if (!utf16 || !*utf16) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, utf16, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// 상태 객체 캐시 키 — 필드 단위 해시 (패딩 바이트 오염 방지)
struct FnvHasher {
    uint64_t hash = 14695981039346656037ull;
    void Bytes(const void* data, size_t size) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
    }
    template <typename T>
    void Value(const T& v) { Bytes(&v, sizeof(v)); }
};

DXGI_FORMAT ToDxgiFormat(Format f) {
    switch (f) {
    case Format::RGBA8Unorm:     return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8Unorm:     return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8UnormSrgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::R8Unorm:        return DXGI_FORMAT_R8_UNORM;
    case Format::R32Uint:        return DXGI_FORMAT_R32_UINT;
    case Format::RG32Float:      return DXGI_FORMAT_R32G32_FLOAT;
    case Format::RGB32Float:     return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::RGBA32Float:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::RGBA16Float:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::D24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32Float:       return DXGI_FORMAT_D32_FLOAT;
    case Format::Unknown:        return DXGI_FORMAT_UNKNOWN;
    default:                     return DXGI_FORMAT_UNKNOWN;
    }
}

// FLIP 모델 스왑체인은 sRGB 포맷 직접 생성 불가 — 버퍼는 UNORM, RTV만 sRGB로 뷰잉한다.
Format StripSrgb(Format f) {
    switch (f) {
    case Format::RGBA8UnormSrgb: return Format::RGBA8Unorm;
    case Format::BGRA8UnormSrgb: return Format::BGRA8Unorm;
    default:                     return f;
    }
}

D3D11_BLEND ToD3DBlend(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero:        return D3D11_BLEND_ZERO;
    case BlendFactor::One:         return D3D11_BLEND_ONE;
    case BlendFactor::SrcAlpha:    return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DstAlpha:    return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::SrcColor:    return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
    default:                       return D3D11_BLEND_ONE;
    }
}

D3D11_BLEND_OP ToD3DBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:         return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:    return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:         return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:         return D3D11_BLEND_OP_MAX;
    default:                   return D3D11_BLEND_OP_ADD;
    }
}

D3D11_COMPARISON_FUNC ToD3DCompare(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never:        return D3D11_COMPARISON_NEVER;
    case CompareFunc::Less:         return D3D11_COMPARISON_LESS;
    case CompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
    case CompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case CompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
    case CompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
    case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareFunc::Always:       return D3D11_COMPARISON_ALWAYS;
    default:                        return D3D11_COMPARISON_ALWAYS;
    }
}

D3D11_CULL_MODE ToD3DCull(CullMode m) {
    switch (m) {
    case CullMode::None:  return D3D11_CULL_NONE;
    case CullMode::Front: return D3D11_CULL_FRONT;
    case CullMode::Back:  return D3D11_CULL_BACK;
    default:              return D3D11_CULL_NONE;
    }
}

D3D11_FILL_MODE ToD3DFill(FillMode m) {
    return m == FillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
}

D3D11_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology t) {
    switch (t) {
    case PrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    default:                               return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

D3D11_TEXTURE_ADDRESS_MODE ToD3DAddress(AddressMode m) {
    switch (m) {
    case AddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
    case AddressMode::Wrap:   return D3D11_TEXTURE_ADDRESS_WRAP;
    case AddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
    case AddressMode::Border: return D3D11_TEXTURE_ADDRESS_BORDER;
    default:                  return D3D11_TEXTURE_ADDRESS_CLAMP;
    }
}

bool HasStage(ShaderStageFlags v, ShaderStageFlags f) {
    return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}

void SetDebugName(ID3D11DeviceChild* obj, const char* name) {
    if (obj && name && name[0] != '\0')
        obj->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
}

const char* SafeName(const char* name) { return name ? name : ""; }

// 지연 파괴 큐에 보관할 COM 참조 수집 (캐시 공유 상태 객체는 AddRef 사본 — 캐시 소유는 유지)
void CollectComRefs(BufferResource& r, std::vector<ComPtr<IUnknown>>& out) {
    if (r.buffer) out.emplace_back(r.buffer);
}
void CollectComRefs(TextureResource& r, std::vector<ComPtr<IUnknown>>& out) {
    if (r.texture) out.emplace_back(r.texture);
    if (r.rtv) out.emplace_back(r.rtv);
    if (r.dsv) out.emplace_back(r.dsv);
    if (r.srv) out.emplace_back(r.srv);
}
void CollectComRefs(SamplerResource& r, std::vector<ComPtr<IUnknown>>& out) {
    if (r.state) out.emplace_back(r.state);
}
void CollectComRefs(ShaderResource& r, std::vector<ComPtr<IUnknown>>& out) {
    if (r.vs) out.emplace_back(r.vs);
    if (r.ps) out.emplace_back(r.ps);
}
void CollectComRefs(BindGroupLayoutResource&, std::vector<ComPtr<IUnknown>>&) {}
void CollectComRefs(BindGroupResource&, std::vector<ComPtr<IUnknown>>&) {}
void CollectComRefs(PipelineResource& r, std::vector<ComPtr<IUnknown>>& out) {
    if (r.vs) out.emplace_back(r.vs);
    if (r.ps) out.emplace_back(r.ps);
    if (r.inputLayout) out.emplace_back(r.inputLayout);
    if (r.blend) out.emplace_back(r.blend);
    if (r.depthStencil) out.emplace_back(r.depthStencil);
    if (r.rasterizer) out.emplace_back(r.rasterizer);
}

} // namespace

// ---------------------------------------------------------------------------
// Dx11CommandContext
// ---------------------------------------------------------------------------
void Dx11CommandContext::Initialize(Dx11Device* device, ID3D11DeviceContext* ctx) {
    m_device = device;
    m_ctx = ctx;
    if (FAILED(ctx->QueryInterface(IID_PPV_ARGS(m_annotation.ReleaseAndGetAddressOf()))))
        m_annotation.Reset();
}

void Dx11CommandContext::ResetShadowState() {
    m_currentPipeline = {};
    m_vertexStride = 0;
    m_vb = {};
    m_lastVs = nullptr;
    m_lastPs = nullptr;
    m_lastInputLayout = nullptr;
    m_lastBlend = nullptr;
    m_lastDepthStencil = nullptr;
    m_lastRasterizer = nullptr;
    m_lastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void Dx11CommandContext::UnbindRenderTargets() {
    if (m_ctx) m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
}

void Dx11CommandContext::BeginRenderPass(const RenderPassBeginDesc& desc) {
    if (m_inRenderPass) {
        MYE_LOG_ERROR(kLog, "BeginRenderPass called inside an open render pass");
        return;
    }

    ID3D11RenderTargetView* rtvs[kMaxColorAttachments] = {};
    uint32_t colorCount = static_cast<uint32_t>(desc.colorAttachments.size());
    if (colorCount > kMaxColorAttachments) {
        MYE_LOG_ERROR(kLog, "BeginRenderPass: too many color attachments ({}) — clamped to {}",
                      colorCount, kMaxColorAttachments);
        colorCount = kMaxColorAttachments;
    }

    uint32_t rtWidth = 0, rtHeight = 0;
    for (uint32_t i = 0; i < colorCount; ++i) {
        const RenderPassColorAttachment& att = desc.colorAttachments[i];
        TextureResource* tex = m_device->m_textures.Get(att.texture);
        if (!tex || !tex->rtv) {
            MYE_LOG_ERROR(kLog, "BeginRenderPass: color attachment {} has no valid RTV", i);
            continue;
        }
        rtvs[i] = tex->rtv.Get();
        if (rtWidth == 0) { rtWidth = tex->width; rtHeight = tex->height; }
        if (att.loadOp == LoadOp::Clear)
            m_ctx->ClearRenderTargetView(rtvs[i], &att.clearColor.r);
    }

    ID3D11DepthStencilView* dsv = nullptr;
    if (desc.depthStencil) {
        TextureResource* tex = m_device->m_textures.Get(desc.depthStencil->texture);
        if (!tex || !tex->dsv) {
            MYE_LOG_ERROR(kLog, "BeginRenderPass: depth attachment has no valid DSV");
        } else {
            dsv = tex->dsv.Get();
            if (rtWidth == 0) { rtWidth = tex->width; rtHeight = tex->height; }
            UINT clearFlags = 0;
            if (desc.depthStencil->depthLoadOp == LoadOp::Clear) clearFlags |= D3D11_CLEAR_DEPTH;
            if (desc.depthStencil->stencilLoadOp == LoadOp::Clear) clearFlags |= D3D11_CLEAR_STENCIL;
            if (clearFlags != 0)
                m_ctx->ClearDepthStencilView(dsv, clearFlags, desc.depthStencil->clearDepth,
                                             desc.depthStencil->clearStencil);
        }
    }

    m_ctx->OMSetRenderTargets(colorCount, rtvs, dsv);

    // 기본 뷰포트·시저 = 첫 어태치먼트 전체 (WebGPU 시맨틱 — SetViewport/SetScissor로 덮어쓰기 가능)
    if (rtWidth > 0) {
        SetViewport(Viewport{0.0f, 0.0f, static_cast<float>(rtWidth), static_cast<float>(rtHeight),
                             0.0f, 1.0f});
        SetScissor(RectInt{0, 0, static_cast<int32_t>(rtWidth), static_cast<int32_t>(rtHeight)});
    }

    if (desc.debugName && m_annotation) {
        m_annotation->BeginEvent(WidenUtf8(desc.debugName).c_str());
        m_passMarkerActive = true;
    }
    m_inRenderPass = true;
}

void Dx11CommandContext::EndRenderPass() {
    if (!m_inRenderPass) {
        MYE_LOG_ERROR(kLog, "EndRenderPass without matching BeginRenderPass");
        return;
    }
    UnbindRenderTargets();
    if (m_passMarkerActive && m_annotation)
        m_annotation->EndEvent();
    m_passMarkerActive = false;
    m_inRenderPass = false;
}

void Dx11CommandContext::SetPipeline(PipelineHandle p) {
    if (p == m_currentPipeline) return;   // 상태 섀도잉 — 중복 SetPipeline 필터

    PipelineResource* pl = m_device->m_pipelines.Get(p);
    if (!pl) {
        MYE_LOG_ERROR(kLog, "SetPipeline: invalid pipeline handle (index={}, gen={})", p.index, p.gen);
        m_currentPipeline = {};
        return;
    }

    if (pl->topology != m_lastTopology) {
        m_ctx->IASetPrimitiveTopology(pl->topology);
        m_lastTopology = pl->topology;
    }
    if (pl->inputLayout.Get() != m_lastInputLayout) {
        m_ctx->IASetInputLayout(pl->inputLayout.Get());
        m_lastInputLayout = pl->inputLayout.Get();
    }
    if (pl->vs.Get() != m_lastVs) {
        m_ctx->VSSetShader(pl->vs.Get(), nullptr, 0);
        m_lastVs = pl->vs.Get();
    }
    if (pl->ps.Get() != m_lastPs) {
        m_ctx->PSSetShader(pl->ps.Get(), nullptr, 0);
        m_lastPs = pl->ps.Get();
    }
    if (pl->blend.Get() != m_lastBlend) {
        const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        m_ctx->OMSetBlendState(pl->blend.Get(), blendFactor, 0xFFFF'FFFFu);
        m_lastBlend = pl->blend.Get();
    }
    if (pl->depthStencil.Get() != m_lastDepthStencil) {
        m_ctx->OMSetDepthStencilState(pl->depthStencil.Get(), 0);
        m_lastDepthStencil = pl->depthStencil.Get();
    }
    if (pl->rasterizer.Get() != m_lastRasterizer) {
        m_ctx->RSSetState(pl->rasterizer.Get());
        m_lastRasterizer = pl->rasterizer.Get();
    }
    if (pl->vertexStride != m_vertexStride) {
        m_vertexStride = pl->vertexStride;
        if (m_vb.buffer.IsValid()) m_vb.dirty = true;   // 새 스트라이드로 재바인딩
    }
    m_currentPipeline = p;
}

void Dx11CommandContext::SetBindGroup(uint32_t slot, BindGroupHandle g,
                                      std::span<const uint32_t> dynamicOffsets) {
    if (slot >= kMaxBindGroups) {
        MYE_LOG_ERROR(kLog, "SetBindGroup: slot {} out of range (max {})", slot, kMaxBindGroups - 1);
        return;
    }
    // DX11은 레지스터가 BindGroup 생성 시 이미 구워져 있어 슬롯은 규약(0=PerFrame..3=PerDraw) 검증 용도.
    if (!dynamicOffsets.empty() && !m_warnedDynamicOffsets) {
        MYE_LOG_WARN(kLog, "SetBindGroup: dynamic offsets are not supported in M0 (ignored)");
        m_warnedDynamicOffsets = true;
    }

    BindGroupResource* group = m_device->m_bindGroups.Get(g);
    if (!group) {
        MYE_LOG_ERROR(kLog, "SetBindGroup: invalid bind group handle (slot={})", slot);
        return;
    }

    for (const BindGroupResource::CbvBinding& cbv : group->cbvs) {
        BufferResource* buf = m_device->m_buffers.Get(cbv.buffer);
        if (!buf) {
            MYE_LOG_ERROR(kLog, "SetBindGroup: stale uniform buffer at register b{}", cbv.reg);
            continue;
        }
        ID3D11Buffer* raw = buf->buffer.Get();
        if (HasStage(cbv.vis, ShaderStageFlags::VertexBit)) m_ctx->VSSetConstantBuffers(cbv.reg, 1, &raw);
        if (HasStage(cbv.vis, ShaderStageFlags::PixelBit))  m_ctx->PSSetConstantBuffers(cbv.reg, 1, &raw);
    }
    for (const BindGroupResource::SrvBinding& srv : group->srvs) {
        TextureResource* tex = m_device->m_textures.Get(srv.texture);
        if (!tex || !tex->srv) {
            MYE_LOG_ERROR(kLog, "SetBindGroup: stale/unsampleable texture at register t{}", srv.reg);
            continue;
        }
        ID3D11ShaderResourceView* raw = tex->srv.Get();
        if (HasStage(srv.vis, ShaderStageFlags::VertexBit)) m_ctx->VSSetShaderResources(srv.reg, 1, &raw);
        if (HasStage(srv.vis, ShaderStageFlags::PixelBit))  m_ctx->PSSetShaderResources(srv.reg, 1, &raw);
    }
    for (const BindGroupResource::SmpBinding& smp : group->smps) {
        SamplerResource* sampler = m_device->m_samplers.Get(smp.sampler);
        if (!sampler) {
            MYE_LOG_ERROR(kLog, "SetBindGroup: stale sampler at register s{}", smp.reg);
            continue;
        }
        ID3D11SamplerState* raw = sampler->state.Get();
        if (HasStage(smp.vis, ShaderStageFlags::VertexBit)) m_ctx->VSSetSamplers(smp.reg, 1, &raw);
        if (HasStage(smp.vis, ShaderStageFlags::PixelBit))  m_ctx->PSSetSamplers(smp.reg, 1, &raw);
    }
}

void Dx11CommandContext::SetVertexBuffer(uint32_t slot, BufferHandle b, uint64_t offset) {
    if (slot != 0) {
        MYE_LOG_ERROR(kLog, "SetVertexBuffer: only slot 0 is supported in M0 (got {})", slot);
        return;
    }
    if (!m_device->m_buffers.Get(b)) {
        MYE_LOG_ERROR(kLog, "SetVertexBuffer: invalid buffer handle");
        return;
    }
    // 스트라이드는 파이프라인 소유 — 실제 IASetVertexBuffers는 드로우 직전 FlushInputAssembly에서
    // (SetPipeline/SetVertexBuffer 호출 순서 무관하게 동작)
    m_vb.buffer = b;
    m_vb.offset = offset;
    m_vb.dirty = true;
}

void Dx11CommandContext::SetIndexBuffer(BufferHandle b, IndexFormat fmt) {
    BufferResource* buf = m_device->m_buffers.Get(b);
    if (!buf) {
        MYE_LOG_ERROR(kLog, "SetIndexBuffer: invalid buffer handle");
        return;
    }
    m_ctx->IASetIndexBuffer(buf->buffer.Get(),
                            fmt == IndexFormat::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT,
                            0);
}

void Dx11CommandContext::SetViewport(const Viewport& vp) {
    D3D11_VIEWPORT d3dVp{};
    d3dVp.TopLeftX = vp.x;
    d3dVp.TopLeftY = vp.y;
    d3dVp.Width = vp.width;
    d3dVp.Height = vp.height;
    d3dVp.MinDepth = vp.minDepth;
    d3dVp.MaxDepth = vp.maxDepth;
    m_ctx->RSSetViewports(1, &d3dVp);
}

void Dx11CommandContext::SetScissor(const RectInt& r) {
    D3D11_RECT rect{};
    rect.left = r.x;
    rect.top = r.y;
    rect.right = r.x + r.w;
    rect.bottom = r.y + r.h;
    m_ctx->RSSetScissorRects(1, &rect);
}

void Dx11CommandContext::FlushInputAssembly() {
    if (!m_vb.dirty) return;
    m_vb.dirty = false;
    BufferResource* buf = m_device->m_buffers.Get(m_vb.buffer);
    if (!buf) return;   // Destroy 이후 드로우 — ValidateDraw/드라이버 검증에 맡김
    ID3D11Buffer* raw = buf->buffer.Get();
    const UINT stride = m_vertexStride;
    const UINT offset = static_cast<UINT>(m_vb.offset);
    m_ctx->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
}

bool Dx11CommandContext::ValidateDraw(const char* what) {
    if (!m_inRenderPass) {
        MYE_LOG_ERROR(kLog, "{} called outside a render pass", what);
        return false;
    }
    if (!m_device->m_pipelines.Get(m_currentPipeline)) {
        MYE_LOG_ERROR(kLog, "{} called without a valid pipeline", what);
        return false;
    }
    return true;
}

void Dx11CommandContext::Draw(uint32_t vertexCount, uint32_t firstVertex) {
    if (!ValidateDraw("Draw")) return;
    FlushInputAssembly();
    m_ctx->Draw(vertexCount, firstVertex);
}

void Dx11CommandContext::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex) {
    if (!ValidateDraw("DrawIndexed")) return;
    FlushInputAssembly();
    m_ctx->DrawIndexed(indexCount, firstIndex, baseVertex);
}

void Dx11CommandContext::DrawIndexedInstanced(uint32_t idxCount, uint32_t instCount, uint32_t firstIdx,
                                              int32_t baseVtx, uint32_t firstInst) {
    if (!ValidateDraw("DrawIndexedInstanced")) return;
    FlushInputAssembly();
    m_ctx->DrawIndexedInstanced(idxCount, instCount, firstIdx, baseVtx, firstInst);
}

void* Dx11CommandContext::MapDynamic(BufferHandle b, uint64_t size) {
    BufferResource* buf = m_device->m_buffers.Get(b);
    if (!buf) {
        MYE_LOG_ERROR(kLog, "MapDynamic: invalid buffer handle");
        return nullptr;
    }
    if (buf->access != CpuAccess::WriteDiscardRing) {
        MYE_LOG_ERROR(kLog, "MapDynamic: buffer was not created with CpuAccess::WriteDiscardRing");
        return nullptr;
    }
    if (size > buf->size) {
        MYE_LOG_ERROR(kLog, "MapDynamic: size {} exceeds buffer size {}", size, buf->size);
        return nullptr;
    }
    // M0: 매핑마다 전체 DISCARD. 부분 링 할당(NO_OVERWRITE 오프셋 관리)은 M1 스프라이트 배처에서.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = m_ctx->Map(buf->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "MapDynamic: Map failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return nullptr;
    }
    return mapped.pData;
}

void Dx11CommandContext::Unmap(BufferHandle b) {
    BufferResource* buf = m_device->m_buffers.Get(b);
    if (!buf) {
        MYE_LOG_ERROR(kLog, "Unmap: invalid buffer handle");
        return;
    }
    m_ctx->Unmap(buf->buffer.Get(), 0);
}

void Dx11CommandContext::CopyTexture(TextureHandle dst, TextureHandle src, const TextureCopyRegion& rgn) {
    TextureResource* dstTex = m_device->m_textures.Get(dst);
    TextureResource* srcTex = m_device->m_textures.Get(src);
    if (!dstTex || !srcTex) {
        MYE_LOG_ERROR(kLog, "CopyTexture: invalid texture handle");
        return;
    }
    const UINT dstSub = D3D11CalcSubresource(rgn.mipLevel, rgn.arrayLayer, dstTex->mips);
    const UINT srcSub = D3D11CalcSubresource(rgn.mipLevel, rgn.arrayLayer, srcTex->mips);
    if (rgn.width == 0) {   // width 0 = 전체 서브리소스 복사
        m_ctx->CopySubresourceRegion(dstTex->texture.Get(), dstSub, 0, 0, 0,
                                     srcTex->texture.Get(), srcSub, nullptr);
    } else {
        D3D11_BOX box{rgn.x, rgn.y, rgn.z, rgn.x + rgn.width, rgn.y + rgn.height, rgn.z + rgn.depth};
        m_ctx->CopySubresourceRegion(dstTex->texture.Get(), dstSub, rgn.x, rgn.y, rgn.z,
                                     srcTex->texture.Get(), srcSub, &box);
    }
}

void Dx11CommandContext::WriteTexture(TextureHandle dst, const TextureCopyRegion& rgn,
                                      const void* srcData, uint32_t srcRowPitch) {
    TextureResource* dstTex = m_device->m_textures.Get(dst);
    if (!dstTex || !srcData) {
        MYE_LOG_ERROR(kLog, "WriteTexture: invalid texture handle or null data");
        return;
    }
    // DX11 매핑: UpdateSubresource (docs/02 — 글리프 아틀라스 R8·팔레트 LUT·타일 청크 갱신 경로)
    const UINT sub = D3D11CalcSubresource(rgn.mipLevel, rgn.arrayLayer, dstTex->mips);
    if (rgn.width == 0) {
        m_ctx->UpdateSubresource(dstTex->texture.Get(), sub, nullptr, srcData, srcRowPitch, 0);
    } else {
        D3D11_BOX box{rgn.x, rgn.y, rgn.z, rgn.x + rgn.width, rgn.y + rgn.height, rgn.z + rgn.depth};
        m_ctx->UpdateSubresource(dstTex->texture.Get(), sub, &box, srcData, srcRowPitch, 0);
    }
}

void Dx11CommandContext::CopyTextureToBuffer(BufferHandle /*dst*/, TextureHandle /*src*/,
                                             const TextureCopyRegion& /*rgn*/) {
    // M2: 리드백 준비 (ID 픽킹·썸네일 캡처) — EnqueueReadback/TryGetReadback과 함께 구현
}

void Dx11CommandContext::WriteTimestamp(uint32_t /*queryIndex*/) {
    // M2: D3D11_QUERY_TIMESTAMP + 프레임당 TIMESTAMP_DISJOINT 쌍 (07 프로파일러 GPU 탭)
}

void Dx11CommandContext::PushDebugMarker(const char* name) {
    if (m_annotation && name)
        m_annotation->BeginEvent(WidenUtf8(name).c_str());
}

void Dx11CommandContext::PopDebugMarker() {
    if (m_annotation)
        m_annotation->EndEvent();
}

// ---------------------------------------------------------------------------
// Dx11Device — 생성·파괴
// ---------------------------------------------------------------------------
Expected<std::unique_ptr<Dx11Device>, Error> Dx11Device::Create(const DeviceCreateInfo& info) {
    auto device = std::unique_ptr<Dx11Device>(new Dx11Device());

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (info.enableDebugLayer)
        flags |= D3D11_CREATE_DEVICE_DEBUG;

    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL obtained{};
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     &requested, 1, D3D11_SDK_VERSION,
                                     device->m_device.ReleaseAndGetAddressOf(), &obtained,
                                     device->m_context.ReleaseAndGetAddressOf());
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        MYE_LOG_WARN(kLog,
                     "D3D11 debug layer unavailable (hr=0x{:08X}) — retrying without it. "
                     "Install the 'Graphics Tools' optional feature to enable it.",
                     static_cast<uint32_t>(hr));
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 &requested, 1, D3D11_SDK_VERSION,
                                 device->m_device.ReleaseAndGetAddressOf(), &obtained,
                                 device->m_context.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr))
        return Error{std::format("D3D11CreateDevice failed (hr=0x{:08X})", static_cast<uint32_t>(hr)),
                     static_cast<int32_t>(hr)};
    device->m_debugLayerEnabled = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;

    // DXGI 어댑터 → caps + 팩토리
    ComPtr<IDXGIDevice> dxgiDevice;
    if (SUCCEEDED(device->m_device.As(&dxgiDevice))) {
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.ReleaseAndGetAddressOf()))) {
            DXGI_ADAPTER_DESC adapterDesc{};
            if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
                const std::string name = NarrowUtf16(adapterDesc.Description);
                const size_t n = std::min(name.size(), sizeof(device->m_caps.adapterName) - 1);
                std::memcpy(device->m_caps.adapterName, name.data(), n);
                device->m_caps.adapterName[n] = '\0';
                device->m_caps.dedicatedVideoMemoryBytes = adapterDesc.DedicatedVideoMemory;
            }
            adapter->GetParent(IID_PPV_ARGS(device->m_factory.ReleaseAndGetAddressOf()));
        }
    }
    if (!device->m_factory)
        return Error{"failed to obtain DXGI factory from D3D11 device", 1};

    // 티어링(vsync off 무제한 프레임) 지원 조회
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(device->m_factory.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allowTearing, sizeof(allowTearing))))
            device->m_tearingSupported = (allowTearing != FALSE);
    }

    // m_caps.supportsTimestamps: M2에서 D3D11_QUERY_TIMESTAMP 지원 조회와 함께 — M0은 false 유지

    device->m_immediateContext.Initialize(device.get(), device->m_context.Get());

    MYE_LOG_INFO(kLog, "DX11 device created: adapter='{}', featureLevel=0x{:04X}, debugLayer={}, tearing={}",
                 device->m_caps.adapterName, static_cast<uint32_t>(obtained),
                 device->m_debugLayerEnabled, device->m_tearingSupported);
    return device;
}

Dx11Device::~Dx11Device() {
    if (m_context) {
        m_immediateContext.UnbindRenderTargets();
        m_context->ClearState();
    }

    ProcessDestroyQueue(true);   // 지연 큐 강제 소진
    ReportLeaks();

    m_buffers.Clear();
    m_textures.Clear();
    m_samplers.Clear();
    m_shaders.Clear();
    m_bindGroupLayouts.Clear();
    m_bindGroups.Clear();
    m_pipelines.Clear();
    m_blendCache.clear();
    m_depthStencilCache.clear();
    m_rasterizerCache.clear();

    if (m_context) m_context->Flush();

    if (m_debugLayerEnabled && m_device) {
        // 종료 시 GPU 객체 릭 리포트 — 디버거 Output 창(OutputDebugString)에 출력.
        // IGNORE_INTERNAL이어도 디바이스 자신 1건(아래 QI로 살아있음)은 보고되며 이는 정상이다.
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug))) {
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            MYE_LOG_INFO(kLog, "ReportLiveDeviceObjects written to debug output");
        }
    }
}

// ---------------------------------------------------------------------------
// Dx11Device — 리소스 생성
// ---------------------------------------------------------------------------
BufferHandle Dx11Device::CreateBuffer(const BufferDesc& desc, const void* initialData) {
    if (desc.size == 0) {
        MYE_LOG_ERROR(kLog, "CreateBuffer: size must be > 0 ('{}')", SafeName(desc.debugName));
        return {};
    }
    if (HasFlag(desc.usage, BufferUsage::Structured)) {
        // BufferDesc에 구조체 스트라이드가 없어 SRV를 만들 수 없다 — M1(스프라이트 인스턴싱)에서 확장
        MYE_LOG_ERROR(kLog, "CreateBuffer: Structured buffers are M1+ ('{}')", SafeName(desc.debugName));
        return {};
    }

    UINT bindFlags = 0;
    if (HasFlag(desc.usage, BufferUsage::Vertex))  bindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (HasFlag(desc.usage, BufferUsage::Index))   bindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (HasFlag(desc.usage, BufferUsage::Uniform)) bindFlags |= D3D11_BIND_CONSTANT_BUFFER;

    uint64_t size = desc.size;
    if (HasFlag(desc.usage, BufferUsage::Uniform))
        size = (size + 15ull) & ~15ull;   // cbuffer는 16바이트 배수 요구

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(size);
    bd.BindFlags = bindFlags;
    if (desc.access == CpuAccess::WriteDiscardRing) {
        bd.Usage = D3D11_USAGE_DYNAMIC;               // MAP_WRITE_DISCARD 링 (docs/02)
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    } else {
        bd.Usage = D3D11_USAGE_DEFAULT;
    }

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = initialData;
    BufferResource res;
    const HRESULT hr = m_device->CreateBuffer(&bd, initialData ? &init : nullptr,
                                              res.buffer.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateBuffer failed ('{}', size={}, hr=0x{:08X})",
                      SafeName(desc.debugName), desc.size, static_cast<uint32_t>(hr));
        return {};
    }
    SetDebugName(res.buffer.Get(), desc.debugName);
    res.size = size;
    res.usage = desc.usage;
    res.access = desc.access;
    return m_buffers.Allocate(std::move(res));
}

TextureHandle Dx11Device::CreateTexture(const TextureDesc& desc, const TextureInitData* init) {
    if (desc.dim != TextureDim::Tex2D && desc.dim != TextureDim::Tex2DArray) {
        MYE_LOG_ERROR(kLog, "CreateTexture: Tex3D/TexCube are M1+ ('{}')", SafeName(desc.debugName));
        return {};
    }
    if (desc.width == 0 || desc.height == 0) {
        MYE_LOG_ERROR(kLog, "CreateTexture: zero extent ('{}')", SafeName(desc.debugName));
        return {};
    }

    UINT bindFlags = 0;
    if (HasFlag(desc.usage, TextureUsage::Sampled))      bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (HasFlag(desc.usage, TextureUsage::RenderTarget)) bindFlags |= D3D11_BIND_RENDER_TARGET;
    if (HasFlag(desc.usage, TextureUsage::DepthStencil)) bindFlags |= D3D11_BIND_DEPTH_STENCIL;
    // CopySrc/CopyDst: DX11은 별도 바인드 플래그 불필요 (Copy*/UpdateSubresource 자유)

    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = desc.mips;
    td.ArraySize = (desc.dim == TextureDim::Tex2DArray) ? desc.depthOrLayers : 1;
    td.Format = ToDxgiFormat(desc.format);
    td.SampleDesc.Count = static_cast<UINT>(desc.samples);
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = bindFlags;

    D3D11_SUBRESOURCE_DATA initData{};
    const D3D11_SUBRESOURCE_DATA* pInit = nullptr;
    if (init && init->data) {
        if (desc.mips > 1 || td.ArraySize > 1) {
            MYE_LOG_ERROR(kLog, "CreateTexture: initial data for mip/array chains is M1+ ('{}')",
                          SafeName(desc.debugName));
            return {};
        }
        initData.pSysMem = init->data;
        initData.SysMemPitch = init->rowPitch;
        initData.SysMemSlicePitch = init->slicePitch;
        pInit = &initData;
    }

    TextureResource res;
    HRESULT hr = m_device->CreateTexture2D(&td, pInit, res.texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateTexture failed ('{}', {}x{}, hr=0x{:08X})",
                      SafeName(desc.debugName), desc.width, desc.height, static_cast<uint32_t>(hr));
        return {};
    }
    SetDebugName(res.texture.Get(), desc.debugName);

    if ((bindFlags & D3D11_BIND_SHADER_RESOURCE) != 0) {
        hr = m_device->CreateShaderResourceView(res.texture.Get(), nullptr,
                                                res.srv.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            MYE_LOG_ERROR(kLog, "CreateTexture: SRV creation failed ('{}', hr=0x{:08X})",
                          SafeName(desc.debugName), static_cast<uint32_t>(hr));
            return {};
        }
    }
    if ((bindFlags & D3D11_BIND_RENDER_TARGET) != 0) {
        hr = m_device->CreateRenderTargetView(res.texture.Get(), nullptr,
                                              res.rtv.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            MYE_LOG_ERROR(kLog, "CreateTexture: RTV creation failed ('{}', hr=0x{:08X})",
                          SafeName(desc.debugName), static_cast<uint32_t>(hr));
            return {};
        }
    }
    if ((bindFlags & D3D11_BIND_DEPTH_STENCIL) != 0) {
        hr = m_device->CreateDepthStencilView(res.texture.Get(), nullptr,
                                              res.dsv.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            MYE_LOG_ERROR(kLog, "CreateTexture: DSV creation failed ('{}', hr=0x{:08X})",
                          SafeName(desc.debugName), static_cast<uint32_t>(hr));
            return {};
        }
    }

    res.format = desc.format;
    res.width = desc.width;
    res.height = desc.height;
    res.mips = desc.mips;
    res.arrayLayers = static_cast<uint16_t>(td.ArraySize);
    return m_textures.Allocate(std::move(res));
}

SamplerHandle Dx11Device::CreateSampler(const SamplerDesc& desc) {
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = (desc.filter == Filter::Point) ? D3D11_FILTER_MIN_MAG_MIP_POINT
                                               : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = ToD3DAddress(desc.u);
    sd.AddressV = ToD3DAddress(desc.v);
    sd.AddressW = ToD3DAddress(desc.w);
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    SamplerResource res;
    const HRESULT hr = m_device->CreateSamplerState(&sd, res.state.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateSampler failed ('{}', hr=0x{:08X})",
                      SafeName(desc.debugName), static_cast<uint32_t>(hr));
        return {};
    }
    SetDebugName(res.state.Get(), desc.debugName);
    return m_samplers.Allocate(std::move(res));
}

ShaderHandle Dx11Device::CreateShader(ShaderStage stage, std::span<const std::byte> bytecode) {
    if (bytecode.empty()) {
        MYE_LOG_ERROR(kLog, "CreateShader: empty bytecode");
        return {};
    }
    ShaderResource res;
    res.stage = stage;
    HRESULT hr = E_FAIL;
    switch (stage) {
    case ShaderStage::Vertex:
        hr = m_device->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr,
                                          res.vs.ReleaseAndGetAddressOf());
        if (SUCCEEDED(hr))
            res.bytecode.assign(bytecode.begin(), bytecode.end());   // InputLayout 생성용 보관
        break;
    case ShaderStage::Pixel:
        hr = m_device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr,
                                         res.ps.ReleaseAndGetAddressOf());
        break;
    }
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateShader failed (stage={}, hr=0x{:08X})",
                      static_cast<uint32_t>(stage), static_cast<uint32_t>(hr));
        return {};
    }
    return m_shaders.Allocate(std::move(res));
}

BindGroupLayoutHandle Dx11Device::CreateBindGroupLayout(const BindGroupLayoutDesc& desc) {
    BindGroupLayoutResource res;
    res.entries.assign(desc.entries.begin(), desc.entries.end());
    return m_bindGroupLayouts.Allocate(std::move(res));
}

BindGroupHandle Dx11Device::CreateBindGroup(const BindGroupDesc& desc) {
    BindGroupLayoutResource* layout = m_bindGroupLayouts.Get(desc.layout);
    if (!layout) {
        MYE_LOG_ERROR(kLog, "CreateBindGroup: invalid layout handle ('{}')", SafeName(desc.debugName));
        return {};
    }

    BindGroupResource res;
    for (const BindGroupEntry& entry : desc.entries) {
        ShaderStageFlags vis = ShaderStageFlags::All;
        bool foundInLayout = false;
        for (const BindGroupLayoutEntry& layoutEntry : layout->entries) {
            if (layoutEntry.binding == entry.binding && layoutEntry.type == entry.type) {
                vis = layoutEntry.visibility;
                foundInLayout = true;
                break;
            }
        }
        if (!foundInLayout)
            MYE_LOG_WARN(kLog, "CreateBindGroup: entry (binding={}) missing in layout — visibility=All ('{}')",
                         entry.binding, SafeName(desc.debugName));

        switch (entry.type) {
        case BindingType::UniformBuffer:
            if (entry.bufferOffset != 0)
                MYE_LOG_ERROR(kLog, "CreateBindGroup: bufferOffset != 0 is M1+ — ignored (binding={})",
                              entry.binding);
            // bufferSize: DX11 기본 바인딩은 전체 버퍼 — 부분 뷰(SetConstantBuffers1)는 M1+
            if (!m_buffers.Get(entry.buffer)) {
                MYE_LOG_ERROR(kLog, "CreateBindGroup: invalid uniform buffer (binding={})", entry.binding);
                break;
            }
            res.cbvs.push_back({entry.binding, entry.buffer, vis});
            break;
        case BindingType::SampledTexture:
            if (!m_textures.Get(entry.texture)) {
                MYE_LOG_ERROR(kLog, "CreateBindGroup: invalid texture (binding={})", entry.binding);
                break;
            }
            res.srvs.push_back({entry.binding, entry.texture, vis});
            break;
        case BindingType::Sampler:
            if (!m_samplers.Get(entry.sampler)) {
                MYE_LOG_ERROR(kLog, "CreateBindGroup: invalid sampler (binding={})", entry.binding);
                break;
            }
            res.smps.push_back({entry.binding, entry.sampler, vis});
            break;
        case BindingType::StructuredBuffer:
            MYE_LOG_ERROR(kLog, "CreateBindGroup: StructuredBuffer bindings are M1+ (binding={})",
                          entry.binding);
            break;
        }
    }
    return m_bindGroups.Allocate(std::move(res));
}

PipelineHandle Dx11Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    ShaderResource* vs = m_shaders.Get(desc.vs);
    ShaderResource* ps = m_shaders.Get(desc.ps);
    if (!vs || vs->stage != ShaderStage::Vertex || !vs->vs) {
        MYE_LOG_ERROR(kLog, "CreateGraphicsPipeline: invalid vertex shader ('{}')", SafeName(desc.debugName));
        return {};
    }
    if (!ps || ps->stage != ShaderStage::Pixel || !ps->ps) {
        MYE_LOG_ERROR(kLog, "CreateGraphicsPipeline: invalid pixel shader ('{}')", SafeName(desc.debugName));
        return {};
    }
    if (desc.bindGroupLayouts.size() > kMaxBindGroups) {
        MYE_LOG_ERROR(kLog, "CreateGraphicsPipeline: too many bind group layouts ({}, max {})",
                      desc.bindGroupLayouts.size(), kMaxBindGroups);
        return {};
    }

    PipelineResource res;
    res.vs = vs->vs;
    res.ps = ps->ps;

    if (!desc.vertexLayout.attributes.empty()) {
        if (vs->bytecode.empty()) {
            MYE_LOG_ERROR(kLog, "CreateGraphicsPipeline: VS bytecode unavailable for input layout ('{}')",
                          SafeName(desc.debugName));
            return {};
        }
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(desc.vertexLayout.attributes.size());
        for (const VertexAttribute& attr : desc.vertexLayout.attributes) {
            D3D11_INPUT_ELEMENT_DESC e{};
            e.SemanticName = attr.semantic;
            e.SemanticIndex = attr.semanticIndex;
            e.Format = ToDxgiFormat(attr.format);
            e.InputSlot = attr.bufferSlot;
            e.AlignedByteOffset = attr.offset;
            e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            e.InstanceDataStepRate = 0;
            elements.push_back(e);
        }
        const HRESULT hr = m_device->CreateInputLayout(elements.data(),
                                                       static_cast<UINT>(elements.size()),
                                                       vs->bytecode.data(), vs->bytecode.size(),
                                                       res.inputLayout.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            MYE_LOG_ERROR(kLog, "CreateGraphicsPipeline: CreateInputLayout failed ('{}', hr=0x{:08X})",
                          SafeName(desc.debugName), static_cast<uint32_t>(hr));
            return {};
        }
        SetDebugName(res.inputLayout.Get(), desc.debugName);
    }

    res.blend = GetOrCreateBlendState(desc.blend);
    res.depthStencil = GetOrCreateDepthStencilState(desc.depthStencil);
    res.rasterizer = GetOrCreateRasterizerState(desc.raster);
    if (!res.blend || !res.depthStencil || !res.rasterizer)
        return {};   // 원인은 GetOrCreate*가 로그

    res.topology = ToD3DTopology(desc.topology);
    res.vertexStride = desc.vertexLayout.stride;
    // desc.rtFormats: DX11은 파이프라인 생성 시 RT 포맷이 불필요 — DX12 PSO 호환용 서술로만 받는다.
    return m_pipelines.Allocate(std::move(res));
}

// ---------------------------------------------------------------------------
// Dx11Device — 상태 객체 캐시
// ---------------------------------------------------------------------------
ID3D11BlendState* Dx11Device::GetOrCreateBlendState(const BlendStateDesc& d) {
    FnvHasher key;
    key.Value(d.enable);
    key.Value(d.srcColor); key.Value(d.dstColor); key.Value(d.colorOp);
    key.Value(d.srcAlpha); key.Value(d.dstAlpha); key.Value(d.alphaOp);
    key.Value(d.writeMask);
    if (auto it = m_blendCache.find(key.hash); it != m_blendCache.end())
        return it->second.Get();

    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    rt.BlendEnable = d.enable ? TRUE : FALSE;
    rt.SrcBlend = ToD3DBlend(d.srcColor);
    rt.DestBlend = ToD3DBlend(d.dstColor);
    rt.BlendOp = ToD3DBlendOp(d.colorOp);
    rt.SrcBlendAlpha = ToD3DBlend(d.srcAlpha);
    rt.DestBlendAlpha = ToD3DBlend(d.dstAlpha);
    rt.BlendOpAlpha = ToD3DBlendOp(d.alphaOp);
    rt.RenderTargetWriteMask = d.writeMask;

    ComPtr<ID3D11BlendState> state;
    const HRESULT hr = m_device->CreateBlendState(&bd, state.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateBlendState failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return nullptr;
    }
    return m_blendCache.emplace(key.hash, std::move(state)).first->second.Get();
}

ID3D11DepthStencilState* Dx11Device::GetOrCreateDepthStencilState(const DepthStencilStateDesc& d) {
    FnvHasher key;
    key.Value(d.depthTest); key.Value(d.depthWrite); key.Value(d.depthFunc);
    if (auto it = m_depthStencilCache.find(key.hash); it != m_depthStencilCache.end())
        return it->second.Get();

    D3D11_DEPTH_STENCIL_DESC dd{};
    // DX11의 DepthEnable=FALSE는 테스트·쓰기 모두 끔 — 쓰기만 원할 때는 ALWAYS 비교로 흉내낸다
    dd.DepthEnable = (d.depthTest || d.depthWrite) ? TRUE : FALSE;
    dd.DepthWriteMask = d.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = d.depthTest ? ToD3DCompare(d.depthFunc) : D3D11_COMPARISON_ALWAYS;
    dd.StencilEnable = FALSE;   // 스텐실(실루엣 패스)은 M2 확장 (RhiTypes.h 주석)

    ComPtr<ID3D11DepthStencilState> state;
    const HRESULT hr = m_device->CreateDepthStencilState(&dd, state.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateDepthStencilState failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return nullptr;
    }
    return m_depthStencilCache.emplace(key.hash, std::move(state)).first->second.Get();
}

ID3D11RasterizerState* Dx11Device::GetOrCreateRasterizerState(const RasterizerStateDesc& d) {
    FnvHasher key;
    key.Value(d.cull); key.Value(d.fill); key.Value(d.scissorEnable); key.Value(d.depthBias);
    if (auto it = m_rasterizerCache.find(key.hash); it != m_rasterizerCache.end())
        return it->second.Get();

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = ToD3DFill(d.fill);
    rd.CullMode = ToD3DCull(d.cull);
    rd.FrontCounterClockwise = FALSE;
    rd.DepthBias = d.depthBias;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = d.scissorEnable ? TRUE : FALSE;

    ComPtr<ID3D11RasterizerState> state;
    const HRESULT hr = m_device->CreateRasterizerState(&rd, state.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateRasterizerState failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return nullptr;
    }
    return m_rasterizerCache.emplace(key.hash, std::move(state)).first->second.Get();
}

// ---------------------------------------------------------------------------
// Dx11Device — 지연 파괴
// ---------------------------------------------------------------------------
template <typename TPool, typename THandle>
void Dx11Device::DestroyDeferred(TPool& pool, THandle h, DestroyKind kind) {
    if (!h.IsValid()) return;   // null 핸들 Destroy 허용 (delete nullptr 시맨틱)
    typename TPool::Resource moved{};
    if (!pool.Invalidate(h, moved)) {
        MYE_LOG_ERROR(kLog, "Destroy: stale or invalid handle (kind={}, index={}, gen={})",
                      static_cast<uint32_t>(kind), h.index, h.gen);
        return;
    }
    DeferredDestroy entry;
    entry.retireFrame = m_frameIndex + kMaxFramesInFlight;
    entry.kind = kind;
    entry.poolIndex = h.index;
    CollectComRefs(moved, entry.keepAlive);
    m_destroyQueue.push_back(std::move(entry));
}

void Dx11Device::Destroy(BufferHandle h)          { DestroyDeferred(m_buffers, h, DestroyKind::Buffer); }
void Dx11Device::Destroy(TextureHandle h)         { DestroyDeferred(m_textures, h, DestroyKind::Texture); }
void Dx11Device::Destroy(SamplerHandle h)         { DestroyDeferred(m_samplers, h, DestroyKind::Sampler); }
void Dx11Device::Destroy(ShaderHandle h)          { DestroyDeferred(m_shaders, h, DestroyKind::Shader); }
void Dx11Device::Destroy(BindGroupLayoutHandle h) { DestroyDeferred(m_bindGroupLayouts, h, DestroyKind::BindGroupLayout); }
void Dx11Device::Destroy(BindGroupHandle h)       { DestroyDeferred(m_bindGroups, h, DestroyKind::BindGroup); }
void Dx11Device::Destroy(PipelineHandle h)        { DestroyDeferred(m_pipelines, h, DestroyKind::Pipeline); }

void Dx11Device::ProcessDestroyQueue(bool force) {
    while (!m_destroyQueue.empty()) {
        DeferredDestroy& entry = m_destroyQueue.front();
        if (!force && entry.retireFrame > m_frameIndex) break;   // 큐는 프레임 순 — 앞이 남으면 전부 남음
        entry.keepAlive.clear();                                 // GPU 객체 실제 해제
        RetireIndex(entry.kind, entry.poolIndex);                // 인덱스 재사용 허용
        m_destroyQueue.pop_front();
    }
}

void Dx11Device::RetireIndex(DestroyKind kind, uint32_t index) {
    switch (kind) {
    case DestroyKind::Buffer:          m_buffers.Retire(index); break;
    case DestroyKind::Texture:         m_textures.Retire(index); break;
    case DestroyKind::Sampler:         m_samplers.Retire(index); break;
    case DestroyKind::Shader:          m_shaders.Retire(index); break;
    case DestroyKind::BindGroupLayout: m_bindGroupLayouts.Retire(index); break;
    case DestroyKind::BindGroup:       m_bindGroups.Retire(index); break;
    case DestroyKind::Pipeline:        m_pipelines.Retire(index); break;
    }
}

void Dx11Device::DestroyTextureImmediate(TextureHandle h) {
    // 스왑체인 백버퍼 전용 — ResizeBuffers가 백버퍼 참조 0을 요구하므로 지연 큐를 거치지 않는다.
    TextureResource moved{};
    if (m_textures.Invalidate(h, moved)) {
        moved = TextureResource{};   // COM 즉시 해제
        m_textures.Retire(h.index);
    }
}

void Dx11Device::ReportLeaks() {
    const auto warnLeak = [](const char* what, size_t count) {
        if (count > 0)
            MYE_LOG_WARN(kLog, "RHI leak: {} {}(s) were not destroyed before device shutdown",
                         count, what);
    };
    warnLeak("buffer", m_buffers.AliveCount());
    warnLeak("texture", m_textures.AliveCount());
    warnLeak("sampler", m_samplers.AliveCount());
    warnLeak("shader", m_shaders.AliveCount());
    warnLeak("bind group layout", m_bindGroupLayouts.AliveCount());
    warnLeak("bind group", m_bindGroups.AliveCount());
    warnLeak("pipeline", m_pipelines.AliveCount());
}

// ---------------------------------------------------------------------------
// Dx11Device — 프레임·기타
// ---------------------------------------------------------------------------
ReadbackHandle Dx11Device::EnqueueReadback(BufferHandle, uint64_t, uint64_t) { return {}; }   // M2
bool Dx11Device::TryGetReadback(ReadbackHandle, std::span<std::byte>) { return false; }       // M2
bool Dx11Device::ResolveTimestamps(std::span<uint64_t>, uint64_t&) { return false; }          // M2

ICommandContext& Dx11Device::GetImmediateContext() { return m_immediateContext; }

void Dx11Device::BeginFrame() {
    // 프레임 시작: 섀도 상태 리셋 — 프레임 첫 SetPipeline이 반드시 실제 적용되도록 보장
    m_immediateContext.ResetShadowState();
}

void Dx11Device::EndFrame() {
    ++m_frameIndex;
    ProcessDestroyQueue(false);   // kMaxFramesInFlight 프레임 지난 항목 실제 해제
    // 동적 링 버퍼 리셋 지점 — M0은 매핑마다 전체 DISCARD라 별도 리셋 없음 (M1 부분 링에서 사용)
}

void* Dx11Device::GetImGuiTextureID(TextureHandle t) {
    TextureResource* tex = m_textures.Get(t);
    return tex ? static_cast<void*>(tex->srv.Get()) : nullptr;
}

const DeviceCaps& Dx11Device::GetCaps() const { return m_caps; }

// ---------------------------------------------------------------------------
// Dx11Device — CaptureBackbuffer (M0 통합 자동 검증용 디버그 유틸)
// ---------------------------------------------------------------------------
Expected<void, Error> Dx11Device::CaptureTextureToBmp(TextureHandle h, std::string_view utf8BmpPath) {
    TextureResource* tex = m_textures.Get(h);
    if (!tex || !tex->texture)
        return Error{"CaptureBackbuffer: invalid texture handle", 1};

    const bool isBgra = tex->format == Format::BGRA8Unorm || tex->format == Format::BGRA8UnormSrgb;
    const bool isRgba = tex->format == Format::RGBA8Unorm || tex->format == Format::RGBA8UnormSrgb;
    if (!isBgra && !isRgba)
        return Error{"CaptureBackbuffer: only RGBA8/BGRA8 formats are supported", 2};

    // 1) 스테이징 텍스처 생성 (CPU 읽기 가능)
    D3D11_TEXTURE2D_DESC sd{};
    tex->texture->GetDesc(&sd);
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = m_device->CreateTexture2D(&sd, nullptr, staging.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return Error{std::format("CaptureBackbuffer: staging texture creation failed (hr=0x{:08X})",
                                 static_cast<uint32_t>(hr)),
                     static_cast<int32_t>(hr)};

    // 2) 복사 + 즉시 Map(READ) — GPU 스톨 발생. 검증·디버그 전용 경로 (일반 리드백은 M2 지연 폴링)
    m_context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex->texture.Get(), 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return Error{std::format("CaptureBackbuffer: Map failed (hr=0x{:08X})",
                                 static_cast<uint32_t>(hr)),
                     static_cast<int32_t>(hr)};

    // 3) 32bpp BMP (top-down, BGRA) 조립
    const uint32_t width = tex->width;
    const uint32_t height = tex->height;
    const uint32_t rowBytes = width * 4;
    const uint32_t imageSize = rowBytes * height;

#pragma pack(push, 1)
    struct BmpFileHeader { uint16_t type; uint32_t fileSize; uint16_t r0, r1; uint32_t dataOffset; };
    struct BmpInfoHeader {
        uint32_t headerSize; int32_t width, height; uint16_t planes, bpp;
        uint32_t compression, imageSize; int32_t xppm, yppm; uint32_t clrUsed, clrImportant;
    };
#pragma pack(pop)
    static_assert(sizeof(BmpFileHeader) == 14 && sizeof(BmpInfoHeader) == 40);

    BmpFileHeader fileHeader{};
    fileHeader.type = 0x4D42;   // 'BM'
    fileHeader.fileSize = 14 + 40 + imageSize;
    fileHeader.dataOffset = 14 + 40;
    BmpInfoHeader infoHeader{};
    infoHeader.headerSize = 40;
    infoHeader.width = static_cast<int32_t>(width);
    infoHeader.height = -static_cast<int32_t>(height);   // 음수 = top-down
    infoHeader.planes = 1;
    infoHeader.bpp = 32;
    infoHeader.compression = 0;   // BI_RGB
    infoHeader.imageSize = imageSize;

    std::vector<uint8_t> file(14 + 40 + static_cast<size_t>(imageSize));
    std::memcpy(file.data(), &fileHeader, 14);
    std::memcpy(file.data() + 14, &infoHeader, 40);
    uint8_t* dstBase = file.data() + 54;
    const uint8_t* srcBase = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = srcBase + static_cast<size_t>(y) * mapped.RowPitch;
        uint8_t* dst = dstBase + static_cast<size_t>(y) * rowBytes;
        if (isBgra) {
            std::memcpy(dst, src, rowBytes);
        } else {
            for (uint32_t x = 0; x < width; ++x) {   // RGBA → BGRA 스위즐
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
    }
    m_context->Unmap(staging.Get(), 0);

    // 4) 파일 저장 (UTF-8 경로 → W-API)
    const std::wstring widePath = WidenUtf8(utf8BmpPath);
    HANDLE fileHandle = ::CreateFileW(widePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE)
        return Error{std::format("CaptureBackbuffer: cannot create '{}' (win32={})",
                                 utf8BmpPath, ::GetLastError()),
                     3};
    DWORD written = 0;
    const BOOL writeOk = ::WriteFile(fileHandle, file.data(), static_cast<DWORD>(file.size()),
                                     &written, nullptr);
    const DWORD writeError = writeOk ? 0 : ::GetLastError();
    ::CloseHandle(fileHandle);
    if (!writeOk || written != static_cast<DWORD>(file.size()))
        return Error{std::format("CaptureBackbuffer: write failed for '{}' (win32={})",
                                 utf8BmpPath, writeError),
                     4};

    MYE_LOG_INFO(kLog, "backbuffer captured: '{}' ({}x{})", utf8BmpPath, width, height);
    return {};
}

// ---------------------------------------------------------------------------
// Dx11Device — 스왑체인 생성
// ---------------------------------------------------------------------------
std::unique_ptr<ISwapChain> Dx11Device::CreateSwapChain(void* nativeWindowHandle,
                                                        const SwapChainDesc& desc) {
    if (!nativeWindowHandle) {
        MYE_LOG_ERROR(kLog, "CreateSwapChain: null window handle");
        return nullptr;
    }
    const HWND hwnd = static_cast<HWND>(nativeWindowHandle);

    auto swapChain = std::unique_ptr<Dx11SwapChain>(new Dx11SwapChain());
    swapChain->m_device = this;
    swapChain->m_format = desc.format;
    swapChain->m_allowTearing = desc.allowTearing && m_tearingSupported;
    if (desc.allowTearing && !m_tearingSupported)
        MYE_LOG_WARN(kLog, "CreateSwapChain: tearing requested but unsupported by OS/driver");

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = desc.width;    // 0 = 창 클라이언트 크기 자동
    sd.Height = desc.height;
    sd.Format = ToDxgiFormat(StripSrgb(desc.format));   // FLIP 모델: 버퍼는 UNORM, sRGB는 RTV 뷰로
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = desc.bufferCount < 2 ? 2u : desc.bufferCount;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChain->m_flags = swapChain->m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    sd.Flags = swapChain->m_flags;

    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &sd, nullptr, nullptr,
                                                   swapChain->m_swapChain.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "CreateSwapChainForHwnd failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return nullptr;
    }
    // 전체화면은 borderless 방식(01 창 모드) — DXGI Alt+Enter 배타 전체화면 전환 차단
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (!swapChain->AcquireBackbuffer())
        return nullptr;

    MYE_LOG_INFO(kLog, "swapchain created: {}x{}, buffers={}, tearing={}",
                 swapChain->m_size.x, swapChain->m_size.y, sd.BufferCount, swapChain->m_allowTearing);
    return swapChain;
}

// ---------------------------------------------------------------------------
// Dx11SwapChain
// ---------------------------------------------------------------------------
Dx11SwapChain::~Dx11SwapChain() {
    // 주의: 스왑체인은 디바이스보다 먼저 파괴할 것 (백버퍼 핸들이 디바이스 텍스처 풀 소유)
    if (m_device && m_backbuffer.IsValid()) {
        m_device->m_immediateContext.UnbindRenderTargets();
        m_device->DestroyTextureImmediate(m_backbuffer);
    }
}

bool Dx11SwapChain::AcquireBackbuffer() {
    ComPtr<ID3D11Texture2D> buffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "swapchain GetBuffer failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 actual{};
    m_swapChain->GetDesc1(&actual);
    m_size = Vec2i{static_cast<int32_t>(actual.Width), static_cast<int32_t>(actual.Height)};

    // 요청 포맷(sRGB 포함) 그대로 RTV 뷰 — FLIP 모델 UNORM 백버퍼에 sRGB 뷰 캐스팅 허용
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = ToDxgiFormat(m_format);
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = m_device->m_device->CreateRenderTargetView(buffer.Get(), &rtvDesc,
                                                    rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "backbuffer RTV creation failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }
    SetDebugName(buffer.Get(), "SwapChainBackbuffer");

    TextureResource res;
    res.texture = std::move(buffer);
    res.rtv = std::move(rtv);
    res.format = m_format;
    res.width = actual.Width;
    res.height = actual.Height;
    res.isBackbuffer = true;
    m_backbuffer = m_device->m_textures.Allocate(std::move(res));
    return true;
}

void Dx11SwapChain::Resize(uint32_t w, uint32_t h) {
    if (!m_swapChain) return;
    if (w == 0 || h == 0) return;   // 최소화 — 복원 시 WindowResizedEvent로 다시 Resize가 온다
    if (static_cast<int32_t>(w) == m_size.x && static_cast<int32_t>(h) == m_size.y) return;

    // ResizeBuffers는 백버퍼 참조 0을 요구 — RTV 언바인드 + 백버퍼 핸들 즉시 해제(지연 큐 미사용)
    m_device->m_immediateContext.UnbindRenderTargets();
    if (m_backbuffer.IsValid())
        m_device->DestroyTextureImmediate(m_backbuffer);
    m_backbuffer = {};

    const HRESULT hr = m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, m_flags);
    if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "ResizeBuffers({}x{}) failed (hr=0x{:08X})", w, h,
                      static_cast<uint32_t>(hr));
        return;
    }
    AcquireBackbuffer();
}

void Dx11SwapChain::Present(bool vsync) {
    if (!m_swapChain) return;
    const UINT interval = vsync ? 1u : 0u;
    const UINT flags = (!vsync && m_allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const HRESULT hr = m_swapChain->Present(interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        const HRESULT reason = m_device->m_device->GetDeviceRemovedReason();
        MYE_LOG_ERROR(kLog, "Present: device removed (hr=0x{:08X}, reason=0x{:08X})",
                      static_cast<uint32_t>(hr), static_cast<uint32_t>(reason));
    } else if (FAILED(hr)) {
        MYE_LOG_ERROR(kLog, "Present failed (hr=0x{:08X})", static_cast<uint32_t>(hr));
    }
    // DXGI_STATUS_OCCLUDED 등 성공 status 코드는 무시 (가려진 창 — 정상)
}

} // namespace mye::rhi::dx11
