// mye/rhi/Rhi.h — IDevice / ICommandContext / ISwapChain (docs/02)
//
// DX11 백엔드 전략: immediate context 단독. SetPipeline은 캐싱된 상태 객체 묶음으로 분해 적용
// + 상태 섀도잉. BeginRenderPass → OMSetRenderTargets + Clear*. 동적 버퍼 = MAP_WRITE_DISCARD 링.
// Destroy는 즉시 해제가 아니라 kMaxFramesInFlight 프레임 지연 파괴 큐를 거친다.
#pragma once

#include "mye/rhi/RhiTypes.h"

#include <memory>
#include <string_view>

namespace mye::rhi {

class ISwapChain;

class ICommandContext {
public:
    virtual ~ICommandContext() = default;

    virtual void BeginRenderPass(const RenderPassBeginDesc& desc) = 0;   // RT 세트 + LoadOp + Clear
    virtual void EndRenderPass() = 0;

    virtual void SetPipeline(PipelineHandle p) = 0;
    virtual void SetBindGroup(uint32_t slot, BindGroupHandle g,
                              std::span<const uint32_t> dynamicOffsets = {}) = 0;   // slot 0..3
    virtual void SetVertexBuffer(uint32_t slot, BufferHandle b, uint64_t offset = 0) = 0;
    virtual void SetIndexBuffer(BufferHandle b, IndexFormat fmt) = 0;
    virtual void SetViewport(const Viewport& vp) = 0;
    virtual void SetScissor(const RectInt& r) = 0;                       // 06 UI 클리핑 요건

    virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t baseVertex = 0) = 0;
    virtual void DrawIndexedInstanced(uint32_t idxCount, uint32_t instCount, uint32_t firstIdx = 0,
                                      int32_t baseVtx = 0, uint32_t firstInst = 0) = 0;

    // 동적 버퍼(CpuAccess::WriteDiscardRing) 갱신 — 링 버퍼 WRITE_DISCARD 스타일
    virtual void* MapDynamic(BufferHandle b, uint64_t size) = 0;
    virtual void  Unmap(BufferHandle b) = 0;

    virtual void CopyTexture(TextureHandle dst, TextureHandle src, const TextureCopyRegion& rgn) = 0;
    // CPU→텍스처 부분 업로드 (DX11: UpdateSubresource) — 글리프 아틀라스 R8·팔레트 LUT·타일 청크 갱신
    virtual void WriteTexture(TextureHandle dst, const TextureCopyRegion& rgn,
                              const void* srcData, uint32_t srcRowPitch) = 0;
    // 리드백 준비 (ID 픽킹·썸네일 캡처, M2) — IDevice::EnqueueReadback과 짝
    virtual void CopyTextureToBuffer(BufferHandle dst, TextureHandle src, const TextureCopyRegion& rgn) = 0;

    virtual void WriteTimestamp(uint32_t queryIndex) = 0;                // 07 프로파일러 GPU 탭 (M2)
    virtual void PushDebugMarker(const char* name) = 0;
    virtual void PopDebugMarker() = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    // ---- 리소스 생성 ----
    virtual BufferHandle          CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
    virtual TextureHandle         CreateTexture(const TextureDesc& desc, const TextureInitData* init = nullptr) = 0;
    virtual SamplerHandle         CreateSampler(const SamplerDesc& desc) = 0;
    virtual ShaderHandle          CreateShader(ShaderStage stage, std::span<const std::byte> bytecode) = 0;
    virtual BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) = 0;
    virtual BindGroupHandle       CreateBindGroup(const BindGroupDesc& desc) = 0;
    virtual PipelineHandle        CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;

    // ---- 파괴: 즉시 해제 아님 — kMaxFramesInFlight 프레임 지연 파괴 큐 ----
    virtual void Destroy(BufferHandle h) = 0;
    virtual void Destroy(TextureHandle h) = 0;
    virtual void Destroy(SamplerHandle h) = 0;
    virtual void Destroy(ShaderHandle h) = 0;
    virtual void Destroy(BindGroupLayoutHandle h) = 0;
    virtual void Destroy(BindGroupHandle h) = 0;
    virtual void Destroy(PipelineHandle h) = 0;

    // 창별 스왑체인 (ImGui 멀티 뷰포트(07 P1)·보조 창 대응)
    virtual std::unique_ptr<ISwapChain> CreateSwapChain(void* nativeWindowHandle,
                                                        const SwapChainDesc& desc) = 0;

    // GPU→CPU 리드백: N프레임 지연 폴링 (스톨 유발 즉시 Map 금지). M2 구현.
    virtual ReadbackHandle EnqueueReadback(BufferHandle src, uint64_t offset, uint64_t size) = 0;
    virtual bool           TryGetReadback(ReadbackHandle h, std::span<std::byte> out) = 0;
    // WriteTimestamp 결과 리졸브 (N프레임 지연, disjoint 시 false). M2 구현.
    virtual bool           ResolveTimestamps(std::span<uint64_t> outTicks, uint64_t& outFrequency) = 0;

    virtual ICommandContext& GetImmediateContext() = 0;   // MVP: 단일 컨텍스트

    // 프레임 경계: 지연 파괴 큐 처리·링 버퍼 리셋 시점
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    // ImGui/에디터 노출용 (SRV 포인터 — ImTextureID로 캐스팅 사용. ImGui 백엔드는 M0 범위 외)
    virtual void* GetImGuiTextureID(TextureHandle t) = 0;

    virtual const DeviceCaps& GetCaps() const = 0;
};

class ISwapChain {
public:
    virtual ~ISwapChain() = default;
    virtual void Resize(uint32_t w, uint32_t h) = 0;         // WindowResizedEvent 구독자(렌더러)가 호출
    virtual TextureHandle GetCurrentBackBuffer() = 0;        // BeginRenderPass 컬러 어태치먼트로 사용
    virtual void Present(bool vsync) = 0;
    virtual Vec2i GetSize() const = 0;
    virtual Format GetFormat() const = 0;
};

// 백엔드 팩토리 — 백엔드는 컴파일 타임 내장 한정(런타임 플러그인 비허용, docs/02)
Expected<std::unique_ptr<IDevice>, Error> CreateDevice(Backend backend, const DeviceCreateInfo& info);

// ---------------------------------------------------------------------------
// 디버그 유틸 (M0 완료 기준 자동 검증용 — 스캐폴드 계약에 대한 '추가' API)
// 텍스처(주로 ISwapChain::GetCurrentBackBuffer())를 스테이징 텍스처로 복사해
// 32bpp BMP(top-down, BGRA)로 저장한다. 즉시 GPU Flush + Map(READ) 스톨이 발생하므로
// 개발·자동 검증 전용이다. RGBA8/BGRA8 계열 포맷만 지원. Present 호출 전(백버퍼에 그린 뒤)에
// 호출할 것. 구현: src/dx11/Dx11Device.cpp
Expected<void, Error> CaptureBackbuffer(IDevice& device, TextureHandle backbuffer,
                                        std::string_view utf8BmpPath);

} // namespace mye::rhi
