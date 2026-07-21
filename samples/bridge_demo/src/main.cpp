// bridge_demo — M2 통합 데모·검증 (docs/00 §7 M2 완료 기준)
//
// 이 엔진의 존재 이유인 하이브리드 깊이 정렬을 실제 화면으로 증명한다.
// 테스트 맵: 지면 + 그 위를 가로지르는 다리(다중 컬럼) + 경사로 + 3D 석상.
//   - 캐릭터 A: 다리 위(floor1)  — cyan
//   - 캐릭터 B: 지면(floor0)      — magenta
//   - 플레이어 P: 조작(floor0)     — yellow
//
// 관통 계층: GuardedMain · Application · RHI(DX11) · InputState · AssetManager(PNG/GLB) ·
//   ECS World · SystemScheduler(RenderExtract) · PhysicsWorld2D · TilemapWorld ·
//   RenderExtract → HybridRenderer(단일 뎁스버퍼) · PixelPerfectTarget · DebugUi.
//
// 검증 CLI:
//   --scenario <name>  결정적 위치 배치. name ∈ {over_bridge, under_bridge, behind_statue,
//                      front_statue, slope, free}
//   --dump path.bmp    마지막 프레임 백버퍼 BMP 저장
//   --frames N         N 렌더 프레임 후 exit 0
//   --resize-test      실행 중 창 2회 리사이즈
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/core/platform/Win32Window.h"

#include "mye/rhi/Rhi.h"

#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/asset/Texture.h"
#include "mye/asset/Mesh.h"
#include "mye/asset/MeshImporter.h"

#include "mye/ecs/World.h"
#include "mye/ecs/View.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/tilemap/Tilemap.h"
#include "mye/phys/PhysicsWorld2D.h"

#include "mye/render/Camera2D.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/DepthEncoder.h"

#include "mye/imgui/DebugUi.h"
#include "imgui.h"

#include <Windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace rhi = mye::rhi;
namespace asset = mye::asset;
namespace render = mye::render;
namespace ecs = mye::ecs;
namespace scene = mye::scene;
namespace tilemap = mye::tilemap;
namespace phys = mye::phys;

using mye::Vec2;
using mye::Vec2i;
using mye::Vec3;
using mye::Color;
using mye::Mat4;
using mye::Quat;

constexpr float kMoveSpeed = 4.0f;   // unit/s
constexpr Color kClearColor{0.05f, 0.06f, 0.09f, 1.0f};

// ---------------------------------------------------------------------------
// 씬 좌표 규약 (world unit, +Y 위)
// ---------------------------------------------------------------------------
// 지면 세계 Y ≈ 0. 다리는 그 위를 동서(±X)로 가로지른다. 다리 상판 논리 Y도 지면과 같은 위치
// (다리는 "위층 floor1"이지 물리적으로 높이 오프셋을 주지 않는다 — 밴드 분리로 깊이를 가른다).
//
// 타일 렌더(HybridRenderer::BuildTileChunkQuads): 셀(cx,cy) → 월드 (x=cx, yBottom=-cy).
// 따라서 월드 Y = W 를 원하면 cellY = -W 로 배치한다.
constexpr int   kBridgeMinX = -3;   // 다리가 덮는 X 셀 범위(월드 X)
constexpr int   kBridgeMaxX =  3;
constexpr int   kBridgeCellY = 0;   // 다리/지면 겹침 밴드가 놓이는 셀 Y(월드 Y=0)

// 석상 위치(월드).
constexpr Vec3  kStatuePos{6.0f, 0.0f, 0.0f};
constexpr float kStatueScale = 1.4f;

// 경사로 위치: 지면 좌측. 셀 Y 범위(월드 Y = -cellY 로 화면 표시됨).
constexpr int   kSlopeCellX      = -8;
constexpr int   kSlopeCellYTop   = -2;   // 월드 Y = +2 (화면 위 = 램프 상단)
constexpr int   kSlopeCellYBottom = 2;   // 월드 Y = -2 (화면 아래 = 램프 하단)
// 램프 시각 높이: 램프 하단(월드 Y=-2)에서 0, 상단(월드 Y=+2)에서 kSlopeMaxRise.
constexpr float kSlopeMaxRise = 2.0f;    // world unit(4 레벨)
inline float SlopeGroundY(float worldY) {
    const float lo = -2.0f, hi = 2.0f;   // 램프가 덮는 월드 Y 구간
    float t = (worldY - lo) / (hi - lo);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return worldY + kSlopeMaxRise * t;   // 지면 Y + 램프 상승분
}

enum class Scenario { Free, OverBridge, UnderBridge, BehindStatue, FrontStatue, Slope };

Scenario ParseScenario(const std::string& s) {
    if (s == "over_bridge")   return Scenario::OverBridge;
    if (s == "under_bridge")  return Scenario::UnderBridge;
    if (s == "behind_statue") return Scenario::BehindStatue;
    if (s == "front_statue")  return Scenario::FrontStatue;
    if (s == "slope")         return Scenario::Slope;
    return Scenario::Free;
}

struct RunControl {
    mye::Application* app = nullptr;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    std::string dumpPath;
    bool     resizeTest = false;
    Scenario scenario = Scenario::Free;
    bool     scenarioSet = false;
};

// GUID → 로드된 에셋 포인터 해석기(렌더러 콜백용).
struct AssetResolvers {
    std::unordered_map<uint64_t, const asset::Texture*> textures;
    std::unordered_map<uint64_t, const asset::Mesh*>    meshes;

    static uint64_t Key(asset::AssetGuid g) { return g.hi ^ (g.lo * 1099511628211ull); }

    static const asset::Texture* ResolveTexture(void* user, asset::AssetGuid guid) {
        auto* self = static_cast<AssetResolvers*>(user);
        auto it = self->textures.find(Key(guid));
        return it == self->textures.end() ? nullptr : it->second;
    }
    static const asset::Mesh* ResolveMesh(void* user, asset::AssetGuid guid) {
        auto* self = static_cast<AssetResolvers*>(user);
        auto it = self->meshes.find(Key(guid));
        return it == self->meshes.end() ? nullptr : it->second;
    }
};

class BridgeDemoModule final : public mye::IModule {
public:
    explicit BridgeDemoModule(RunControl* control) : m_control(control) {}

    const char* GetName() const override { return "BridgeDemo"; }

    void OnInitialize(mye::EngineContext& ctx) override {
        m_ctx = &ctx;
        m_bus = &ctx.Events();
        m_input = ctx.GetService<mye::InputState>();
        mye::IWindow& window = ctx.MainWindow();

        auto deviceResult = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!deviceResult) { Fatal("CreateDevice failed", -10); return; }
        m_device = std::move(deviceResult).Value();

        rhi::SwapChainDesc scDesc{};
        scDesc.width = 0; scDesc.height = 0;
        scDesc.format = rhi::Format::BGRA8UnormSrgb;
        m_swapChain = m_device->CreateSwapChain(window.GetNativeHandle(), scDesc);
        if (!m_swapChain) { Fatal("CreateSwapChain failed", -11); return; }

        // VFS + AssetManager (assets:// → samples/bridge_demo/assets)
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        std::string assetsDir = ResolveAssetsDir();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(assetsDir), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::MeshImporter>());

        if (!LoadAssets(assetsDir)) return;

        // 픽셀퍼펙트 RT (색 + 단일 공유 뎁스).
        render::PixelPerfectDesc ppd{};
        ppd.useDepth = true;
        ppd.backbufferFormat = m_swapChain->GetFormat();
        m_rt.Init(*m_device, ppd);
        if (!m_rt.IsInitialized()) { Fatal("PixelPerfectTarget init failed", -13); return; }

        // 하이브리드 렌더러 — 내부 RT 포맷과 정합.
        render::HybridRendererDesc hrd{};
        hrd.colorFormat = m_rt.ColorFormat();
        hrd.depthFormat = rhi::Format::D24UnormS8Uint;
        hrd.alphaCutoff = 0.5f;
        m_hybrid.Init(*m_device, hrd);
        if (!m_hybrid.IsInitialized()) { Fatal("HybridRenderer init failed", -14); return; }
        m_hybrid.SetTextureResolver(&AssetResolvers::ResolveTexture, &m_resolvers);
        m_hybrid.SetMeshResolver(&AssetResolvers::ResolveMesh, &m_resolvers);
        m_hybrid.SetDirectionalLight(Vec3{-0.3f, -0.6f, 0.5f}, Color::White(), 0.45f);

        // 카메라(픽셀 스냅).
        render::Camera2DDesc camDesc{};
        camDesc.position = {0.0f, 0.0f};
        camDesc.pixelSnap = true;
        m_camera = render::Camera2D(camDesc);

        BuildWorld();
        ApplyScenario();

        // ImGui 오버레이.
        auto& win32Window = static_cast<mye::win32::Win32Window&>(window);
        auto uiRes = m_debugUi.Initialize(win32Window, *m_device);
        if (uiRes) m_debugUi.AttachInput(m_input);

        m_onResized = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<mye::WindowResizedEvent>([this](const mye::WindowResizedEvent& e) {
                if (m_swapChain && e.clientSize.x > 0 && e.clientSize.y > 0)
                    m_swapChain->Resize(static_cast<uint32_t>(e.clientSize.x),
                                        static_cast<uint32_t>(e.clientSize.y));
                return false;
            })};

        MYE_LOG_INFO("BridgeDemo", "initialized (assetsDir='{}', scenario={})",
                     assetsDir, static_cast<int>(m_control->scenario));
    }

    void OnPostInitialize(mye::EngineContext& ctx) override {
        ctx.Modules().AddTick(this, mye::UpdatePhase::FixedUpdate,
                              [this](const mye::TimeStep& s) { FixedUpdate(s); }, 0);
        ctx.Modules().AddTick(this, mye::UpdatePhase::Update,
                              [this](const mye::TimeStep& s) { Update(s); }, 0);
        ctx.Modules().AddTick(this, mye::UpdatePhase::PreRender,
                              [this](const mye::TimeStep& s) { Render(s); }, 0);
    }

    void OnShutdown(mye::EngineContext& /*ctx*/) override {
        m_onResized.Reset();
        m_debugUi.Shutdown();
        m_hybrid.Shutdown();
        m_rt.Shutdown();
        m_meshHandle = {};
        m_texHandles.clear();
        m_world.reset();
        m_scheduler.reset();
        m_tilemap.reset();
        m_physics.reset();
        m_assets.reset();
        m_vfs.reset();
        m_swapChain.reset();
        m_device.reset();
        MYE_LOG_INFO("BridgeDemo", "shutdown ({} frames)", m_frameCount);
    }

private:
    void Fatal(const char* msg, int code) {
        MYE_LOG_FATAL("BridgeDemo", "{}", msg);
        m_control->app->RequestExit(code);
    }

    std::string ResolveAssetsDir() const {
        namespace fs = std::filesystem;
        auto has = [](const fs::path& dir) {
            std::error_code ec;
            return fs::exists(dir / "hero_A.png", ec);
        };
        wchar_t exePathW[MAX_PATH] = {};
        if (::GetModuleFileNameW(nullptr, exePathW, MAX_PATH) > 0) {
            fs::path dir = fs::path(exePathW).parent_path();
            for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
                const fs::path cand = dir / "samples" / "bridge_demo" / "assets";
                if (has(cand)) return cand.string();
                dir = dir.parent_path();
            }
        }
        const char* candidates[] = {
            "samples/bridge_demo/assets", "../samples/bridge_demo/assets",
            "../../samples/bridge_demo/assets", "../../../samples/bridge_demo/assets", "assets",
        };
        for (const char* c : candidates) if (has(fs::path(c))) return c;
        return "samples/bridge_demo/assets";
    }

    // 텍스처 로드 + 리졸버 등록. 반환 GUID를 렌더러블에 심는다.
    asset::AssetGuid LoadTex(const char* vpath) {
        auto h = m_assets->LoadSync<asset::Texture>(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) {
            MYE_LOG_ERROR("BridgeDemo", "texture load failed: {}", vpath);
            return {};
        }
        asset::AssetGuid g = h.Guid();
        m_resolvers.textures[AssetResolvers::Key(g)] = h.Get();
        m_texHandles.push_back(std::move(h));
        return g;
    }

    bool LoadAssets(const std::string& /*dir*/) {
        m_guidHeroA  = LoadTex("assets://hero_A.png");
        m_guidHeroB  = LoadTex("assets://hero_B.png");
        m_guidHeroP  = LoadTex("assets://hero_P.png");
        m_guidGround = LoadTex("assets://tile_ground.png");
        m_guidBridge = LoadTex("assets://tile_bridge.png");
        m_guidSlope  = LoadTex("assets://tile_slope.png");
        m_guidWhite  = LoadTex("assets://white.png");

        m_meshHandle = m_assets->LoadSync<asset::Mesh>("assets://mesh/cube.glb");
        if (m_meshHandle.State() != asset::AssetState::Loaded || !m_meshHandle.Get()) {
            Fatal("cube.glb load failed", -12);
            return false;
        }
        m_guidStatue = m_meshHandle.Guid();
        m_resolvers.meshes[AssetResolvers::Key(m_guidStatue)] = m_meshHandle.Get();

        if (!m_guidHeroA.IsValid() || !m_guidGround.IsValid() || !m_guidBridge.IsValid()) {
            Fatal("essential textures failed to load", -12);
            return false;
        }
        return true;
    }

    // 타일 컬럼 헬퍼(월드 Y=worldY 밴드에 지면/다리/경사 배치).
    void SetGroundCell(int cellX, int cellY) {
        tilemap::TileColumn c{};
        c.tile = 1;
        c.baseHeightLevel = 0;
        c.layer = 0;
        m_tilemap->GetOrCreateLayer(0).SetTile({cellX, cellY}, c);
    }
    void SetSlopeCell(int cellX, int cellY, tilemap::SlopeType slope, int baseLevel) {
        tilemap::TileColumn c{};
        c.tile = 1;
        c.baseHeightLevel = baseLevel;
        c.slope = slope;
        c.layer = 0;
        m_tilemap->GetOrCreateLayer(0).SetTile({cellX, cellY}, c);
    }
    void SetBridgeCell(int cellX, int cellY) {
        tilemap::TileColumn c{};
        c.tile = 1;
        c.baseHeightLevel = 0;
        c.flags = tilemap::ColumnFlags::OneWayBridge | tilemap::ColumnFlags::Bridge;
        c.layer = 1;   // 다리 렌더 레이어(별도 TilemapRenderer가 이 layer만 추출)
        m_tilemap->GetOrCreateLayer(1).AddBridge({cellX, cellY}, c);
    }

    void BuildWorld() {
        m_world = std::make_unique<ecs::World>();
        m_scheduler = std::make_unique<scene::SystemScheduler>(*m_world, m_bus);
        m_tilemap = std::make_unique<tilemap::TilemapWorld>();
        m_physics = std::make_unique<phys::PhysicsWorld2D>();

        // ---- 지면(넓은 평지) : 화면 전체를 덮는 셀 그리드 ----
        // 월드 Y = -cellY. 화면 세로 ±6 unit 정도 → cellY ∈ [-6, 6] 커버.
        for (int cy = -8; cy <= 8; ++cy)
            for (int cx = -14; cx <= 14; ++cx)
                SetGroundCell(cx, cy);

        // ---- 경사로(좌측): 지면과 플러시한 완경사 램프(깊이 램프) ----
        // baseHeightLevel=0으로 지면과 같은 평면에 두어 배경 갭이 생기지 않게 하고, SlopeType으로
        // 램프 깊이를 부여한다(HybridRenderer가 셀 상/하단 sortKeyY를 각각 인코딩). 캐릭터의 시각
        // 높이는 데모가 직접 산출한다(SlopeGroundY): 타일맵 SampleHeight는 이제 렌더러와 동일한
        // Y 규약(worldY=-cellY, tilemap::CellToWorldY)을 공유하지만, 데모의 램프는 baseLevel=0
        // 플러시 슬래브 위에 kSlopeMaxRise 시각 상승을 얹는 별도 파라미터(월드 Y 구간 [-2,2])라
        // SampleHeight의 heightLevel 기반 경사와 산식이 달라 데모가 직접 SlopeGroundY로 산출한다.
        for (int cy = kSlopeCellYTop; cy <= kSlopeCellYBottom; ++cy) {
            SetSlopeCell(kSlopeCellX,     cy, tilemap::SlopeType::GentleN, 0);
            SetSlopeCell(kSlopeCellX + 1, cy, tilemap::SlopeType::GentleN, 0);
        }

        // ---- 다리(중앙): 동서로 가로지르는 상판(다중 컬럼) ----
        // 다리 상판은 지면과 같은 월드 Y=0 밴드에 놓이되 layer1(World1 밴드)로 추출된다.
        for (int cx = kBridgeMinX; cx <= kBridgeMaxX; ++cx)
            SetBridgeCell(cx, kBridgeCellY);

        // ---- TilemapRenderer 엔티티 2개(지면 layer0, 다리 layer1) ----
        // 지면: Ground 밴드(항상 뒤).
        {
            ecs::Entity e = m_world->Create();
            m_world->Add<scene::LocalTransform>(e);
            m_world->Add<scene::WorldTransform>(e);
            auto& tr = m_world->Add<scene::TilemapRenderer>(e);
            tr.runtime = m_tilemap.get();
            tr.map.guid = m_guidGround;                     // 타일셋 텍스처(지면)
            tr.sort.sortLayer = render::kSortLayerGround;   // 2
            tr.sort.orderInLayer = render::kOrderTileGround; // 같은 밴드 캐릭터보다 뒤로
            m_groundTilemapEntity = e;
        }
        // 다리 상판: World1 밴드(floor1)로 — B(World0) 앞, A(World1)와 동일 밴드.
        // TilemapRenderer는 floorLevel을 안 보므로 별도 엔티티에서 sortLayer를 직접 지정하고,
        // 추출된 청크의 tileLayer를 layer1로 필터해야 한다. 현재 TilemapRenderer는 runtime의
        // 모든 레이어를 순회하므로, 다리 전용 TilemapWorld를 별도로 두어 분리한다.
        m_bridgeTilemap = std::make_unique<tilemap::TilemapWorld>();
        for (int cx = kBridgeMinX; cx <= kBridgeMaxX; ++cx) {
            tilemap::TileColumn c{};
            c.tile = 1; c.baseHeightLevel = 0;
            c.flags = tilemap::ColumnFlags::OneWayBridge | tilemap::ColumnFlags::Bridge;
            c.layer = 0;
            m_bridgeTilemap->GetOrCreateLayer(0).AddBridge({cx, kBridgeCellY}, c);
        }
        {
            ecs::Entity e = m_world->Create();
            // 다리 상판은 World1 밴드(floor1). B(World0)는 밴드가 통째로 뒤라 항상 가려지고,
            // 같은 World1의 A는 orderInLayer 타이브레이커로 상판보다 앞에 그려진다(MakeCharacter 참조).
            m_world->Add<scene::LocalTransform>(e);
            m_world->Add<scene::WorldTransform>(e);
            auto& tr = m_world->Add<scene::TilemapRenderer>(e);
            tr.runtime = m_bridgeTilemap.get();
            tr.map.guid = m_guidBridge;                           // 타일셋 텍스처(다리 상판)
            tr.sort.sortLayer = render::FloorLevelToSortLayer(1);  // World1 = 101
            tr.sort.orderInLayer = render::kOrderTileGround;       // 같은 World1 캐릭터 A보다 뒤로
            m_bridgeTilemapEntity = e;
        }

        // ---- 3D 석상(cube) : AnchorBiased, floor0(World0) ----
        {
            ecs::Entity e = m_world->Create();
            auto& lt = m_world->Add<scene::LocalTransform>(e);
            lt.position = kStatuePos;
            // 세로로 긴 기둥(석상). AnchorBiased 바이어스는 이제 메시 뷰Z 범위로 정규화되어
            // Z-두께와 무관(편차 ±biasEps/2 고정)하므로 두께는 정렬에 영향 없다. 시각적 비례로
            // Z=0.35 슬래브 유지(두껍게 해도 앞/뒤 캐릭터 정렬은 침범하지 않음 — 두께 독립 확인됨).
            lt.scale = {kStatueScale, kStatueScale * 1.8f, 0.35f};
            m_world->Add<scene::WorldTransform>(e);
            auto& mr = m_world->Add<scene::MeshRenderer>(e);
            mr.mesh.guid = m_guidStatue;
            mr.material.guid = m_guidWhite;
            mr.depthMode = static_cast<uint8_t>(render::DepthMode::AnchorBiased);
            mr.sort.orderInLayer = 0;
            m_world->Add<scene::FloorLevel>(e).level = 0;
            m_statueEntity = e;
        }

        // ---- 캐릭터 A(다리 위, floor1) ----
        m_charA = MakeCharacter(m_guidHeroA, Vec3{0.0f, 0.0f, 0.0f}, 1);
        // ---- 캐릭터 B(지면, floor0) ----
        m_charB = MakeCharacter(m_guidHeroB, Vec3{0.0f, 0.0f, 0.0f}, 0);
        // ---- 플레이어 P(조작, floor0) ----
        m_player = MakeCharacter(m_guidHeroP, Vec3{-2.0f, 3.0f, 0.0f}, 0);

        // RenderExtract 시스템 등록(RenderExtract 페이즈).
        scene::RegisterRenderExtractSystem(*m_scheduler, m_proxies);
    }

    ecs::Entity MakeCharacter(asset::AssetGuid tex, Vec3 pos, int8_t floor) {
        ecs::Entity e = m_world->Create();
        auto& lt = m_world->Add<scene::LocalTransform>(e);
        lt.position = pos;
        m_world->Add<scene::WorldTransform>(e);
        auto& sr = m_world->Add<scene::SpriteRenderer>(e);
        sr.sprite.guid = tex;
        sr.pivotPx = {12.0f, 32.0f};             // 발밑(하단 중앙, 24x32 스프라이트)
        // 캐릭터는 자신이 밟고 선 "같은 밴드 바닥 타일/다리 상판"보다 항상 앞에 그려져야 한다.
        // 관례 상수 kOrderCharacter(+300)로 밴드 내부에서 앞으로 당긴다(타일은 kOrderTileGround
        // -300으로 뒤로 밀림). 상대차 0.006이 지면/상판을 이기되, 같은 밴드 3D 석상(order 0)과는
        // sortKeyY 정렬이 우선하도록 절제된 값이다(behind_statue 마진 확보). EncodeDepth의
        // band.width×kOrderMaxBandFraction 클램프가 인접 밴드 불침범을 보증한다.
        sr.sort.orderInLayer = render::kOrderCharacter;
        m_world->Add<scene::FloorLevel>(e).level = floor;
        return e;
    }

    void SetCharPos(ecs::Entity e, Vec3 pos, int8_t floor) {
        if (auto* lt = m_world->TryGet<scene::LocalTransform>(e)) {
            lt->position = pos; lt->dirty = true;
        }
        if (auto* fl = m_world->TryGet<scene::FloorLevel>(e)) fl->level = floor;
    }

    // 시나리오별 결정적 캐릭터 배치.
    void ApplyScenario() {
        const Scenario sc = m_control->scenario;
        // 기본(free): A는 다리 위 중앙, B는 다리 아래, P는 좌상.
        SetCharPos(m_charA, Vec3{0.0f, 0.0f, 0.0f}, 1);
        SetCharPos(m_charB, Vec3{0.0f, 0.0f, 0.0f}, 0);
        SetCharPos(m_player, Vec3{-2.0f, 3.0f, 0.0f}, 0);

        switch (sc) {
            case Scenario::OverBridge:
                // A(floor1) & B(floor0) 같은 화면 X에서 겹침. A 보이고 B는 상판에 가려짐.
                SetCharPos(m_charA, Vec3{0.0f, 0.0f, 0.0f}, 1);
                SetCharPos(m_charB, Vec3{0.0f, 0.0f, 0.0f}, 0);
                SetCharPos(m_player, Vec3{-16.0f, 4.0f, 0.0f}, 0);   // 화면 밖 근처
                break;
            case Scenario::UnderBridge:
                // B가 다리 상판 바로 아래 통과 순간: 상판(월드 Y 0..1, 화면 222..270)이 B 상반신을
                // 가리고, B 발밑은 상판 앞(아래) 가장자리 밖으로 삐져나와 보인다. B 발밑 월드 Y=-0.5
                // (화면 ~294) → 다리(밑변 월드 Y=0=화면 270) 아래로 다리·발이 노출된다.
                SetCharPos(m_charA, Vec3{-16.0f, 4.0f, 0.0f}, 1);
                SetCharPos(m_charB, Vec3{0.0f, -0.5f, 0.0f}, 0);
                SetCharPos(m_player, Vec3{-16.0f, 3.0f, 0.0f}, 0);
                break;
            case Scenario::BehindStatue:
                // 플레이어가 석상 뒤(더 위=뒤). 석상이 플레이어를 픽셀 가림.
                SetCharPos(m_charA, Vec3{-16.0f, 4.0f, 0.0f}, 1);
                SetCharPos(m_charB, Vec3{-16.0f, 3.0f, 0.0f}, 0);
                SetCharPos(m_player, Vec3{kStatuePos.x, kStatuePos.y + 1.5f, 0.0f}, 0);
                break;
            case Scenario::FrontStatue:
                // 플레이어가 석상 앞(더 아래=앞). 플레이어가 석상을 가림.
                SetCharPos(m_charA, Vec3{-16.0f, 4.0f, 0.0f}, 1);
                SetCharPos(m_charB, Vec3{-16.0f, 3.0f, 0.0f}, 0);
                SetCharPos(m_player, Vec3{kStatuePos.x, kStatuePos.y - 1.5f, 0.0f}, 0);
                break;
            case Scenario::Slope:
                // 플레이어(P)가 경사 중턱(경사 높이만큼 올라가 렌더). 램프 중앙(월드 Y=0)에서
                // SlopeGroundY(0)=1.0 → 평지 대비 1 unit(48px) 위로 렌더된다.
                // 비교 기준(B, magenta): 같은 화면 X에서 평지(월드 Y=0.5=카메라중심)에 둔다.
                // P의 발밑이 B보다 위(작은 스크린 Y)면 경사 상승이 검증된다.
                SetCharPos(m_charA, Vec3{-20.0f, 4.0f, 0.0f}, 1);
                SetCharPos(m_charB, Vec3{static_cast<float>(kSlopeCellX) + 2.5f, 0.5f, 0.0f}, 0);
                {
                    const float baseY = 0.0f;
                    SetCharPos(m_player, Vec3{static_cast<float>(kSlopeCellX) + 0.5f,
                                              SlopeGroundY(baseY), 0.0f}, 0);
                }
                break;
            case Scenario::Free:
            default:
                break;
        }
        // 시나리오는 카메라를 대상 근처로 고정(결정적 덤프).
        Vec2 camTarget{0.0f, 0.0f};
        if (sc == Scenario::BehindStatue || sc == Scenario::FrontStatue)
            camTarget = {kStatuePos.x, kStatuePos.y};
        else if (sc == Scenario::Slope)
            camTarget = {static_cast<float>(kSlopeCellX) + 0.5f, 0.5f};
        m_camera.SetPosition(camTarget);
        m_camFixed = (sc != Scenario::Free);

        // WorldTransform을 즉시 갱신(첫 프레임 덤프 정합).
        scene::UpdateWorldTransforms(*m_world);
    }

    void FixedUpdate(const mye::TimeStep& step) {
        m_prevPlayer = m_curPlayer;
        if (auto* lt = m_world->TryGet<scene::LocalTransform>(m_player))
            m_curPlayer = lt->position;

        if (m_control->scenario != Scenario::Free) return;   // 시나리오는 정적

        const float dt = static_cast<float>(step.deltaSeconds);
        Vec2 dir{0, 0};
        if (m_input) {
            if (m_input->IsDown(mye::KeyCode::W)) dir.y += 1.0f;
            if (m_input->IsDown(mye::KeyCode::S)) dir.y -= 1.0f;
            if (m_input->IsDown(mye::KeyCode::A)) dir.x -= 1.0f;
            if (m_input->IsDown(mye::KeyCode::D)) dir.x += 1.0f;
        }
        if (dir.x != 0.0f || dir.y != 0.0f) {
            dir = dir / dir.Length();
            m_curPlayer.x += dir.x * kMoveSpeed * dt;
            m_curPlayer.y += dir.y * kMoveSpeed * dt;
            if (auto* lt = m_world->TryGet<scene::LocalTransform>(m_player)) {
                lt->position = m_curPlayer; lt->dirty = true;
            }
        }
    }

    void Update(const mye::TimeStep& /*step*/) {
        if (m_input && m_input->WasPressed(mye::KeyCode::Escape))
            m_control->app->RequestExit(0);
    }

    void Render(const mye::TimeStep& /*step*/) {
        if (!m_device || !m_swapChain || !m_rt.IsInitialized()) return;

        // 카메라 추적(free 모드만).
        if (!m_camFixed) {
            const float alpha = static_cast<float>(m_ctx->Time().GetInterpolationAlpha());
            const Vec2 rp = Vec2::Lerp(Vec2{m_prevPlayer.x, m_prevPlayer.y},
                                       Vec2{m_curPlayer.x, m_curPlayer.y}, alpha);
            m_camera.SetPosition(rp);
        }

        // ECS: WorldTransform 갱신 후 RenderExtract 페이즈 실행.
        scene::UpdateWorldTransforms(*m_world);
        m_scheduler->RunPhase(scene::Phase::RenderExtract, mye::TimeStep{});

        m_device->BeginFrame();
        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        m_rt.BeginScenePass(cmd, kClearColor);
        m_hybrid.Render(m_proxies, m_camera, cmd);
        m_rt.EndScenePass(cmd);

        const Vec2i winSize = m_swapChain->GetSize();
        rhi::TextureHandle backbuffer = m_swapChain->GetCurrentBackBuffer();
        m_rt.Blit(cmd, backbuffer, winSize, m_camera.SubpixelResidual());

        const render::HybridRenderer::Stats stats = m_hybrid.LastStats();

        // 검증 덤프 시에는 오버레이를 그리지 않는다(픽셀 분석 오염 방지).
        const bool drawUi = m_debugUi.IsInitialized() && !m_control->dumpEnabled;
        if (drawUi) {
            m_debugUi.BeginFrame();
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("BridgeDemo Debug");
            ImGui::Text("FPS: %.1f", m_fps);
            ImGui::Text("Draw calls: %u", stats.drawCalls);
            ImGui::Text("Sprites: %u  TileQuads: %u  Meshes: %u",
                        stats.spriteCount, stats.tileQuadCount, stats.meshCount);
            ImGui::Separator();
            ShowCharInfo("A(bridge)", m_charA);
            ShowCharInfo("B(under)", m_charB);
            ShowCharInfo("P(player)", m_player);
            ImGui::End();

            rhi::RenderPassColorAttachment uiColor{};
            uiColor.texture = backbuffer;
            uiColor.loadOp = rhi::LoadOp::Load;
            rhi::RenderPassBeginDesc uiPass{};
            uiPass.colorAttachments = {&uiColor, 1};
            uiPass.debugName = "imgui.overlay";
            cmd.BeginRenderPass(uiPass);
            rhi::Viewport uiVp{};
            uiVp.x = 0; uiVp.y = 0;
            uiVp.width = static_cast<float>(winSize.x);
            uiVp.height = static_cast<float>(winSize.y);
            uiVp.minDepth = 0.0f; uiVp.maxDepth = 1.0f;
            cmd.SetViewport(uiVp);
            m_debugUi.EndFrame();
            cmd.EndRenderPass();
        }

        ++m_frameCount;
        HandleDumps(backbuffer);

        m_swapChain->Present(false);
        m_device->EndFrame();

        if (m_control->resizeTest) {
            if (m_frameCount == 30) DoResize(1920, 1080);
            if (m_frameCount == 60) DoResize(1024, 600);
        }

        m_secondsAccum += m_ctx->Time().CurrentStep().unscaledDeltaSeconds;
        if (m_secondsAccum >= 1.0) {
            m_fps = static_cast<float>(m_frameCount - m_lastFrameCount);
            m_lastFrameCount = m_frameCount;
            m_secondsAccum -= 1.0;
        }

        if (m_control->frameLimit && m_frameCount >= m_control->maxFrames)
            m_control->app->RequestExit(0);
    }

    void ShowCharInfo(const char* label, ecs::Entity e) {
        const auto* lt = m_world->TryGet<scene::LocalTransform>(e);
        const auto* fl = m_world->TryGet<scene::FloorLevel>(e);
        if (!lt || !fl) return;
        const uint16_t sl = render::FloorLevelToSortLayer(fl->level);
        const float sortKeyY = lt->position.y + tilemap::HeightLevelToWorldY(fl->level);
        const render::HybridViewInfo vi = render::HybridRenderer::MakeViewInfo(m_camera);
        const float depth = render::EncodeDepth(sl, sortKeyY, render::kOrderCharacter, vi.depth);
        ImGui::Text("%s floor=%d pos=(%.2f,%.2f) sortLayer=%u sortKeyY=%.3f depth=%.4f",
                    label, fl->level, lt->position.x, lt->position.y, sl, sortKeyY, depth);
    }

    void HandleDumps(rhi::TextureHandle backbuffer) {
        if (m_control->dumpEnabled && !m_dumped) {
            const bool last = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
            if (last || (!m_control->frameLimit && m_frameCount >= 5)) {
                auto cap = rhi::CaptureBackbuffer(*m_device, backbuffer, m_control->dumpPath);
                if (cap) MYE_LOG_INFO("BridgeDemo", "dump -> '{}' (frame {})",
                                      m_control->dumpPath, m_frameCount);
                else MYE_LOG_ERROR("BridgeDemo", "dump failed: {}", cap.GetError().message);
                m_dumped = true;
            }
        }
    }

    void DoResize(int w, int h) {
        HWND hwnd = static_cast<HWND>(m_ctx->MainWindow().GetNativeHandle());
        if (!hwnd) return;
        RECT rc{0, 0, w, h};
        ::AdjustWindowRectEx(&rc, static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE)),
                             FALSE, static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
        ::SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RunControl* m_control = nullptr;
    mye::EngineContext* m_ctx = nullptr;
    mye::EventBus* m_bus = nullptr;
    mye::InputState* m_input = nullptr;

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;

    std::vector<asset::AssetHandle<asset::Texture>> m_texHandles;
    asset::AssetHandle<asset::Mesh>                 m_meshHandle;
    AssetResolvers m_resolvers;

    asset::AssetGuid m_guidHeroA, m_guidHeroB, m_guidHeroP;
    asset::AssetGuid m_guidGround, m_guidBridge, m_guidSlope, m_guidWhite, m_guidStatue;

    std::unique_ptr<ecs::World>             m_world;
    std::unique_ptr<scene::SystemScheduler> m_scheduler;
    std::unique_ptr<tilemap::TilemapWorld>  m_tilemap;
    std::unique_ptr<tilemap::TilemapWorld>  m_bridgeTilemap;
    std::unique_ptr<phys::PhysicsWorld2D>   m_physics;
    scene::RenderProxyList m_proxies;

    ecs::Entity m_groundTilemapEntity, m_bridgeTilemapEntity, m_statueEntity;
    ecs::Entity m_charA, m_charB, m_player;

    render::PixelPerfectTarget m_rt;
    render::HybridRenderer     m_hybrid;
    render::Camera2D           m_camera;
    mye::imgui::DebugUi        m_debugUi;
    mye::ScopedSubscription    m_onResized;

    Vec3 m_curPlayer{-2.0f, 3.0f, 0.0f};
    Vec3 m_prevPlayer{-2.0f, 3.0f, 0.0f};
    bool m_camFixed = false;

    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
    double   m_secondsAccum = 0.0;
    uint64_t m_lastFrameCount = 0;
    float    m_fps = 0.0f;
};

class BridgeDemoApp final : public mye::Application {
public:
    explicit BridgeDemoApp(const mye::LaunchArgs& args) {
        m_control.app = this;
        const auto& a = args.args;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_control.frameLimit = true;
                m_control.maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_control.dumpEnabled = true;
                m_control.dumpPath = a[++i];
            } else if (a[i] == "--scenario" && i + 1 < a.size()) {
                m_control.scenario = ParseScenario(a[++i]);
                m_control.scenarioSet = true;
            } else if (a[i] == "--resize-test") {
                m_control.resizeTest = true;
            }
        }
    }

    void OnConfigure(mye::ConfigSystem& /*config*/) override {}
    void OnRegisterModules(mye::ModuleRegistry& modules) override {
        modules.Register(std::make_unique<BridgeDemoModule>(&m_control));
    }
    void OnStart(mye::EngineContext& /*ctx*/) override {
        MYE_LOG_INFO("BridgeDemo", "bridge_demo start (frames={} scenario={})",
                     m_control.frameLimit ? static_cast<long long>(m_control.maxFrames) : -1LL,
                     static_cast<int>(m_control.scenario));
    }

private:
    RunControl m_control;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) { return new BridgeDemoApp(args); }
} // namespace mye

int main(int /*argc*/, char** /*argv*/) {
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    }
    launch.mainWindow.title = "MyEngine — bridge_demo (M2)";
    launch.mainWindow.clientSize = {960, 540};
    launch.mainWindow.resizable = true;
    return mye::GuardedMain(launch);
}
