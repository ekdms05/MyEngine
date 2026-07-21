// mye/render/HybridRenderer.h — 하이브리드 깊이 렌더러 (M2-C 구현) (docs/02 §하이브리드 깊이 정렬)
//
// 이 엔진의 존재 이유. RenderExtract의 RenderProxyList(RenderItem 목록)를 입력받아 단일 깊이 버퍼를
// 공유하는 960×540 내부 RT(색+뎁스)에 렌더한다. 2D(스프라이트·타일)는 alpha cutout + 뎁스 기록,
// 깊이값은 DepthEncoder(밴드+sortKeyY)로 인코딩해 SV_Position.z로 출력한다. 3D 메시는 포워드
// 렌더하되 AnchorBiased 모드로 앵커 지면Y를 같은 깊이 함수에 매핑 → 스프라이트와 픽셀 단위 정렬.
//
// 패스 순서(docs/02): 불투명 3D → 타일/스프라이트(cutout,뎁스) → (반투명, M3) → 픽셀퍼펙트 업스케일.
//   ImGui/UI는 통합 단계(상위)가 담당. 단일 뎁스버퍼 공유가 핵심.
//
// [기존 존중] Camera2D/PixelPerfectTarget/SpriteBatch를 재작성하지 않고 소비·확장한다.
//   - 내부 RT+뎁스와 업스케일 블릿은 PixelPerfectTarget 소유(그대로 사용).
//   - 카메라 viewProj·픽셀스냅·서브픽셀 잔차는 Camera2D 소유(그대로 사용).
//   - 스프라이트/타일 정점은 본 렌더러의 하이브리드 뎁스 PSO로 직접 발행(SpriteBatch는 뎁스 미기록
//     M1 경로라 여기서 쓰지 않음 — 정렬 규약이 다르다).
//
// [사용 흐름] Init(device, desc) → 매 프레임:
//   RegisterMesh로 필요한 메시를 1회 업로드(GUID→MeshGpu) →
//   target.BeginScenePass(cmd, clear) → Render(items, camera, target 정보) → target.EndScenePass →
//   target.Blit(백버퍼) → Present. (자세한 배선은 apiForIntegration 참조.)
#pragma once

#include "mye/core/Math.h"
#include "mye/render/Camera2D.h"     // kInternalWidth/Height, kPixelsPerUnit (02 규약 재노출)
#include "mye/render/MeshTypes.h"
#include "mye/render/DepthEncoder.h"
#include "mye/asset/AssetGuid.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::rhi { class IDevice; class ICommandContext; }
namespace mye::asset { struct Texture; struct Mesh; }
namespace mye::scene { struct RenderItem; struct RenderProxyList; }

namespace mye::render {

class Camera2D;

struct HybridRendererDesc {
    rhi::Format colorFormat = rhi::Format::RGBA8Unorm;         // 내부 RT 컬러 포맷(PixelPerfectTarget과 일치)
    rhi::Format depthFormat = rhi::Format::D24UnormS8Uint;     // 단일 공유 뎁스 포맷
    float       alphaCutoff = 0.5f;                            // cutout discard 임계(a<cutoff → discard)
};

// 프레임 뷰 정보 — Render 호출당 카메라 상태 스냅샷.
struct HybridViewInfo {
    Mat4  viewProj = Mat4::Identity();   // Camera2D::ViewProjection() (row-vector, v*M)
    Mat4  view = Mat4::Identity();       // Camera2D::ViewMatrix() (메시 뎁스 계산·앵커 viewZ용)
    Mat4  proj = Mat4::Identity();       // Camera2D::ProjectionMatrix()
    DepthViewParams depth{};             // sortKeyY 정규화 범위(카메라 세로 뷰에서 산출)
    uint32_t viewportWidth  = kInternalWidth;
    uint32_t viewportHeight = kInternalHeight;
};

// GUID → 에셋 해석 콜백. 상위(통합/에셋)가 AssetManager로 해석해 넘긴다.
// (asset을 읽기만 하는 규약 준수 — 렌더러는 GUID를 직접 로드하지 않는다.)
// asset::Mesh/Texture는 이미 GPU 버퍼/텍스처 핸들을 보유(04 임포터 Finalize) → 렌더러는 바인딩만.
using TextureResolver = const asset::Texture* (*)(void* user, asset::AssetGuid guid);
using MeshResolver    = const asset::Mesh*    (*)(void* user, asset::AssetGuid guid);

// 하이브리드 렌더러. 구현 M2-C.
class HybridRenderer {
public:
    HybridRenderer() = default;
    ~HybridRenderer() = default;
    HybridRenderer(const HybridRenderer&) = delete;
    HybridRenderer& operator=(const HybridRenderer&) = delete;

    void Init(rhi::IDevice& device, const HybridRendererDesc& desc);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // ---- 자산 연결 ----
    // 텍스처 해석기(스프라이트/타일/메시 알베도 GUID → asset::Texture*).
    void SetTextureResolver(TextureResolver fn, void* user) { m_texResolver = fn; m_texUser = user; }
    // 메시 해석기(MeshRenderer.mesh GUID → asset::Mesh*, 04 임포터가 GPU 업로드 완료 상태).
    void SetMeshResolver(MeshResolver fn, void* user) { m_meshResolver = fn; m_meshUser = user; }

    // 방향광(3D 메시 라이팅) 설정. 기본: 위-앞에서 비추는 약한 방향광 + 앰비언트.
    void SetDirectionalLight(Vec3 dirWorld, Color color, float ambient);

    // ---- 프레임 렌더 ----
    // 활성 RenderPass(target.BeginScenePass로 내부 RT 바인딩·클리어) 안에서 호출한다.
    // items를 kind별로 분류·정렬해 (1) 불투명 3D 메시 → (2) 타일 청크 → (3) 스프라이트 순으로
    // 단일 뎁스버퍼에 발행한다. 뎁스 테스트/기록 ON, 스프라이트/타일은 alpha cutout.
    void Render(const scene::RenderProxyList& items, const Camera2D& camera,
                rhi::ICommandContext& ctx);

    // camera 없이 명시적 뷰 정보로 렌더(에디터 오프스크린·테스트용).
    void Render(const scene::RenderProxyList& items, const HybridViewInfo& view,
                rhi::ICommandContext& ctx);

    // ---- 통계(오버레이) ----
    struct Stats {
        uint32_t spriteCount = 0;
        uint32_t tileQuadCount = 0;
        uint32_t meshCount = 0;
        uint32_t drawCalls = 0;
    };
    const Stats& LastStats() const { return m_stats; }

    // Camera2D → HybridViewInfo 산출(뷰/프로젝션 + sortKeyY 정규화 범위). 통합 배선 헬퍼.
    static HybridViewInfo MakeViewInfo(const Camera2D& camera);

private:
    // 스프라이트/타일 공용 뎁스 인코딩 정점(월드 XY + 인코딩된 z + uv + 틴트 + flash).
    struct QuadVertex {
        float x, y, z;       // 월드 XY + 인코딩된 NDC 깊이(z는 VS에서 그대로 clip.z로 사용)
        float u, v;
        float r, g, b, a;
        float flash;      // 히트 플래시(0..1). VS는 TEXCOORD1 scalar(R32Float)로 읽음.
        float flashPad;   // 정렬 패딩(16B 배수 유지). 셰이더 입력 아님(레이아웃 미참조).
    };
    struct MeshInstanceCB;   // 정의는 .cpp
    struct FrameCB;

    void InitSpriteTilePipeline();
    void InitMeshPipeline();

    void EnsureQuadCapacity(uint32_t quadCount);
    rhi::BindGroupHandle GetOrCreateTextureBG(rhi::TextureHandle tex);

    // 패스별 수집·발행.
    void CollectAndDrawSpritesTiles(const scene::RenderProxyList& items, const HybridViewInfo& view,
                                    rhi::ICommandContext& ctx);
    void DrawMeshes(const scene::RenderProxyList& items, const HybridViewInfo& view,
                    rhi::ICommandContext& ctx);

    // 타일 청크 → 쿼드 정점(경사 램프 깊이 포함) 빌드. m_scratchQuads에 append.
    void BuildTileChunkQuads(const scene::RenderItem& item, const HybridViewInfo& view);
    // 스프라이트/빌보드 → 쿼드 정점(flat depth). m_scratchQuads에 append.
    void BuildSpriteQuad(const scene::RenderItem& item, const HybridViewInfo& view);

    const asset::Texture* Resolve(asset::AssetGuid guid) const;
    const asset::Mesh*    ResolveMesh(asset::AssetGuid guid) const;

    rhi::IDevice*     m_device = nullptr;
    HybridRendererDesc m_desc{};

    // --- 스프라이트/타일 파이프라인(하이브리드 뎁스 cutout) ---
    rhi::ShaderHandle          m_quadVs{};
    rhi::ShaderHandle          m_quadPs{};
    rhi::PipelineHandle        m_quadPipeline{};
    rhi::BufferHandle          m_quadVB{};              // 동적 링
    rhi::BufferHandle          m_frameCB{};             // b0: viewProj + cutoff
    rhi::SamplerHandle         m_sampler{};             // Point/Clamp
    rhi::BindGroupLayoutHandle m_frameLayout{};
    rhi::BindGroupLayoutHandle m_texLayout{};
    rhi::BindGroupHandle       m_frameBG{};
    uint32_t                   m_quadCapacity = 0;      // 쿼드 수용량

    // --- 3D 메시 파이프라인(포워드, 뎁스 테스트/기록) ---
    rhi::ShaderHandle          m_meshVs{};
    rhi::ShaderHandle          m_meshPs{};
    rhi::PipelineHandle        m_meshPipeline{};
    rhi::BufferHandle          m_meshCB{};              // b0: MVP + 라이팅 + 앵커 바이어스
    rhi::BindGroupLayoutHandle m_meshCbLayout{};
    rhi::BindGroupHandle       m_meshCbBG{};

    // 텍스처 BindGroup 캐시(스프라이트/타일/메시 공용).
    std::unordered_map<uint64_t, rhi::BindGroupHandle> m_texCache;

    // 스크래치(프레임 재사용).
    std::vector<QuadVertex> m_scratchQuads;

    TextureResolver m_texResolver = nullptr;
    void*           m_texUser = nullptr;
    MeshResolver    m_meshResolver = nullptr;
    void*           m_meshUser = nullptr;

    Vec3  m_lightDir{-0.3f, -0.6f, 0.5f};   // 위-앞에서 비추는 방향(월드). 정규화는 셰이더.
    Color m_lightColor = Color::White();
    float m_ambient = 0.35f;

    Stats m_stats{};
    bool  m_initialized = false;
};

} // namespace mye::render
