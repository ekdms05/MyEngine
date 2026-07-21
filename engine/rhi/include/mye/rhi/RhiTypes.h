// mye/rhi/RhiTypes.h — RHI 공용 타입: 핸들·열거·서술자 (docs/02)
//
// 3기둥: 모놀리식 PipelineState · BindGroup 바인딩 모델 · BeginRenderPass/EndRenderPass.
// 핸들은 불투명 32+32bit(index+generation) — dangling 접근을 세대 검증으로 탐지.
// 리소스 수명: 수동 생성/파괴 + 프레임 지연 파괴(참조 카운트 없음 — 상위 계층 담당).
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <span>

namespace mye::rhi {

// ---------------------------------------------------------------------------
// 핸들
// ---------------------------------------------------------------------------
template <typename Tag>
struct Handle {
    static constexpr uint32_t kInvalidIndex = 0xFFFF'FFFFu;
    uint32_t index = kInvalidIndex;
    uint32_t gen = 0;
    constexpr bool IsValid() const { return index != kInvalidIndex; }
    constexpr bool operator==(const Handle&) const = default;
};

using BufferHandle          = Handle<struct BufferTag>;
using TextureHandle         = Handle<struct TextureTag>;
using SamplerHandle         = Handle<struct SamplerTag>;
using ShaderHandle          = Handle<struct ShaderTag>;          // 스테이지별 바이트코드 1개
using PipelineHandle        = Handle<struct PipelineTag>;
using BindGroupHandle       = Handle<struct BindGroupTag>;
using BindGroupLayoutHandle = Handle<struct BindGroupLayoutTag>;

struct ReadbackHandle { uint64_t opaque = 0; bool IsValid() const { return opaque != 0; } };

// 프레임 지연 파괴·리드백 폴링의 in-flight 프레임 수 (Destroy 후 N프레임 뒤 실제 해제)
inline constexpr uint32_t kMaxFramesInFlight = 2;

// BindGroup 슬롯 규약 (docs/02): 0 = PerFrame(뷰·시간), 1 = PerPass, 2 = PerMaterial, 3 = PerDraw
inline constexpr uint32_t kBindGroupSlotPerFrame    = 0;
inline constexpr uint32_t kBindGroupSlotPerPass     = 1;
inline constexpr uint32_t kBindGroupSlotPerMaterial = 2;
inline constexpr uint32_t kBindGroupSlotPerDraw     = 3;
inline constexpr uint32_t kMaxBindGroups            = 4;
inline constexpr uint32_t kMaxColorAttachments      = 8;

// ---------------------------------------------------------------------------
// 열거
// ---------------------------------------------------------------------------
enum class Backend : uint8_t { DX11 };   // DX12/Vulkan은 컴파일 타임 내장으로 추가(런타임 플러그인 비허용)

enum class Format : uint8_t {
    Unknown,
    RGBA8Unorm, RGBA8UnormSrgb,
    BGRA8Unorm, BGRA8UnormSrgb,
    R8Unorm,                      // 글리프 아틀라스(06)·팔레트 인덱스
    R32Uint,                      // 엔티티 ID 버퍼(M2)
    RG32Float, RGB32Float, RGBA32Float,
    RGBA16Float,
    D24UnormS8Uint, D32Float,
};

enum class BufferUsage : uint8_t {       // 비트 플래그
    None = 0, Vertex = 1 << 0, Index = 1 << 1, Uniform = 1 << 2, Structured = 1 << 3,
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool HasFlag(BufferUsage v, BufferUsage f) {
    return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}

enum class CpuAccess : uint8_t { None, WriteDiscardRing };   // 동적 버퍼 = MAP_WRITE_DISCARD 링

enum class TextureDim : uint8_t { Tex2D, Tex2DArray, Tex3D, TexCube };

enum class TextureUsage : uint8_t {      // 비트 플래그
    None = 0, Sampled = 1 << 0, RenderTarget = 1 << 1, DepthStencil = 1 << 2,
    CopySrc = 1 << 3, CopyDst = 1 << 4,
};
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool HasFlag(TextureUsage v, TextureUsage f) {
    return (static_cast<uint8_t>(v) & static_cast<uint8_t>(f)) != 0;
}

enum class SampleCount : uint8_t { X1 = 1, X2 = 2, X4 = 4, X8 = 8 };
enum class Filter : uint8_t { Point, Linear };               // 픽셀아트 기본 = Point
enum class AddressMode : uint8_t { Clamp, Wrap, Mirror, Border };
enum class ShaderStage : uint8_t { Vertex, Pixel };          // gs/hs/ds 도입 안 함(이식성)
enum class PrimitiveTopology : uint8_t { TriangleList, TriangleStrip, LineList, PointList };
enum class IndexFormat : uint8_t { Uint16, Uint32 };
enum class BindingType : uint8_t { UniformBuffer, StructuredBuffer, SampledTexture, Sampler };
enum class LoadOp : uint8_t { Load, Clear, DontCare };
enum class CompareFunc : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha, SrcColor, InvSrcColor };
enum class BlendOp : uint8_t { Add, Subtract, RevSubtract, Min, Max };
enum class CullMode : uint8_t { None, Front, Back };
enum class FillMode : uint8_t { Solid, Wireframe };

enum class ShaderStageFlags : uint8_t {  // BindGroupLayout 가시성
    None = 0, VertexBit = 1 << 0, PixelBit = 1 << 1, All = VertexBit | PixelBit,
};
constexpr ShaderStageFlags operator|(ShaderStageFlags a, ShaderStageFlags b) {
    return static_cast<ShaderStageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// ---------------------------------------------------------------------------
// 리소스 서술자
// ---------------------------------------------------------------------------
struct BufferDesc {
    uint64_t    size = 0;
    BufferUsage usage = BufferUsage::None;
    CpuAccess   access = CpuAccess::None;
    const char* debugName = nullptr;
};

struct TextureDesc {
    TextureDim   dim = TextureDim::Tex2D;
    Format       format = Format::RGBA8Unorm;
    uint32_t     width = 1, height = 1;
    uint16_t     depthOrLayers = 1;
    uint16_t     mips = 1;
    SampleCount  samples = SampleCount::X1;
    TextureUsage usage = TextureUsage::Sampled;
    const char*  debugName = nullptr;
};

struct TextureInitData {
    const void* data = nullptr;
    uint32_t    rowPitch = 0;     // 바이트 단위
    uint32_t    slicePitch = 0;   // 3D/배열용 (2D는 0 허용)
};

struct SamplerDesc {
    Filter      filter = Filter::Point;   // 픽셀아트 기본
    AddressMode u = AddressMode::Clamp, v = AddressMode::Clamp, w = AddressMode::Clamp;
    const char* debugName = nullptr;
};

// ---- 파이프라인 (DX12 PSO 형태 정본 — DX11은 개별 상태 객체로 분해·캐싱) ----
struct VertexAttribute {
    const char* semantic = "";        // HLSL 시맨틱 이름 ("POSITION", "TEXCOORD"...)
    uint32_t    semanticIndex = 0;
    Format      format = Format::RGB32Float;
    uint32_t    offset = 0;           // 버텍스 내 바이트 오프셋
    uint32_t    bufferSlot = 0;       // 버텍스 버퍼 슬롯 (M0은 0만 사용)
};

struct VertexLayoutDesc {
    std::span<const VertexAttribute> attributes;
    uint32_t stride = 0;              // 슬롯 0 스트라이드 (멀티 슬롯은 M1+)
};

struct BlendStateDesc {
    bool        enable = false;
    BlendFactor srcColor = BlendFactor::One,  dstColor = BlendFactor::Zero;
    BlendOp     colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One,  dstAlpha = BlendFactor::Zero;
    BlendOp     alphaOp = BlendOp::Add;
    uint8_t     writeMask = 0xF;      // RGBA

    static constexpr BlendStateDesc Opaque() { return {}; }
    static constexpr BlendStateDesc Premultiplied() {   // 엔진 표준 알파(02: premultiplied 통일)
        return {true, BlendFactor::One, BlendFactor::InvSrcAlpha, BlendOp::Add,
                      BlendFactor::One, BlendFactor::InvSrcAlpha, BlendOp::Add, 0xF};
    }
};

struct DepthStencilStateDesc {
    bool        depthTest = false;
    bool        depthWrite = false;
    CompareFunc depthFunc = CompareFunc::LessEqual;
    // 스텐실(실루엣 패스용)은 M2에서 확장
};

struct RasterizerStateDesc {
    CullMode cull = CullMode::None;   // 2D 쿼드 기본
    FillMode fill = FillMode::Solid;
    bool     scissorEnable = false;
    int32_t  depthBias = 0;
};

struct RenderTargetFormats {
    uint32_t colorCount = 1;
    Format   colorFormats[kMaxColorAttachments] = {Format::BGRA8Unorm};
    Format   depthStencilFormat = Format::Unknown;   // Unknown = 깊이 버퍼 없음
};

// ---- 바인딩 (WebGPU식 BindGroup — DX11은 슬롯 바인딩으로 평탄화) ----
struct BindGroupLayoutEntry {
    uint32_t         binding = 0;     // HLSL 레지스터 번호 (b#/t#/s# — type이 공간 결정)
    BindingType      type = BindingType::UniformBuffer;
    ShaderStageFlags visibility = ShaderStageFlags::All;
};

struct BindGroupLayoutDesc {
    std::span<const BindGroupLayoutEntry> entries;
    const char* debugName = nullptr;
};

struct BindGroupEntry {
    uint32_t    binding = 0;
    BindingType type = BindingType::UniformBuffer;
    // type에 따라 하나만 유효
    BufferHandle  buffer{};
    TextureHandle texture{};
    SamplerHandle sampler{};
    uint64_t bufferOffset = 0;
    uint64_t bufferSize = 0;          // 0 = 전체
};

struct BindGroupDesc {
    BindGroupLayoutHandle layout;
    std::span<const BindGroupEntry> entries;
    const char* debugName = nullptr;
};

struct GraphicsPipelineDesc {
    ShaderHandle vs, ps;
    VertexLayoutDesc      vertexLayout;
    BlendStateDesc        blend;
    DepthStencilStateDesc depthStencil;
    RasterizerStateDesc   raster;
    PrimitiveTopology     topology = PrimitiveTopology::TriangleList;
    RenderTargetFormats   rtFormats;                            // 생성 시점 고정 (DX12 PSO 호환)
    std::span<const BindGroupLayoutHandle> bindGroupLayouts;    // 슬롯 0..3 순
    const char* debugName = nullptr;
};

// ---- 렌더 패스·뷰포트·복사 ----
struct Viewport {
    float x = 0, y = 0, width = 0, height = 0;
    float minDepth = 0.0f, maxDepth = 1.0f;   // NDC 깊이 0~1 (02 규약)
};

struct RenderPassColorAttachment {
    TextureHandle texture;            // 백버퍼 또는 RenderTarget 텍스처
    LoadOp loadOp = LoadOp::Clear;
    Color  clearColor{0, 0, 0, 1};
};

struct RenderPassDepthStencilAttachment {
    TextureHandle texture;
    LoadOp  depthLoadOp = LoadOp::Clear;
    float   clearDepth = 1.0f;
    LoadOp  stencilLoadOp = LoadOp::DontCare;
    uint8_t clearStencil = 0;
};

struct RenderPassBeginDesc {
    std::span<const RenderPassColorAttachment> colorAttachments;
    const RenderPassDepthStencilAttachment* depthStencil = nullptr;   // null = 깊이 없음
    const char* debugName = nullptr;
};

struct TextureCopyRegion {
    uint32_t mipLevel = 0, arrayLayer = 0;
    uint32_t x = 0, y = 0, z = 0;
    uint32_t width = 0, height = 0, depth = 1;
};

// ---- 디바이스·스왑체인 ----
struct SwapChainDesc {
    uint32_t width = 0, height = 0;   // 0 = 창 클라이언트 크기 사용
    Format   format = Format::BGRA8UnormSrgb;   // 최종 sRGB 백버퍼 (02 색공간 규약)
    uint32_t bufferCount = 2;
    bool     allowTearing = false;    // vsync off + 무제한 프레임용
};

struct DeviceCreateInfo {
    bool enableDebugLayer =
#if MYE_DEBUG
        true;
#else
        false;
#endif
};

struct DeviceCaps {
    char     adapterName[128] = {};
    uint64_t dedicatedVideoMemoryBytes = 0;
    bool     supportsTimestamps = false;
};

} // namespace mye::rhi
