// mye/render/PixelPerfectTarget.h — 내부 RT + 정수배 업스케일 (M1-B 구현)
//                                                              (docs/02 §해상도·업스케일)
// 책임: 고정 내부 해상도(기본 960×540) 오프스크린 RT에 월드를 그린 뒤,
// 창 크기에 맞춰 **정수배**로 업스케일하고 레터박스(필러박스)를 적용해 백버퍼에 블릿한다.
// 픽셀 스냅 카메라의 서브픽셀 잔여를 업스케일 UV 오프셋으로 되돌려 지터를 방지한다.
//
// 블릿: 풀스크린 삼각형을 destRect 뷰포트로 그려 내부 RT를 포인트 샘플. 서브픽셀 오프셋을
//       UV에 반영. 백버퍼 전체를 먼저 검은색으로 클리어(레터박스 바) 후 블릿.
#pragma once

#include "mye/core/Math.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>

namespace mye::rhi { class IDevice; class ICommandContext; }

namespace mye::render {

struct PixelPerfectDesc {
    uint32_t internalWidth  = 960;
    uint32_t internalHeight = 540;
    rhi::Format colorFormat = rhi::Format::RGBA8Unorm;
    bool     useDepth = true;
    rhi::Format depthFormat = rhi::Format::D24UnormS8Uint;
    // 백버퍼 포맷(블릿 PSO 고정용). 스왑체인 GetFormat()과 일치해야 한다.
    rhi::Format backbufferFormat = rhi::Format::BGRA8UnormSrgb;
};

// 창 크기에 대한 정수배·레터박스 산출 결과.
struct UpscaleLayout {
    uint32_t scale = 1;         // 정수 스케일(1080p→2, 4K→4). 최소 1.
    RectInt  destRect{};        // 백버퍼 내 그리기 사각형(레터박스 뺀 중앙 영역, px)
};

// 내부 RT + 정수배 업스케일 타깃. 구현 M1-B.
//
// 사용: Init → (프레임) [BeginScenePass로 내부 RT 바인딩·클리어] → 스프라이트 그리기 →
//       EndScenePass → Blit(백버퍼, 창크기, 카메라 서브픽셀잔차) → (종료) Shutdown.
class PixelPerfectTarget {
public:
    PixelPerfectTarget() = default;
    ~PixelPerfectTarget() = default;
    PixelPerfectTarget(const PixelPerfectTarget&) = delete;
    PixelPerfectTarget& operator=(const PixelPerfectTarget&) = delete;

    void Init(rhi::IDevice& device, const PixelPerfectDesc& desc);
    void Shutdown();

    rhi::TextureHandle ColorTarget() const;   // 월드 패스 컬러 어태치먼트(Sampled|RenderTarget)
    rhi::TextureHandle DepthTarget() const;   // 월드 패스 뎁스(없으면 무효 핸들)

    uint32_t Width()  const { return m_desc.internalWidth; }
    uint32_t Height() const { return m_desc.internalHeight; }
    rhi::Format ColorFormat() const { return m_desc.colorFormat; }
    bool DepthEnabled() const { return m_desc.useDepth; }

    // 내부 RT를 컬러/뎁스 어태치먼트로 바인딩하고 클리어한 뒤 뷰포트를 내부 해상도로 설정한다.
    // (스프라이트 배처 End 이전에 호출. EndScenePass로 닫는다.)
    void BeginScenePass(rhi::ICommandContext& ctx, Color clearColor);
    void EndScenePass(rhi::ICommandContext& ctx);

    // 창 크기 → 정수배·레터박스 레이아웃(순수 함수).
    static UpscaleLayout ComputeLayout(Vec2i internalSize, Vec2i windowSize);

    // 내부 RT를 백버퍼로 정수배 업스케일 블릿(포인트 샘플 + 서브픽셀 UV 오프셋 환원).
    // subpixelOffset = 카메라 SubpixelResidual()(내부 RT 픽셀, +Y 아래). 백버퍼를 자체 RenderPass로
    // 열어 검은색 클리어(레터박스) 후 destRect 뷰포트에 풀스크린 삼각형을 그린다.
    void Blit(rhi::ICommandContext& ctx, rhi::TextureHandle backbuffer,
              Vec2i windowSize, Vec2 subpixelOffset);

    bool IsInitialized() const { return m_initialized; }

private:
    rhi::IDevice*             m_device = nullptr;
    PixelPerfectDesc          m_desc{};
    rhi::TextureHandle        m_color{};
    rhi::TextureHandle        m_depth{};

    // 블릿 파이프라인
    rhi::SamplerHandle        m_sampler{};          // Point/Clamp
    rhi::BufferHandle         m_blitConstants{};     // b0: uvScale/uvOffset
    rhi::ShaderHandle         m_blitVs{};
    rhi::ShaderHandle         m_blitPs{};
    rhi::PipelineHandle       m_blitPipeline{};
    rhi::BindGroupLayoutHandle m_blitLayout{};       // t0(내부RT)+s0(sampler)+b0(constants)
    rhi::BindGroupHandle      m_blitBindGroup{};
    bool                      m_initialized = false;
};

} // namespace mye::render
