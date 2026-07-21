// Dx11Device.h — DX11 백엔드 내부 헤더 (공개 API는 mye/rhi/Rhi.h — 이 헤더는 rhi 모듈 밖 포함 금지)
//
// 구조 (docs/02 "DX11 백엔드 구현 전략"):
//  - HandlePool<T,H>    : index+generation 슬롯 풀. Destroy 시 세대를 즉시 올려 dangling 접근을 탐지하고,
//                         GPU 객체는 지연 파괴 큐가 kMaxFramesInFlight 프레임 보관 후 실제 해제한다.
//                         인덱스 재사용(Retire)도 큐 소비 시점까지 미룬다.
//  - Dx11Device         : ID3D11Device + 리소스 풀 + 상태 객체 캐시(블렌드·뎁스·래스터) + 지연 파괴 큐.
//  - Dx11CommandContext : immediate context 단독 래핑. SetPipeline은 캐싱된 상태 객체 묶음으로 분해 적용,
//                         중복 바인딩은 상태 섀도잉으로 걸러낸다. BeginRenderPass → OMSetRenderTargets + Clear*.
//  - Dx11SwapChain      : IDXGISwapChain1 (FLIP_DISCARD). 백버퍼는 디바이스 텍스처 풀에 등록된 TextureHandle.
//                         주의: 스왑체인은 반드시 디바이스보다 먼저 파괴할 것.
#pragma once

#include "mye/rhi/Rhi.h"

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <deque>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::rhi::dx11 {

using Microsoft::WRL::ComPtr;

class Dx11Device;
class Dx11SwapChain;

// ---------------------------------------------------------------------------
// HandlePool — index+generation 슬롯 풀 (세대 검증으로 stale 핸들 탐지)
// ---------------------------------------------------------------------------
template <typename TResource, typename THandle>
class HandlePool {
public:
    using Resource = TResource;

    THandle Allocate(TResource&& resource) {
        uint32_t index = 0;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        Slot& slot = m_slots[index];
        slot.resource = std::move(resource);
        slot.alive = true;
        return THandle{index, slot.gen};
    }

    TResource* Get(THandle h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot& slot = m_slots[h.index];
        if (!slot.alive || slot.gen != h.gen) return nullptr;
        return &slot.resource;
    }

    // Destroy 시점 호출: 세대 즉시 증가(이후 Get은 nullptr — dangling 탐지) + 리소스 소유권 반출.
    // 반출된 리소스는 지연 파괴 큐가 kMaxFramesInFlight 프레임 보관한다.
    // 인덱스 재사용은 Retire() 이후에만 일어난다.
    bool Invalidate(THandle h, TResource& outMoved) {
        if (!Get(h)) return false;
        Slot& slot = m_slots[h.index];
        outMoved = std::move(slot.resource);
        slot.resource = TResource{};
        slot.alive = false;
        ++slot.gen;
        return true;
    }

    void Retire(uint32_t index) { m_free.push_back(index); }

    size_t AliveCount() const {
        size_t count = 0;
        for (const Slot& slot : m_slots)
            if (slot.alive) ++count;
        return count;
    }

    void Clear() {
        m_slots.clear();
        m_free.clear();
    }

private:
    struct Slot {
        TResource resource{};
        uint32_t  gen = 1;      // 1 시작 — 기본 핸들(gen=0)과 절대 일치하지 않음
        bool      alive = false;
    };
    std::vector<Slot>     m_slots;
    std::vector<uint32_t> m_free;
};

// ---------------------------------------------------------------------------
// 리소스 풀 엔트리
// ---------------------------------------------------------------------------
struct BufferResource {
    ComPtr<ID3D11Buffer> buffer;
    uint64_t    size = 0;
    BufferUsage usage = BufferUsage::None;
    CpuAccess   access = CpuAccess::None;
};

struct TextureResource {
    ComPtr<ID3D11Texture2D>          texture;
    ComPtr<ID3D11RenderTargetView>   rtv;    // TextureUsage::RenderTarget (스왑체인 백버퍼 포함)
    ComPtr<ID3D11DepthStencilView>   dsv;    // TextureUsage::DepthStencil
    ComPtr<ID3D11ShaderResourceView> srv;    // TextureUsage::Sampled
    Format   format = Format::Unknown;
    uint32_t width = 0, height = 0;
    uint16_t mips = 1, arrayLayers = 1;
    bool     isBackbuffer = false;
};

struct SamplerResource {
    ComPtr<ID3D11SamplerState> state;
};

struct ShaderResource {
    ShaderStage stage = ShaderStage::Vertex;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader>  ps;
    std::vector<std::byte> bytecode;   // VS만 보관 — CreateGraphicsPipeline의 InputLayout 생성용
};

struct BindGroupLayoutResource {
    std::vector<BindGroupLayoutEntry> entries;
};

// BindGroup은 생성 시 레지스터별 바인딩으로 미리 구워두고 SetBindGroup에서 일괄 바인딩 (docs/02)
struct BindGroupResource {
    struct CbvBinding { uint32_t reg = 0; BufferHandle  buffer{};  ShaderStageFlags vis = ShaderStageFlags::All; };
    struct SrvBinding { uint32_t reg = 0; TextureHandle texture{}; ShaderStageFlags vis = ShaderStageFlags::All; };
    struct SmpBinding { uint32_t reg = 0; SamplerHandle sampler{}; ShaderStageFlags vis = ShaderStageFlags::All; };
    std::vector<CbvBinding> cbvs;
    std::vector<SrvBinding> srvs;
    std::vector<SmpBinding> smps;
};

// 모놀리식 PSO → DX11 개별 상태 객체 묶음으로 분해 (상태 객체는 디바이스 캐시가 소유·공유)
struct PipelineResource {
    ComPtr<ID3D11VertexShader>      vs;
    ComPtr<ID3D11PixelShader>       ps;
    ComPtr<ID3D11InputLayout>       inputLayout;   // 어트리뷰트 없으면 null (버텍스리스 드로우)
    ComPtr<ID3D11BlendState>        blend;
    ComPtr<ID3D11DepthStencilState> depthStencil;
    ComPtr<ID3D11RasterizerState>   rasterizer;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32_t vertexStride = 0;      // 버텍스 버퍼 슬롯 0 (멀티 슬롯 스트라이드는 M1+)
};

// ---------------------------------------------------------------------------
// Dx11CommandContext — immediate context 래핑 + 상태 섀도잉
// ---------------------------------------------------------------------------
class Dx11CommandContext final : public ICommandContext {
public:
    void BeginRenderPass(const RenderPassBeginDesc& desc) override;
    void EndRenderPass() override;
    void SetPipeline(PipelineHandle p) override;
    void SetBindGroup(uint32_t slot, BindGroupHandle g,
                      std::span<const uint32_t> dynamicOffsets) override;
    void SetVertexBuffer(uint32_t slot, BufferHandle b, uint64_t offset) override;
    void SetIndexBuffer(BufferHandle b, IndexFormat fmt) override;
    void SetViewport(const Viewport& vp) override;
    void SetScissor(const RectInt& r) override;
    void Draw(uint32_t vertexCount, uint32_t firstVertex) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex) override;
    void DrawIndexedInstanced(uint32_t idxCount, uint32_t instCount, uint32_t firstIdx,
                              int32_t baseVtx, uint32_t firstInst) override;
    void* MapDynamic(BufferHandle b, uint64_t size) override;
    void  Unmap(BufferHandle b) override;
    void CopyTexture(TextureHandle dst, TextureHandle src, const TextureCopyRegion& rgn) override;
    void WriteTexture(TextureHandle dst, const TextureCopyRegion& rgn,
                      const void* srcData, uint32_t srcRowPitch) override;
    void CopyTextureToBuffer(BufferHandle dst, TextureHandle src, const TextureCopyRegion& rgn) override;
    void WriteTimestamp(uint32_t queryIndex) override;
    void PushDebugMarker(const char* name) override;
    void PopDebugMarker() override;

private:
    friend class Dx11Device;
    friend class Dx11SwapChain;

    void Initialize(Dx11Device* device, ID3D11DeviceContext* ctx);
    void ResetShadowState();          // 프레임 시작 시 재적용 보장 (BeginFrame)
    void UnbindRenderTargets();       // ResizeBuffers·디바이스 종료 전 백버퍼 참조 해제
    void FlushInputAssembly();        // 지연된 버텍스 버퍼 바인딩 적용 (SetPipeline↔SetVertexBuffer 순서 무관)
    bool ValidateDraw(const char* what);

    Dx11Device* m_device = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;             // 소유: Dx11Device (ComPtr)
    ComPtr<ID3DUserDefinedAnnotation> m_annotation;   // 디버그 마커 (미지원 환경이면 null → no-op)

    bool m_inRenderPass = false;
    bool m_passMarkerActive = false;
    bool m_warnedDynamicOffsets = false;
    PipelineHandle m_currentPipeline{};
    uint32_t m_vertexStride = 0;

    struct VertexBufferBinding {
        BufferHandle buffer{};
        uint64_t     offset = 0;
        bool         dirty = false;
    };
    VertexBufferBinding m_vb;   // 슬롯 0만 (M0 — VertexLayoutDesc.stride가 슬롯 0 한정)

    // 상태 섀도잉 — 마지막 적용 객체 (파이프라인 간 중복 바인딩 필터)
    ID3D11VertexShader*      m_lastVs = nullptr;
    ID3D11PixelShader*       m_lastPs = nullptr;
    ID3D11InputLayout*       m_lastInputLayout = nullptr;
    ID3D11BlendState*        m_lastBlend = nullptr;
    ID3D11DepthStencilState* m_lastDepthStencil = nullptr;
    ID3D11RasterizerState*   m_lastRasterizer = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY m_lastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

// ---------------------------------------------------------------------------
// Dx11Device
// ---------------------------------------------------------------------------
class Dx11Device final : public IDevice {
public:
    static Expected<std::unique_ptr<Dx11Device>, Error> Create(const DeviceCreateInfo& info);
    ~Dx11Device() override;

    BufferHandle          CreateBuffer(const BufferDesc& desc, const void* initialData) override;
    TextureHandle         CreateTexture(const TextureDesc& desc, const TextureInitData* init) override;
    SamplerHandle         CreateSampler(const SamplerDesc& desc) override;
    ShaderHandle          CreateShader(ShaderStage stage, std::span<const std::byte> bytecode) override;
    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    BindGroupHandle       CreateBindGroup(const BindGroupDesc& desc) override;
    PipelineHandle        CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;

    void Destroy(BufferHandle h) override;
    void Destroy(TextureHandle h) override;
    void Destroy(SamplerHandle h) override;
    void Destroy(ShaderHandle h) override;
    void Destroy(BindGroupLayoutHandle h) override;
    void Destroy(BindGroupHandle h) override;
    void Destroy(PipelineHandle h) override;

    std::unique_ptr<ISwapChain> CreateSwapChain(void* nativeWindowHandle,
                                                const SwapChainDesc& desc) override;

    ReadbackHandle EnqueueReadback(BufferHandle src, uint64_t offset, uint64_t size) override;
    bool           TryGetReadback(ReadbackHandle h, std::span<std::byte> out) override;
    bool           ResolveTimestamps(std::span<uint64_t> outTicks, uint64_t& outFrequency) override;

    ICommandContext& GetImmediateContext() override;
    void BeginFrame() override;
    void EndFrame() override;
    void* GetImGuiTextureID(TextureHandle t) override;
    void* GetNativeDevice() override;
    void* GetNativeContext() override;
    const DeviceCaps& GetCaps() const override;

    // 디버그 유틸(M0 자동 검증): 텍스처를 스테이징 텍스처로 복사해 32bpp BMP로 저장.
    // 공개 진입점은 rhi::CaptureBackbuffer (Rhi.h) — 즉시 Map(READ) 스톨이 있으므로 개발·검증 전용.
    Expected<void, Error> CaptureTextureToBmp(TextureHandle h, std::string_view utf8BmpPath);

private:
    friend class Dx11CommandContext;
    friend class Dx11SwapChain;

    enum class DestroyKind : uint8_t { Buffer, Texture, Sampler, Shader, BindGroupLayout, BindGroup, Pipeline };

    struct DeferredDestroy {
        uint64_t    retireFrame = 0;                 // 이 프레임 인덱스 도달 시 실제 해제
        DestroyKind kind = DestroyKind::Buffer;
        uint32_t    poolIndex = 0;
        std::vector<ComPtr<IUnknown>> keepAlive;     // GPU 객체 수명 연장 (kMaxFramesInFlight 프레임)
    };

    Dx11Device() = default;

    template <typename TPool, typename THandle>
    void DestroyDeferred(TPool& pool, THandle h, DestroyKind kind);
    void ProcessDestroyQueue(bool force);
    void RetireIndex(DestroyKind kind, uint32_t index);
    void DestroyTextureImmediate(TextureHandle h);   // 스왑체인 백버퍼 전용 (ResizeBuffers가 참조 0 요구)
    void ReportLeaks();

    // 상태 객체 캐시 — 반환 포인터는 캐시가 소유 (호출측 ComPtr 대입 시 AddRef)
    ID3D11BlendState*        GetOrCreateBlendState(const BlendStateDesc& d);
    ID3D11DepthStencilState* GetOrCreateDepthStencilState(const DepthStencilStateDesc& d);
    ID3D11RasterizerState*   GetOrCreateRasterizerState(const RasterizerStateDesc& d);

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIFactory2>       m_factory;
    bool     m_debugLayerEnabled = false;
    bool     m_tearingSupported = false;
    uint64_t m_frameIndex = 0;

    HandlePool<BufferResource, BufferHandle>                   m_buffers;
    HandlePool<TextureResource, TextureHandle>                 m_textures;
    HandlePool<SamplerResource, SamplerHandle>                 m_samplers;
    HandlePool<ShaderResource, ShaderHandle>                   m_shaders;
    HandlePool<BindGroupLayoutResource, BindGroupLayoutHandle> m_bindGroupLayouts;
    HandlePool<BindGroupResource, BindGroupHandle>             m_bindGroups;
    HandlePool<PipelineResource, PipelineHandle>               m_pipelines;

    std::unordered_map<uint64_t, ComPtr<ID3D11BlendState>>        m_blendCache;
    std::unordered_map<uint64_t, ComPtr<ID3D11DepthStencilState>> m_depthStencilCache;
    std::unordered_map<uint64_t, ComPtr<ID3D11RasterizerState>>   m_rasterizerCache;

    std::deque<DeferredDestroy> m_destroyQueue;

    Dx11CommandContext m_immediateContext;
    DeviceCaps m_caps{};
};

// ---------------------------------------------------------------------------
// Dx11SwapChain — FLIP_DISCARD. 디바이스보다 먼저 파괴할 것 (백버퍼 핸들이 디바이스 풀 소유).
// ---------------------------------------------------------------------------
class Dx11SwapChain final : public ISwapChain {
public:
    ~Dx11SwapChain() override;

    void Resize(uint32_t w, uint32_t h) override;
    TextureHandle GetCurrentBackBuffer() override { return m_backbuffer; }
    void Present(bool vsync) override;
    Vec2i GetSize() const override { return m_size; }
    Format GetFormat() const override { return m_format; }

private:
    friend class Dx11Device;

    Dx11SwapChain() = default;
    bool AcquireBackbuffer();   // GetBuffer(0) → RTV(sRGB 뷰) → 텍스처 풀 등록

    Dx11Device*             m_device = nullptr;
    ComPtr<IDXGISwapChain1> m_swapChain;
    TextureHandle           m_backbuffer{};
    Vec2i                   m_size{};
    Format                  m_format = Format::BGRA8UnormSrgb;
    UINT                    m_flags = 0;        // 생성 플래그 — ResizeBuffers에 동일 전달
    bool                    m_allowTearing = false;
};

} // namespace mye::rhi::dx11
