// mye/render/SpriteBatch.h — 스프라이트 배처 (M1-B 구현) (docs/02 §스프라이트 배칭)
//
// 책임: 동일 텍스처(아틀라스)·머티리얼·블렌드의 쿼드를 동적 정점 버퍼로 모아
// 최소 드로우콜로 제출한다. premultiplied alpha 블렌드(02 통일 규약), 포인트 샘플.
// CPU Y-sort(sortY 기준 안정 정렬) 후 텍스처가 바뀌는 지점마다 드로우콜을 분할한다.
// 06 UI·디버그 드로잉도 이 저수준 2D API를 소비한다. 텍셀:픽셀 1:1(PixelPerfect).
//
// 셰이더: VS = 카메라 뷰프로젝션 변환. PS = 텍스처 샘플 × premultiplied 틴트
//         + 히트플래시 화이트 블렌드(lerp(rgb, white, flash), 알파 보존).
#pragma once

#include "mye/core/Math.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>
#include <vector>

namespace mye::rhi { class IDevice; class ICommandContext; }

namespace mye::render {

// 한 스프라이트 제출 단위.
//
// UV 규약: uv는 정규화(0..1) 소스 사각형(좌상단 원점, +V 아래 — DX). 아틀라스 서브렉트는
//   소비자(통합 SpriteRenderer)가 텍스처 크기로 나눠 채운다. 배처는 텍스처 크기를 모른다.
//   기본값 {0,0,1,1} = 텍스처 전체.
// 피벗: pivot은 스프라이트 크기 대비 정규화 비율(0..1). {0.5,1} = 하단 중앙(발밑, 기본).
//   position이 이 피벗 지점에 놓인다. (픽셀 피벗 / 소스픽셀크기 = 정규화 피벗)
struct SpriteDraw {
    Vec2               position{};      // 월드(unit) — 보간 완료·스냅 전 위치(피벗 기준점)
    Vec2               size{};          // 월드 크기(unit). 0,0이면 uv 크기×텍스처는 모르므로 1×1 unit
    Rect               uv{0, 0, 1, 1};  // 정규화 소스 사각형(0..1, +V 아래)
    rhi::TextureHandle texture{};       // 샘플할 텍스처(아틀라스)
    Color              tint = Color::White();   // 곱셈 틴트(straight color, 셰이더가 premultiply)
    float              flashAmount = 0.0f;      // 히트 플래시 강도 0~1 (화이트 블렌드)
    float              sortY = 0.0f;            // Y-sort 키(발밑 월드 Y; 오름차순으로 그림)
    Vec2               pivot{0.5f, 1.0f};       // 정규화 피벗(0..1). 기본=하단 중앙(발밑).
};

// 배치별 통계(오버레이용).
struct SpriteBatchStats {
    uint32_t drawCalls = 0;
    uint32_t sprites   = 0;
};

// 스프라이트 배처. 구현 M1-B.
//
// 사용: Init → (프레임) Begin(vp) → Submit(...)×N → End(ctx) [→ 통계 조회] → (종료) Shutdown.
// End는 반드시 활성 RenderPass(내부 RT 바인딩) 안에서 호출한다.
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch() = default;
    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    // GPU 동적 버퍼·파이프라인·상수버퍼 준비. device 수명은 배처보다 길어야 함.
    // colorFormat = 그릴 렌더타겟(내부 RT)의 컬러 포맷(PSO 고정용).
    void Init(rhi::IDevice& device, rhi::Format colorFormat, bool depthEnabled);
    void Shutdown();

    void Begin(const Mat4& viewProj);                 // 프레임 상수(뷰프로젝션) 세팅·수집 리셋
    void Submit(const SpriteDraw& draw);              // 스프라이트 누적(정렬은 End에서)
    void End(rhi::ICommandContext& ctx);              // Y-sort·정점 채우기·드로우콜 발행

    uint32_t DrawCallCount() const;   // 직전 End의 드로우콜 수(ImGui 오버레이용)
    uint32_t QuadCount() const;       // 직전 End의 스프라이트(쿼드) 수
    const SpriteBatchStats& Stats() const { return m_stats; }

    bool IsInitialized() const { return m_initialized; }

private:
    struct Vertex {                   // POSITION(월드 xy) + TEXCOORD0(uv) + COLOR(틴트 rgba) + TEXCOORD1(flash,pad)
        float x, y;
        float u, v;
        float r, g, b, a;
        float flash;
        float flashPad = 0.0f;        // TEXCOORD1을 RG32Float로 읽을 때 오버런 방지 패딩
    };

    void EnsureCapacity(uint32_t spriteCount);

    rhi::IDevice*             m_device = nullptr;
    rhi::BufferHandle         m_vertexBuffer{};     // 동적 링(WriteDiscardRing)
    rhi::BufferHandle         m_constantBuffer{};   // b0: viewProj
    rhi::SamplerHandle        m_sampler{};          // Point/Clamp
    rhi::ShaderHandle         m_vs{};
    rhi::ShaderHandle         m_ps{};
    rhi::PipelineHandle       m_pipeline{};
    rhi::BindGroupLayoutHandle m_frameLayout{};     // slot0: b0(viewProj)
    rhi::BindGroupLayoutHandle m_materialLayout{};  // slot2: t0(texture)+s0(sampler)
    rhi::BindGroupHandle      m_frameBindGroup{};

    uint32_t                  m_vbCapacitySprites = 0;   // 정점 버퍼 수용 스프라이트 수
    Mat4                      m_viewProj{};
    std::vector<SpriteDraw>   m_pending;
    SpriteBatchStats          m_stats{};
    bool                      m_initialized = false;
};

} // namespace mye::render
