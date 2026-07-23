// village_demo — M6-B 로드맵 최종 수직 슬라이스 데모 (docs/00 §7 M6 완료 기준)
//
// 테일즈위버풍 마을: 도트 캐릭터가 하이브리드 맵(지면·길·다리·개울·경사 언덕·3D 프랍)을 걸어다니고
//   NPC(촌장·상인·경비병)와 한글로 대화하는 수직 슬라이스. 실행 파일 하나로 처음부터 끝까지 시연.
//   맵·대사·게임 로직 수정이 엔진 재빌드 없이(Lua 핫리로드) 이뤄진다.
//
// 관통 계층(자립형 배선 — bridge_demo/character_demo 패턴 확장):
//   GuardedMain · Application · RHI(DX11) · InputState · AssetManager(PNG/GLB/WAV/.lua) ·
//   ECS World · SystemScheduler(anim·RenderExtract) · PhysicsWorld2D · TilemapWorld(지면+다리) ·
//   AudioEngine(BGM·발소리) · ScriptRuntime/ScriptSystem(플레이어·NPC Lua + 핫리로드) ·
//   runtime(Localization·DialogueSystem·CutsceneRuntime·MoveController·NpcSystem) ·
//   text(FontRegistry·GlyphAtlas·TextRenderer — 한글 대화창 오버레이) ·
//   RenderExtract → HybridRenderer(단일 뎁스버퍼 2D+3D) · PixelPerfectTarget(960×540 정수배) · DebugUi.
//
// 검증 CLI:
//   --frames N          N 렌더 프레임 후 exit 0
//   --dump path.bmp     마지막 프레임 백버퍼 BMP 저장(오버레이 제외 — 픽셀 분석 오염 방지)
//   --dump-ui path.bmp  마지막 프레임 백버퍼 BMP 저장(대화창 오버레이 포함 — 한글 글리프 검증용)
//   --headless          장치 초기화 실패해도 관대(오디오 무음). GPU 없으면 렌더 스킵.
//   --scenario <name>   결정적 시나리오. name ∈
//       { free, walk, walk_up, bridge, under_bridge, talk, talk_merchant, talk_guard,
//         wander, behind_prop }
//     walk 계열: 강제 이동으로 위치 변화(두 프레임 비교). bridge: 다리 위/아래 하이브리드 정렬.
//     talk: 촌장과 상호작용 → 한글 대화창. wander: NPC 배회. 결과는 "M6-VERIFY:" 로그로 요약.
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/core/platform/Win32Window.h"

#include "mye/rhi/Rhi.h"

#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AudioClip.h"
#include "mye/asset/AudioImporter.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/asset/Mesh.h"
#include "mye/asset/MeshImporter.h"
#include "mye/asset/SpriteImporter.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/asset/Texture.h"

#include "mye/ecs/World.h"
#include "mye/ecs/View.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/tilemap/Tilemap.h"
#include "mye/phys/PhysicsWorld2D.h"

#include "mye/anim/AnimationSystem.h"
#include "mye/anim/AnimationTypes.h"
#include "mye/anim/SpriteAnimator.h"

#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioTypes.h"

#include "mye/script/ScriptClass.h"
#include "mye/script/ScriptComponent.h"
#include "mye/script/ScriptImporter.h"
#include "mye/script/CoroutineScheduler.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/ScriptSystem.h"
#include "mye/script/ScriptTypes.h"
#include "mye/script/bindings/EngineBindings.h"

#include "mye/runtime/Localization.h"
#include "mye/runtime/DialogueData.h"
#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/NpcSystem.h"
#include "mye/runtime/NpcBindings.h"
#include "mye/runtime/RuntimeBindings.h"
#include "mye/runtime/AudioListener.h"

#include "mye/text/FontFace.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/TextRenderer.h"
#include "mye/text/TextTypes.h"

#include "mye/render/Camera2D.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/SpriteBatch.h"
#include "mye/render/DepthEncoder.h"

#include "mye/imgui/DebugUi.h"
#include "imgui.h"

#include <sol/sol.hpp>

#include <Windows.h>
#include <shellapi.h>

// windows.h 의 DrawText 매크로(→DrawTextA/W)가 text::TextRenderer::DrawText 를 오염시킨다. 해제.
#ifdef DrawText
#undef DrawText
#endif

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace rhi     = mye::rhi;
namespace asset   = mye::asset;
namespace render  = mye::render;
namespace ecs     = mye::ecs;
namespace scene   = mye::scene;
namespace tilemap = mye::tilemap;
namespace phys    = mye::phys;
namespace anim    = mye::anim;
namespace audio   = mye::audio;
namespace script  = mye::script;
namespace runtime = mye::runtime;
namespace text    = mye::text;

using mye::Vec2;
using mye::Vec2i;
using mye::Vec3;
using mye::Color;
using mye::Mat4;
using anim::Dir8;

constexpr Color kClearColor{0.06f, 0.10f, 0.07f, 1.0f};   // 어두운 풀빛(마을 지면 톤)
constexpr int   kCellW = 24, kCellH = 32, kCols = 4, kRows = 8;

// ---------------------------------------------------------------------------
// 좌표 규약(docs/00·village.json): 1 unit = 1 cell = PPU48, +Y 위, 왼손.
//   HybridRenderer: 셀(cx,cy) → 월드(x=cx, yBottom=-cy). 월드 Y=W 원하면 cellY=-W.
// ---------------------------------------------------------------------------
constexpr int kMinCX = -14, kMaxCX = 14, kMinCY = -8, kMaxCY = 8;

// 다리: 개울(worldX 5..6)을 동서로 가로지르는 상판(worldX 4..7, worldY 0 → cellY 0), floor1.
constexpr int kBridgeCX0 = 4, kBridgeCX1 = 7, kBridgeCY = 0;

// 경사 언덕(서쪽): 셀X -13..-11, 월드 Y -2(하단)~+2(상단). bridge_demo SlopeGroundY 산식.
constexpr int   kSlopeCX0 = -13, kSlopeCX1 = -11;
constexpr int   kSlopeCellYTop = -2, kSlopeCellYBottom = 2;
constexpr float kSlopeMaxRise = 2.0f;
inline float SlopeGroundY(float worldY) {
    const float lo = -2.0f, hi = 2.0f;
    float t = (worldY - lo) / (hi - lo);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return worldY + kSlopeMaxRise * t;
}

// 3D 프랍(플라자).
constexpr Vec3 kStatuePos{-7.0f, 5.0f, 0.0f};
constexpr Vec3 kFountainPos{-8.0f, 3.5f, 0.0f};

// ---------------------------------------------------------------------------
// 시나리오
// ---------------------------------------------------------------------------
enum class Scenario {
    Free, Walk, WalkUp, Bridge, UnderBridge, Talk, TalkMerchant, TalkGuard, Wander, BehindProp,
};

Scenario ParseScenario(const std::string& s) {
    if (s == "walk")         return Scenario::Walk;
    if (s == "walk_up")      return Scenario::WalkUp;
    if (s == "bridge")       return Scenario::Bridge;
    if (s == "under_bridge") return Scenario::UnderBridge;
    if (s == "talk")         return Scenario::Talk;
    if (s == "talk_merchant")return Scenario::TalkMerchant;
    if (s == "talk_guard")   return Scenario::TalkGuard;
    if (s == "wander")       return Scenario::Wander;
    if (s == "behind_prop")  return Scenario::BehindProp;
    return Scenario::Free;
}

struct RunControl {
    mye::Application* app = nullptr;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    bool     dumpWithUi = false;      // --dump-ui: 대화창 오버레이 포함
    std::string dumpPath;
    bool     headless = false;
    Scenario scenario = Scenario::Free;
    bool     scenarioSet = false;
};

// GUID → 로드된 에셋 포인터 해석기(렌더러 콜백).
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

// NPC 스폰 서술(맵 데이터에서 파생 — village.json spawns.npcs 반영).
struct NpcSpawn {
    const char* id;
    const char* sheet;
    const char* script;
    Vec3        pos;
    Dir8        facing;
    const char* nameKey;
    std::vector<Vec2> waypoints;   // 배회 웨이포인트(비면 제자리)
    runtime::WanderMode mode;
};

// ===========================================================================
// VillageDemoModule
// ===========================================================================
class VillageDemoModule final : public mye::IModule {
public:
    explicit VillageDemoModule(RunControl* control) : m_control(control) {}
    const char* GetName() const override { return "VillageDemo"; }

    void OnInitialize(mye::EngineContext& ctx) override {
        m_ctx = &ctx;
        m_bus = &ctx.Events();
        m_input = ctx.GetService<mye::InputState>();
        mye::IWindow& window = ctx.MainWindow();

        auto deviceResult = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!deviceResult) {
            if (m_control->headless) { m_gpuOk = false; MYE_LOG_WARN("VillageDemo", "no GPU device (headless)"); return; }
            Fatal("CreateDevice failed", -10); return;
        }
        m_device = std::move(deviceResult).Value();

        rhi::SwapChainDesc scDesc{};
        scDesc.width = 0; scDesc.height = 0;
        scDesc.format = rhi::Format::BGRA8UnormSrgb;
        m_swapChain = m_device->CreateSwapChain(window.GetNativeHandle(), scDesc);
        if (!m_swapChain) { Fatal("CreateSwapChain failed", -11); return; }
        m_gpuOk = true;

        // VFS + AssetManager (assets:// → samples/village_demo/assets)
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        m_assetsDir = ResolveAssetsDir();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(m_assetsDir), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::MeshImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::AudioImporter>());
        m_assets->RegisterImporter(std::make_unique<script::ScriptImporter>());

        if (!LoadAssets()) return;

        // 오디오(오프라인 무음 믹서 — 헤드리스·CI 안전). BGM·발소리는 믹서 경로로 검증.
        m_audio = std::make_unique<audio::AudioEngine>();
        (void)m_audio->Initialize(nullptr, 44100);
        BuildAudioCue();

        // 픽셀퍼펙트 RT + 하이브리드 렌더러
        render::PixelPerfectDesc ppd{};
        ppd.useDepth = true;
        ppd.backbufferFormat = m_swapChain->GetFormat();
        m_rt.Init(*m_device, ppd);
        if (!m_rt.IsInitialized()) { Fatal("PixelPerfectTarget init failed", -13); return; }

        render::HybridRendererDesc hrd{};
        hrd.colorFormat = m_rt.ColorFormat();
        hrd.depthFormat = rhi::Format::D24UnormS8Uint;
        hrd.alphaCutoff = 0.5f;
        m_hybrid.Init(*m_device, hrd);
        if (!m_hybrid.IsInitialized()) { Fatal("HybridRenderer init failed", -14); return; }
        m_hybrid.SetTextureResolver(&AssetResolvers::ResolveTexture, &m_resolvers);
        m_hybrid.SetMeshResolver(&AssetResolvers::ResolveMesh, &m_resolvers);
        m_hybrid.SetDirectionalLight(Vec3{-0.3f, -0.6f, 0.5f}, Color::White(), 0.45f);

        render::Camera2DDesc camDesc{};
        camDesc.position = {0.0f, -3.0f};   // village.json camera.startWorldPos
        camDesc.pixelSnap = true;
        m_camera = render::Camera2D(camDesc);

        InitTextStack();          // 한글 대화창 오버레이(text 스택)
        BuildWorld();
        BuildRuntimeAndScripts();
        ApplyScenario();

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

        MYE_LOG_INFO("VillageDemo", "initialized (assets='{}', scenario={})",
                     m_assetsDir, static_cast<int>(m_control->scenario));
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
        // 셧다운 순서(데드락 규약): 스크립트/NPC 먼저(오디오 큐·sol 참조 해제), 그다음 World(컴포넌트
        //   sol::table 보유 — sol::state 보다 먼저 파괴), 그다음 런타임 서브시스템, 오디오, 렌더, 에셋.
        m_onResized.Reset();
        m_debugUi.Shutdown();

        // 1) 스크립트/NPC (오디오 큐·sol 참조 해제).
        m_scriptSystem.reset();
        m_npcBindings.reset();
        m_npc.reset();
        // 2) World (ScriptComponent sol::table 보유 → sol::state 보다 먼저 파괴).
        m_world.reset();
        m_runtime.reset();
        m_npcBindings.reset();       // (재확인) 비소유 바인딩 정리.
        m_runtimeBindings.reset();
        m_ecsBinding.reset();        // CommandBuffer(World&) 보유 → World 이후 즉시 파괴.
        // 3) 런타임 서브시스템.
        if (m_cutscene) { m_cutscene->Shutdown(); m_cutscene.reset(); }
        if (m_dialogue) { m_dialogue->Shutdown(); m_dialogue.reset(); }
        m_move.reset();
        m_camFocus.reset();
        m_audioListener.reset();
        if (m_loc) { m_loc->Shutdown(); m_loc.reset(); }
        // 4) 오디오.
        if (m_audio) { m_audio->Shutdown(); m_audio.reset(); }
        // 5) GPU 소비자(전부 device 살아있을 때 파괴). text 스택 → hybrid → RT.
        m_spriteBatch.Shutdown();
        m_atlas.Shutdown();
        m_fonts.reset();
        m_koFont = nullptr;
        m_hybrid.Shutdown();
        m_rt.Shutdown();
        // 6) 씬 스케줄러·타일맵·물리(World 이후 안전).
        m_scheduler.reset();
        m_tilemapGround.reset(); m_tilemapPath.reset(); m_tilemapWater.reset();
        m_tilemapPlaza.reset(); m_tilemapSlope.reset(); m_tilemapBridge.reset();
        m_physics.reset();
        // 7) 에셋 핸들(device 로 GPU 리소스 해제 — device 이전) → AssetManager → device.
        m_texHandles.clear();
        m_meshHandles.clear();
        m_audioClipHandles.clear();
        m_footstepClip = {}; m_bgmClip = {};
        m_scriptHandles.clear();
        m_assets.reset();
        m_vfs.reset();
        m_swapChain.reset();
        m_device.reset();
        MYE_LOG_INFO("VillageDemo", "shutdown ({} frames)", m_frameCount);
    }

private:
    void Fatal(const char* msg, int code) {
        MYE_LOG_FATAL("VillageDemo", "{}", msg);
        m_control->app->RequestExit(code);
    }

    std::string ResolveAssetsDir() const {
        namespace fs = std::filesystem;
        auto has = [](const fs::path& dir) {
            std::error_code ec;
            return fs::exists(dir / "maps" / "village.json", ec) ||
                   fs::exists(dir / "player_sheet.png", ec);
        };
        wchar_t exePathW[MAX_PATH] = {};
        if (::GetModuleFileNameW(nullptr, exePathW, MAX_PATH) > 0) {
            fs::path dir = fs::path(exePathW).parent_path();
            for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
                const fs::path cand = dir / "samples" / "village_demo" / "assets";
                if (has(cand)) return cand.string();
                dir = dir.parent_path();
            }
        }
        const char* candidates[] = {
            "samples/village_demo/assets", "../samples/village_demo/assets",
            "../../samples/village_demo/assets", "../../../samples/village_demo/assets",
        };
        for (const char* c : candidates) if (has(fs::path(c))) return c;
        return "samples/village_demo/assets";
    }

    asset::AssetGuid LoadTex(const char* vpath) {
        auto h = m_assets->LoadSync<asset::Texture>(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) {
            MYE_LOG_ERROR("VillageDemo", "texture load failed: {}", vpath);
            return {};
        }
        asset::AssetGuid g = h.Guid();
        m_resolvers.textures[AssetResolvers::Key(g)] = h.Get();
        Vec2i sz{ static_cast<int>(h.Get()->width), static_cast<int>(h.Get()->height) };
        m_texSizes[AssetResolvers::Key(g)] = sz;
        m_texHandles.push_back(std::move(h));
        return g;
    }

    asset::AssetGuid LoadMesh(const char* vpath) {
        auto h = m_assets->LoadSync<asset::Mesh>(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) {
            MYE_LOG_ERROR("VillageDemo", "mesh load failed: {}", vpath);
            return {};
        }
        asset::AssetGuid g = h.Guid();
        m_resolvers.meshes[AssetResolvers::Key(g)] = h.Get();
        m_meshHandles.push_back(std::move(h));
        return g;
    }

    asset::AssetHandle<script::ScriptAsset> LoadScript(const char* vpath) {
        auto h = m_assets->LoadSync<script::ScriptAsset>(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get())
            MYE_LOG_ERROR("VillageDemo", "script load failed: {}", vpath);
        return h;
    }

    bool LoadAssets() {
        m_guidGrass  = LoadTex("assets://tile_grass.png");
        m_guidPath   = LoadTex("assets://tile_path.png");
        m_guidWater  = LoadTex("assets://tile_water.png");
        m_guidBridge = LoadTex("assets://tile_bridge.png");
        m_guidSlope  = LoadTex("assets://tile_slope.png");
        m_guidPlaza  = LoadTex("assets://tile_plaza.png");
        m_guidWhite  = LoadTex("assets://white.png");

        m_guidPlayerSheet   = LoadTex("assets://player_sheet.png");
        m_guidChiefSheet    = LoadTex("assets://npc_chief.png");
        m_guidMerchantSheet = LoadTex("assets://npc_merchant.png");
        m_guidGuardSheet    = LoadTex("assets://npc_guard.png");

        m_guidStatue   = LoadMesh("assets://mesh/statue.glb");
        m_guidFountain = LoadMesh("assets://mesh/fountain.glb");

        // 오디오 클립(발소리·BGM).
        m_footstepClip = m_assets->LoadSync<asset::AudioClip>("assets://audio/footstep.wav");
        if (m_footstepClip.State() == asset::AssetState::Loaded)
            m_audioClipHandles.push_back(m_footstepClip);
        m_bgmClip = m_assets->LoadSync<asset::AudioClip>("assets://audio/village_bgm.wav");
        if (m_bgmClip.State() == asset::AssetState::Loaded)
            m_audioClipHandles.push_back(m_bgmClip);

        if (!m_guidGrass.IsValid() || !m_guidPlayerSheet.IsValid()) {
            Fatal("essential assets failed to load", -12);
            return false;
        }
        return true;
    }

    void BuildAudioCue() {
        m_footstepCue.clips.clear();
        if (m_footstepClip.Get()) m_footstepCue.clips.push_back(m_footstepClip.Get());
        m_footstepCue.selection = audio::CueSelect::Random;
        m_footstepCue.bus = audio::BusId::SFX;
        m_footstepCue.volumeMin = 0.9f; m_footstepCue.volumeMax = 1.0f;
        m_footstepCue.pitchMin = 0.95f; m_footstepCue.pitchMax = 1.05f;
        m_footstepCue.maxVoices = 8;
        // BGM 크로스페이드 재생(루핑).
        if (m_bgmClip.Get()) m_audio->Music().Play(*m_bgmClip.Get(), 1.0f, 0.6f);
    }

    // ---- 한글 대화창 오버레이용 text 스택(FontRegistry·GlyphAtlas·TextRenderer) ----
    void InitTextStack() {
        m_fonts = std::make_unique<text::FontRegistry>();
        // 시스템 맑은 고딕(한글) 로드 — 실패해도 데모는 계속(대화창 텍스트만 비활성).
        const wchar_t* kFontPath = L"C:/Windows/Fonts/malgun.ttf";
        std::ifstream f(kFontPath, std::ios::binary | std::ios::ate);
        if (f) {
            std::streamsize n = f.tellg();
            f.seekg(0);
            m_fontBlob.resize(static_cast<size_t>(n));
            f.read(reinterpret_cast<char*>(m_fontBlob.data()), n);
            auto reg = m_fonts->RegisterTtf(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(m_fontBlob.data()),
                                           m_fontBlob.size()), "malgun");
            if (reg) { m_koFontId = reg.Value(); m_koFont = m_fonts->ResolveFont(m_koFontId); }
        }
        if (!m_koFont) MYE_LOG_WARN("VillageDemo", "malgun.ttf load failed — dialogue text disabled");

        text::GlyphAtlasConfig acfg{};
        m_atlas.Init(*m_device, acfg);
        m_spriteBatch.Init(*m_device, m_rt.ColorFormat(), true);
    }

    // ---- 타일 헬퍼 ----
    void SetTile(tilemap::TilemapWorld& map, int layer, int cx, int cy) {
        tilemap::TileColumn c{};
        c.tile = 1; c.baseHeightLevel = 0; c.layer = static_cast<uint8_t>(layer);
        map.GetOrCreateLayer(layer).SetTile({cx, cy}, c);
    }
    void SetSlopeTile(tilemap::TilemapWorld& map, int cx, int cy) {
        tilemap::TileColumn c{};
        c.tile = 1; c.baseHeightLevel = 0; c.slope = tilemap::SlopeType::GentleN; c.layer = 0;
        map.GetOrCreateLayer(0).SetTile({cx, cy}, c);
    }

    // 지면 역할별 타일셋을 세분화하려면 여러 TilemapRenderer 엔티티가 필요하지만(TilemapRenderer 는
    //   한 텍스처), 데모는 역할 텍스처를 색으로 구분해 배경을 채운다. 여기서는 지면 텍스처(grass)를
    //   기본으로 전체를 덮되, 길/물/플라자/경사는 각 역할 텍스처의 별도 TilemapWorld 로 오버레이한다.
    void BuildTilemapRole(std::unique_ptr<tilemap::TilemapWorld>& map, asset::AssetGuid tex,
                          const std::vector<Vec2i>& cells, uint16_t sortLayer, bool bridge) {
        map = std::make_unique<tilemap::TilemapWorld>();
        for (const Vec2i& c : cells) {
            if (bridge) {
                tilemap::TileColumn col{};
                col.tile = 1; col.baseHeightLevel = 0;
                col.flags = tilemap::ColumnFlags::OneWayBridge | tilemap::ColumnFlags::Bridge;
                col.layer = 0;
                map->GetOrCreateLayer(0).AddBridge({c.x, c.y}, col);
            } else {
                SetTile(*map, 0, c.x, c.y);
            }
        }
        ecs::Entity e = m_world->Create();
        m_world->Add<scene::LocalTransform>(e);
        m_world->Add<scene::WorldTransform>(e);
        auto& tr = m_world->Add<scene::TilemapRenderer>(e);
        tr.runtime = map.get();
        tr.map.guid = tex;
        tr.sort.sortLayer = sortLayer;
        tr.sort.orderInLayer = render::kOrderTileGround;
    }

    // 역할 셀 목록(village.json ground rects 반영).
    static bool InRect(int cx, int cy, int x0, int x1, int y0, int y1) {
        return cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1;
    }

    void BuildWorld() {
        m_world = std::make_unique<ecs::World>();
        m_world->SetEventBus(m_bus);
        m_scheduler = std::make_unique<scene::SystemScheduler>(*m_world, m_bus);
        m_physics = std::make_unique<phys::PhysicsWorld2D>();

        BuildAnimData();

        // ---- 지면 역할 셀 분류(village.json ground) ----
        std::vector<Vec2i> grass, path, water, plaza, slope;
        for (int cy = kMinCY; cy <= kMaxCY; ++cy)
            for (int cx = kMinCX; cx <= kMaxCX; ++cx) {
                // 경사 언덕(서쪽): 별도 slope 맵.
                if (InRect(cx, cy, kSlopeCX0, kSlopeCX1, kSlopeCellYTop, kSlopeCellYBottom)) {
                    slope.push_back({cx, cy}); continue;
                }
                // 개울(worldX 5..6 == cx 5..6) — 전 cellY.
                if (InRect(cx, cy, 5, 6, kMinCY, kMaxCY)) { water.push_back({cx, cy}); continue; }
                // 플라자(cx -9..-6, cellY -6..-3 == worldY 3..6).
                if (InRect(cx, cy, -9, -6, -6, -3)) { plaza.push_back({cx, cy}); continue; }
                // 남북 대로(cx -1..1) / 동서 대로(cellY -1..1).
                if (InRect(cx, cy, -1, 1, kMinCY, kMaxCY) || InRect(cx, cy, -10, 10, -1, 1)) {
                    path.push_back({cx, cy}); continue;
                }
                grass.push_back({cx, cy});
            }

        // ---- 지면 TilemapWorld(역할별, 전부 Ground 밴드) ----
        BuildTilemapRole(m_tilemapGround, m_guidGrass, grass, render::kSortLayerGround, false);
        BuildTilemapRole(m_tilemapPath,   m_guidPath,  path,  render::kSortLayerGround, false);
        BuildTilemapRole(m_tilemapWater,  m_guidWater, water, render::kSortLayerGround, false);
        BuildTilemapRole(m_tilemapPlaza,  m_guidPlaza, plaza, render::kSortLayerGround, false);
        // 경사: 지면 밴드지만 slope 컬럼(깊이 램프). 별도 맵에 slope 타일.
        {
            m_tilemapSlope = std::make_unique<tilemap::TilemapWorld>();
            for (const Vec2i& c : slope) SetSlopeTile(*m_tilemapSlope, c.x, c.y);
            ecs::Entity e = m_world->Create();
            m_world->Add<scene::LocalTransform>(e);
            m_world->Add<scene::WorldTransform>(e);
            auto& tr = m_world->Add<scene::TilemapRenderer>(e);
            tr.runtime = m_tilemapSlope.get();
            tr.map.guid = m_guidSlope;
            tr.sort.sortLayer = render::kSortLayerGround;
            tr.sort.orderInLayer = render::kOrderTileGround;
        }

        // ---- 다리 상판(floor1, World1 밴드) — bridge_demo 2-TilemapWorld 패턴 ----
        {
            std::vector<Vec2i> bridge;
            for (int cx = kBridgeCX0; cx <= kBridgeCX1; ++cx) bridge.push_back({cx, kBridgeCY});
            BuildTilemapRole(m_tilemapBridge, m_guidBridge, bridge,
                             render::FloorLevelToSortLayer(1), true);
        }

        // ---- 3D 프랍(석상·분수) : AnchorBiased, floor0 ----
        MakeProp(m_guidStatue,   kStatuePos,   Vec3{1.0f, 1.0f, 0.5f});
        MakeProp(m_guidFountain, kFountainPos, Vec3{1.0f, 1.0f, 0.6f});

        // ---- 플레이어 캐릭터 ----
        m_player = MakeCharacter(m_guidPlayerSheet, Vec3{0.0f, -3.0f, 0.0f}, 0, Dir8::Down);

        // ---- NPC 스폰(village.json spawns.npcs) ----
        m_npcSpawns = {
            { "chief", "assets://scripts/npc_chief.lua", nullptr, kChiefPos, Dir8::Down,
              "npc.chief.name", { Vec2{-6.5f, 4.0f}, Vec2{-8.0f, 4.5f}, Vec2{-6.0f, 5.5f} },
              runtime::WanderMode::Patrol },
            { "merchant", "assets://scripts/npc_merchant.lua", nullptr, kMerchantPos, Dir8::Left,
              "npc.merchant.name", {}, runtime::WanderMode::None },
            { "guard", "assets://scripts/npc_guard.lua", nullptr, kGuardPos, Dir8::Left,
              "npc.guard.name", { Vec2{8.5f, 0.0f}, Vec2{8.5f, 2.0f} }, runtime::WanderMode::Patrol },
        };
        m_npcSpawns[0].sheet = nullptr; // (sheet 는 별도 guid — 아래 배치)
        m_chief    = MakeCharacter(m_guidChiefSheet,    kChiefPos,    0, Dir8::Down);
        m_merchant = MakeCharacter(m_guidMerchantSheet, kMerchantPos, 0, Dir8::Left);
        m_guard    = MakeCharacter(m_guidGuardSheet,    kGuardPos,    0, Dir8::Left);

        anim::RegisterAnimationSystem(*m_scheduler);
        scene::RegisterRenderExtractSystem(*m_scheduler, m_proxies);
    }

    void MakeProp(asset::AssetGuid mesh, Vec3 pos, Vec3 scale) {
        if (!mesh.IsValid()) return;
        ecs::Entity e = m_world->Create();
        auto& lt = m_world->Add<scene::LocalTransform>(e);
        lt.position = pos; lt.scale = scale;
        m_world->Add<scene::WorldTransform>(e);
        auto& mr = m_world->Add<scene::MeshRenderer>(e);
        mr.mesh.guid = mesh;
        mr.material.guid = m_guidWhite;
        mr.depthMode = static_cast<uint8_t>(render::DepthMode::AnchorBiased);
        mr.sort.orderInLayer = 0;
        m_world->Add<scene::FloorLevel>(e).level = 0;
    }

    // 동일 레이아웃(96×256, 4×8, 24×32, 피벗 12,32)으로 각 캐릭터의 SpriteSheet 를 만든다.
    //   AnimationSystem 이 renderer->sprite = sheet->texture 로 덮으므로, NPC 마다 자기 텍스처를
    //   가진 별도 SpriteSheet 가 필요하다(공유 시 전원 플레이어 시트로 렌더됨).
    asset::SpriteSheet MakeSheet(asset::AssetGuid tex) {
        asset::SpriteSheetImportSettings s;
        s.frameSize = {kCellW, kCellH};
        Vec2i sheetSize{kCols * kCellW, kRows * kCellH};
        asset::SpriteSheet sh;
        sh.frames = asset::SpriteSheetImporter::SliceGrid(sheetSize, s);
        sh.texture.guid = tex;
        for (auto& fr : sh.frames) { fr.pivot = {12.0f, 32.0f}; fr.pivotInPixels = true; }
        return sh;
    }

    // 8방향 walk/idle 클립 + 상태머신(character_demo BuildAnimData 그대로).
    void BuildAnimData() {
        m_walkClips.resize(kRows);
        m_idleClips.resize(kRows);
        for (int dir = 0; dir < kRows; ++dir) {
            const uint32_t base = static_cast<uint32_t>(dir * kCols);
            asset::AnimationClipData& wc = m_walkClips[static_cast<size_t>(dir)];
            wc.name = std::string("walk_") + anim::Dir8Suffix(static_cast<Dir8>(dir));
            wc.loop = true;
            wc.direction = asset::AnimationClipData::Direction::Forward;
            for (int c = 0; c < kCols; ++c) {
                wc.frameIndices.push_back(base + static_cast<uint32_t>(c));
                wc.frameDurations.push_back(0.12f);
            }
            wc.events.push_back(asset::AnimEventMarker{1, "footstep", "cue_footstep", 0.0f});
            wc.events.push_back(asset::AnimEventMarker{3, "footstep", "cue_footstep", 0.0f});

            asset::AnimationClipData& ic = m_idleClips[static_cast<size_t>(dir)];
            ic.name = std::string("idle_") + anim::Dir8Suffix(static_cast<Dir8>(dir));
            ic.loop = true;
            ic.frameIndices.push_back(base);
            ic.frameDurations.push_back(0.2f);
        }
        m_machine.states.clear();
        m_machine.transitions.clear();
        {
            anim::AnimState st; st.name = "idle"; st.directional = true;
            for (int d = 0; d < kRows; ++d)
                st.dirSet.clips[static_cast<size_t>(d)] = &m_idleClips[static_cast<size_t>(d)];
            m_machine.states.push_back(st);
        }
        {
            anim::AnimState st; st.name = "walk"; st.directional = true;
            for (int d = 0; d < kRows; ++d)
                st.dirSet.clips[static_cast<size_t>(d)] = &m_walkClips[static_cast<size_t>(d)];
            m_machine.states.push_back(st);
        }
        m_machine.initialState = 0;
        { anim::AnimTransition t; t.from = 0; t.to = 1;
          t.conditions.push_back({"isMoving", anim::CmpOp::IsTrue, 0}); m_machine.transitions.push_back(t); }
        { anim::AnimTransition t; t.from = 1; t.to = 0;
          t.conditions.push_back({"isMoving", anim::CmpOp::IsFalse, 0}); m_machine.transitions.push_back(t); }
    }

    ecs::Entity MakeCharacter(asset::AssetGuid sheetTex, Vec3 pos, int8_t floor, Dir8 facing) {
        // 각 캐릭터 전용 SpriteSheet(수명: 모듈). 애니 시스템이 sprite 를 이 시트 텍스처로 덮는다.
        m_sheets.push_back(std::make_unique<asset::SpriteSheet>(MakeSheet(sheetTex)));
        asset::SpriteSheet* sheet = m_sheets.back().get();

        ecs::Entity e = m_world->Create();
        auto& lt = m_world->Add<scene::LocalTransform>(e);
        lt.position = pos;
        m_world->Add<scene::WorldTransform>(e);
        auto& sr = m_world->Add<scene::SpriteRenderer>(e);
        sr.sprite.guid = sheetTex;
        sr.pivotPx = {12.0f, 32.0f};
        sr.sort.sortLayer = render::kSortLayerGround;   // 밴드는 FloorLevel 로 정해짐
        sr.sort.orderInLayer = render::kOrderCharacter;
        auto& an = m_world->Add<anim::SpriteAnimator>(e);
        an.machine = &m_machine;
        an.sheet = sheet;
        an.facing = facing;
        an.SetBool("isMoving", false);
        m_world->Add<scene::FloorLevel>(e).level = floor;
        return e;
    }

    void BuildRuntimeAndScripts() {
        // ---- 런타임 서브시스템(Localization·Dialogue·Cutscene·Move·Camera·Npc) ----
        m_loc = std::make_unique<runtime::LocalizationSystem>();
        m_loc->Initialize(m_bus);
        (void)m_loc->LoadTableFromFile(runtime::Locale::Korean, *m_vfs, "assets://loc/ko.json");
        (void)m_loc->LoadTableFromFile(runtime::Locale::English, *m_vfs, "assets://loc/en.json");
        m_loc->SetLocale(runtime::Locale::Korean);

        m_dialogue = std::make_unique<runtime::DialogueSystem>();
        // 헤드리스 대화(box=nullptr): 상태기계로 진행, 표시는 우리 text 오버레이가 담당.
        m_dialogue->Initialize(nullptr, m_loc.get(), m_audio.get(), m_bus);

        m_move = std::make_unique<runtime::MoveController>();
        m_move->Initialize(m_world.get(), nullptr);   // nav 없음 → 직선 이동

        m_camFocus = std::make_unique<runtime::CameraFocusController>();
        // 컷신 카메라 포커스는 컷신(인트로)이 활성일 때만 실제 카메라에 적용한다. 그 외에는
        //   데모가 카메라를 직접 제어(플레이어 추적/시나리오 고정)한다 — 포커스 컨트롤러가 매
        //   프레임 초기 포커스로 덮어써 시나리오 카메라를 무효화하는 문제 방지.
        m_camFocus->Initialize([this](Vec2 p) {
            if (m_allowCameraFocus) { m_camera.SetPosition(p); m_camFocusActive = true; }
        }, Vec2{0.0f, -3.0f});

        m_cutscene = std::make_unique<runtime::CutsceneRuntime>();
        m_cutscene->Initialize(m_dialogue.get(), m_move.get(), m_camFocus.get());

        m_audioListener = std::make_unique<runtime::AudioListenerBridge>();
        m_audioListener->Initialize(m_audio.get(), m_world.get());
        m_audioListener->SetListenerEntity(m_player);

        m_npc = std::make_unique<runtime::NpcSystem>();
        m_npc->Initialize(m_world.get(), m_move.get());
        m_npc->SetPlayer(m_player);

        // ---- 스크립트 런타임(package 활성 — require 커스텀 로더) ----
        m_runtime = std::make_unique<script::ScriptRuntime>();
        script::StdLibPolicy policy;
        policy.package = true;   // require 를 켜되 커스텀 로더로 assets://scripts 를 해석
        m_runtime->Initialize(policy, m_bus, m_assets.get());

        m_ecsBinding = std::make_unique<script::EcsBindingModule>(m_world.get());
        m_runtime->AddBindingModule(m_ecsBinding.get());
        m_runtime->AddBindingModule(std::make_unique<script::MathBindingModule>());
        m_runtime->AddBindingModule(std::make_unique<script::InputBindingModule>(m_input));
        m_runtime->AddBindingModule(std::make_unique<script::EventBindingModule>());
        {
            auto audioBinding = std::make_unique<script::AudioBindingModule>(m_audio.get());
            audioBinding->SetCueResolver([this](std::string_view name) -> const audio::AudioCue* {
                if (name == "cue_footstep") return &m_footstepCue;
                return nullptr;
            });
            m_runtime->AddBindingModule(std::move(audioBinding));
        }
        // 런타임 바인딩(dialogue/cutscene/camera/save/loc/scene).
        m_runtimeBindings = std::make_unique<runtime::RuntimeBindings>(
            m_dialogue.get(), m_cutscene.get(), nullptr, nullptr, m_loc.get());
        m_runtime->AddBindingModule(m_runtimeBindings.get());
        // NPC 바인딩(mye.npc.*).
        m_npcBindings = std::make_unique<runtime::NpcBindings>(m_npc.get(), m_runtime.get());
        m_runtime->AddBindingModule(m_npcBindings.get());

        m_scriptSystem = std::make_unique<script::ScriptSystem>(
            *m_runtime, *m_world, m_bus, m_assets.get());
        m_scriptSystem->RegisterComponent();

        // 대화 표시 캡처 훅: say/say_key/choose 를 감싸 현재 화자·본문·선택지의 로컬라이즈된
        //   최종 문자열을 전역(_dlg_speaker/_dlg_body/_dlg_choices)에 저장한다. 우리 text 오버레이가
        //   이걸 읽어 한글 대화창을 그린다(DialogueBox 없이 자체 표현).
        InstallDialogueCaptureShim();

        // require("npc_common") 해석: 모듈 소스를 package.loaded 에 미리 등록.
        InstallRequireModule("npc_common", "assets://scripts/npc_common.lua");

        // ---- 스크립트 부착(플레이어 + NPC) ----
        AttachScript(m_player, "assets://scripts/player.lua");
        AttachScript(m_chief, "assets://scripts/npc_chief.lua");
        AttachScript(m_merchant, "assets://scripts/npc_merchant.lua");
        AttachScript(m_guard, "assets://scripts/npc_guard.lua");

        // ---- 발소리 애니 이벤트 → AudioCue (월드버스) ----
        m_footstepSub = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<anim::AnimationEvent>([this](const anim::AnimationEvent& e) {
                const char* n = e.name ? e.name : "";
                if (std::string_view(n) == "footstep") {
                    ++m_footstepCount;
                    if (m_audio && m_footstepCue.IsValid()) {
                        const auto* lt = m_world->TryGet<scene::LocalTransform>(e.entity);
                        Vec2 pos = lt ? Vec2{lt->position.x, lt->position.y} : Vec2{};
                        m_audio->PostCue(m_footstepCue, std::optional<Vec2>(pos));
                    }
                }
                return false;
            })};
        m_errorSub = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<script::ScriptErrorEvent>([this](const script::ScriptErrorEvent& e) {
                ++m_scriptErrorCount;
                m_lastError = "[" + e.callback + "] " + e.file + ":" + std::to_string(e.line) +
                              " " + e.message;
                MYE_LOG_WARN("VillageDemo", "ScriptError: {}", m_lastError);
                return false;
            })};

        // ---- NPC 등록(mye.npc.register) — Lua 에서 웨이포인트·on_interact 배선 ----
        // NPC 스크립트가 proximity+E 로 대화를 자체 구동하므로(npc_common), NpcSystem 에는
        //   배회만 등록한다(on_interact 없음 → NpcSystem 은 배회·접근정지만, 대화는 Lua on_update).
        RegisterNpcWander("chief", m_chief, m_npcSpawns[0].waypoints, runtime::WanderMode::Patrol);
        RegisterNpcWander("guard", m_guard, m_npcSpawns[2].waypoints, runtime::WanderMode::Patrol);
        // 상인은 제자리(교차로). 배회 없음.

        // 인트로 컷신을 부팅 후 1회 시작(코루틴).
        m_pendingIntro = (m_control->scenario == Scenario::Free);
    }

    // Lua 모듈을 package.loaded[name] 에 미리 등록(require 해석).
    void InstallRequireModule(const char* name, const char* vpath) {
        auto h = LoadScript(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) return;
        m_scriptHandles.push_back(h);
        sol::state& lua = m_runtime->State();
        sol::protected_function_result r =
            lua.safe_script(h.Get()->source, sol::script_pass_on_error, std::string("@") + vpath);
        if (!r.valid()) {
            sol::error err = r;
            MYE_LOG_ERROR("VillageDemo", "require module '{}' load failed: {}", name, err.what());
            return;
        }
        sol::table pkg = lua["package"];
        if (pkg.valid()) {
            sol::table loaded = pkg["loaded"];
            if (loaded.valid()) loaded[name] = r.get<sol::object>();
        }
    }

    // say/say_key/choose 를 감싸 로컬라이즈된 표시 문자열을 전역에 캡처(오버레이 표시용).
    void InstallDialogueCaptureShim() {
        auto r = m_runtime->DoString(R"LUA(
            _dlg_speaker = ""
            _dlg_body = ""
            _dlg_choices = nil
            do
                local L = function(k) return (mye.loc and mye.loc.text and mye.loc.text(k)) or k end
                local raw_say_key = mye.dialogue.say_key
                local raw_say     = mye.dialogue.say
                local raw_choose  = mye.dialogue.choose
                function mye.dialogue.say_key(sp, bd)
                    _dlg_speaker = L(sp); _dlg_body = L(bd); _dlg_choices = nil
                    return raw_say_key(sp, bd)
                end
                function mye.dialogue.say(sp, bd)
                    _dlg_speaker = sp or ""; _dlg_body = bd or ""; _dlg_choices = nil
                    return raw_say(sp, bd)
                end
                function mye.dialogue.choose(options)
                    _dlg_choices = options
                    return raw_choose(options)
                end
            end
        )LUA", "dialogue.capture");
        if (!r) MYE_LOG_WARN("VillageDemo", "dialogue capture shim failed: {}", r.GetError().message);
    }

    void AttachScript(ecs::Entity e, const char* vpath) {
        auto h = LoadScript(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) return;
        m_scriptHandles.push_back(h);
        auto& sc = m_world->Add<script::ScriptComponent>(e);
        sc.script = h;
    }

    void RegisterNpcWander(const char* id, ecs::Entity e, const std::vector<Vec2>& wps,
                           runtime::WanderMode mode) {
        runtime::NpcDesc d;
        d.entity = e; d.id = id; d.mode = wps.empty() ? runtime::WanderMode::None : mode;
        d.waypoints = wps; d.loop = true;
        d.moveSpeed = 2.2f; d.waitMin = 0.3f; d.waitMax = 0.8f;
        d.alertRadius = 2.2f; d.interactRadius = 1.4f; d.facePlayerOnAlert = true;
        m_npc->Register(d);
    }

    // 강제 이동 방향 주입(_demo_move 전역). Lua player.lua on_update 가 소비.
    void SetForcedMove(std::optional<Vec2> dir) {
        if (!m_runtime || !m_runtime->IsInitialized()) return;
        sol::state& lua = m_runtime->State();
        if (dir) lua["_demo_move"] = *dir; else lua["_demo_move"] = sol::nil;
    }

    void SetPlayerPos(Vec3 p) {
        if (auto* lt = m_world->TryGet<scene::LocalTransform>(m_player)) { lt->position = p; lt->dirty = true; }
    }

    void ApplyScenario() {
        const Scenario sc = m_control->scenario;
        m_camFixed = false;
        switch (sc) {
            case Scenario::Walk:
                SetPlayerPos(Vec3{0.0f, -3.0f, 0.0f});
                SetForcedMove(Vec2{1.0f, 0.0f});   // 동쪽으로 이동(위치 변화 검증)
                break;
            case Scenario::WalkUp:
                SetPlayerPos(Vec3{0.0f, -3.0f, 0.0f});
                SetForcedMove(Vec2{0.0f, 1.0f});
                break;
            case Scenario::Bridge:
                // 플레이어를 다리 위(floor1)에 배치 → 상판·개울 위 하이브리드 정렬.
                SetPlayerPos(Vec3{5.5f, 0.0f, 0.0f});
                if (auto* fl = m_world->TryGet<scene::FloorLevel>(m_player)) fl->level = 1;
                m_camera.SetPosition({5.5f, 0.0f}); m_camFixed = true;
                break;
            case Scenario::UnderBridge:
                // 플레이어(floor0)가 다리 상판 아래를 통과 — 상판이 상반신 가림.
                SetPlayerPos(Vec3{5.5f, -0.5f, 0.0f});
                if (auto* fl = m_world->TryGet<scene::FloorLevel>(m_player)) fl->level = 0;
                m_camera.SetPosition({5.5f, 0.0f}); m_camFixed = true;
                break;
            case Scenario::Talk:
                // 촌장 근처로 이동 → 대화 자동 트리거(아래 Update 에서 강제).
                SetPlayerPos(Vec3{kChiefPos.x + 1.0f, kChiefPos.y, 0.0f});
                m_camera.SetPosition({kChiefPos.x, kChiefPos.y}); m_camFixed = true;
                m_forceTalk = m_chief;
                break;
            case Scenario::TalkMerchant:
                SetPlayerPos(Vec3{kMerchantPos.x + 1.0f, kMerchantPos.y, 0.0f});
                m_camera.SetPosition({kMerchantPos.x, kMerchantPos.y}); m_camFixed = true;
                m_forceTalk = m_merchant;
                break;
            case Scenario::TalkGuard:
                SetPlayerPos(Vec3{kGuardPos.x - 1.0f, kGuardPos.y, 0.0f});
                m_camera.SetPosition({kGuardPos.x, kGuardPos.y}); m_camFixed = true;
                m_forceTalk = m_guard;
                break;
            case Scenario::BehindProp:
                // 플레이어를 석상 뒤(더 위=뒤) → 석상이 가림.
                SetPlayerPos(Vec3{kStatuePos.x, kStatuePos.y + 1.2f, 0.0f});
                m_camera.SetPosition({kStatuePos.x, kStatuePos.y}); m_camFixed = true;
                break;
            case Scenario::Wander:
                // NPC 배회 관찰(플레이어는 멀리). 카메라를 촌장 플라자에 고정.
                SetPlayerPos(Vec3{0.0f, -6.0f, 0.0f});
                m_camera.SetPosition({-7.0f, 4.5f}); m_camFixed = true;
                break;
            case Scenario::Free:
            default:
                m_camFixed = false;
                break;
        }
        scene::UpdateWorldTransforms(*m_world);
    }

    // ---- 시뮬(고정틱): 이동·NPC·오디오 리스너·컷신 sim ----
    void FixedUpdate(const mye::TimeStep& step) {
        if (!m_world) return;
        m_prevPlayer = m_curPlayer;
        if (auto* lt = m_world->TryGet<scene::LocalTransform>(m_player)) m_curPlayer = lt->position;

        const float dt = static_cast<float>(step.deltaSeconds);

        // MoveController 는 프레임당 1회만(NPC + 컷신 공유). cutscene->UpdateSim 이 move->Update 를
        //   호출하지 않도록, 여기서 move->Update 를 직접 돌리고 npc->Tick 을 이어붙인다.
        if (m_move) m_move->Update(dt);
        if (m_npc)  m_npc->Tick(dt);
        if (m_audioListener) m_audioListener->Update();
    }

    // ---- 표현(가변): 스크립트·애니·대화·컷신 view·카메라 ----
    void Update(const mye::TimeStep& step) {
        if (m_input && m_input->WasPressed(mye::KeyCode::Escape))
            m_control->app->RequestExit(0);
        if (!m_world) return;

        const float dt = static_cast<float>(step.deltaSeconds);

        // 인트로 컷신 1회 시작.
        if (m_pendingIntro && m_frameCount >= 2) {
            m_pendingIntro = false;
            m_allowCameraFocus = true;   // 인트로 동안 컷신 카메라 포커스 허용.
            m_introRunning = true;
            StartIntroCutscene();
        }
        // 인트로 컷신 종료 감지(코루틴 종료 + 대화 닫힘) → 카메라 제어를 플레이어에게 반환.
        if (m_introRunning && m_runtime->Coroutines().ActiveCount() == 0 &&
            (!m_dialogue || !m_dialogue->IsActive())) {
            m_introRunning = false;
            m_allowCameraFocus = false;
        }

        // 강제 대화 시나리오: 지정 프레임에 E 입력을 시뮬(Lua NPC 스크립트가 소비).
        if (m_forceTalk.has_value() && !m_forcedTalkFired && m_frameCount >= 3) {
            m_forcedTalkFired = true;
            TriggerTalk(*m_forceTalk);
        }

        // 스크립트 on_update + 코루틴 tick.
        if (m_scriptSystem) {
            m_scriptSystem->EnsureInstances();
            m_scriptSystem->Update(dt);
            m_ecsBinding->FlushDeferred();
            m_runtime->UpdateCoroutines(dt);
        }

        // 애니메이션.
        scene::UpdateWorldTransforms(*m_world);
        anim::RunAnimationSystem(*m_world, dt);

        // 대화·컷신 표현 갱신.
        if (m_dialogue) m_dialogue->Update(dt);
        if (m_cutscene) m_cutscene->UpdateView(dt, m_world.get());

        // 강제 대화(시나리오): 대화가 열리면 몇 프레임 뒤 자동 advance/pick 으로 진행(결정적).
        DriveScriptedDialogue();

        if (m_audio) m_audio->Update(dt);
    }

    // 인트로 컷신(intro_cutscene.lua) 코루틴 시작.
    void StartIntroCutscene() {
        sol::state& lua = m_runtime->State();
        lua["_CHIEF"] = static_cast<double>(m_chief.Packed());
        auto r = m_runtime->DoString(R"LUA(
            mye.co.start(function()
                local ok, intro = pcall(require, 'intro_cutscene')
                if ok and intro and intro.play then intro.play({ chief = _CHIEF }) end
            end)
        )LUA", "intro.boot");
        if (!r) MYE_LOG_WARN("VillageDemo", "intro cutscene start failed: {}", r.GetError().message);
    }

    // 시나리오 강제 대화: 플레이어를 NPC 앞에 두고 E 를 시뮬(Lua NPC on_update 가 대화 시작).
    void TriggerTalk(ecs::Entity /*npc*/) {
        // player.lua 는 대화중 이동 잠금. NPC 스크립트는 was_pressed(E) 로 대화 시작.
        // InputState 를 직접 조작할 수 없으므로, NPC 스크립트 대신 여기서 대화를 직접 개시한다:
        //   해당 NPC 의 on_interact 상당 대화를 mye.npc / dialogue 로 시작한다.
        // 간단·결정적 경로: 촌장/상인/경비 대화를 loc 키로 직접 say 코루틴 시작.
        const char* speakerKey = "npc.chief.name";
        const char* bodyKey = "dialogue.chief.greet";
        if (m_forceTalk == m_merchant) { speakerKey = "npc.merchant.name"; bodyKey = "dialogue.merchant.greet"; }
        else if (m_forceTalk == m_guard) { speakerKey = "npc.guard.name"; bodyKey = "dialogue.guard.greet"; }
        sol::state& lua = m_runtime->State();
        lua["_SPK"] = speakerKey; lua["_BODY"] = bodyKey;
        auto r = m_runtime->DoString(R"LUA(
            mye.co.start(function()
                mye.dialogue.say_key(_SPK, _BODY)
                local pick = mye.dialogue.choose({ "네", "아니오" })
                _talk_pick = pick
            end)
        )LUA", "forced.talk");
        if (!r) MYE_LOG_WARN("VillageDemo", "forced talk failed: {}", r.GetError().message);
    }

    // 결정적 진행: 시나리오 대화가 열려 있으면 프레임 간격으로 자동 advance/pick.
    void DriveScriptedDialogue() {
        if (!m_forceTalk.has_value() || !m_dialogue) return;
        if (!m_dialogue->IsActive()) return;
        ++m_dialogueVisibleFrames;
        // 덤프 중에는 대화창을 열어둔 채로 유지(한글 글리프 픽셀 검증). 덤프가 아니면 자동 진행.
        if (m_control->dumpEnabled || m_control->dumpWithUi) return;
        if (m_dialogueVisibleFrames % 8 == 0) {
            if (m_dialogue->IsWaitingChoice()) m_dialogue->Pick(0);
            else if (m_dialogue->IsWaitingAdvance()) m_dialogue->Advance();
        }
    }

    // ---- 렌더 ----
    void Render(const mye::TimeStep& /*step*/) {
        if (!m_gpuOk || !m_device || !m_swapChain || !m_rt.IsInitialized()) {
            // 헤드리스(GPU 없음): 프레임만 카운트해 종료 조건 충족.
            ++m_frameCount;
            if (m_control->frameLimit && m_frameCount >= m_control->maxFrames) {
                EmitVerify();
                m_control->app->RequestExit(0);
            }
            return;
        }

        // 카메라 추적(비고정 & 컷신 포커스 비활성).
        if (!m_camFixed && !m_camFocusActive) {
            const float alpha = static_cast<float>(m_ctx->Time().GetInterpolationAlpha());
            const Vec2 rp = Vec2::Lerp(Vec2{m_prevPlayer.x, m_prevPlayer.y},
                                       Vec2{m_curPlayer.x, m_curPlayer.y}, alpha);
            m_camera.SetPosition(rp);
        }
        m_camFocusActive = false;   // 매 프레임 리셋(컷신 포커스가 재설정)

        scene::UpdateWorldTransforms(*m_world);
        m_scheduler->RunPhase(scene::Phase::RenderExtract, mye::TimeStep{});

        m_device->BeginFrame();
        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        m_rt.BeginScenePass(cmd, kClearColor);
        m_hybrid.Render(m_proxies, m_camera, cmd);
        // 대화창 오버레이(한글 텍스트)는 씬 RT 안에 그려 픽셀퍼펙트 블릿에 포함.
        const bool wantDialogueOverlay = m_dialogue && m_dialogue->IsActive() &&
            (!m_control->dumpEnabled || m_control->dumpWithUi);
        if (wantDialogueOverlay) RenderDialogueOverlay(cmd);
        m_rt.EndScenePass(cmd);

        const Vec2i winSize = m_swapChain->GetSize();
        rhi::TextureHandle backbuffer = m_swapChain->GetCurrentBackBuffer();
        m_rt.Blit(cmd, backbuffer, winSize, m_camera.SubpixelResidual());

        const render::HybridRenderer::Stats stats = m_hybrid.LastStats();

        const bool drawUi = m_debugUi.IsInitialized() && !m_control->dumpEnabled;
        if (drawUi) DrawDebugOverlay(cmd, backbuffer, winSize, stats);
        m_lastStats = stats;

        ++m_frameCount;
        HandleDumps(backbuffer);

        m_swapChain->Present(false);
        m_device->EndFrame();

        m_secondsAccum += m_ctx->Time().CurrentStep().unscaledDeltaSeconds;
        if (m_secondsAccum >= 1.0) {
            m_fps = static_cast<float>(m_frameCount - m_lastFrameCount);
            m_lastFrameCount = m_frameCount;
            m_secondsAccum -= 1.0;
        }

        if (m_control->frameLimit && m_frameCount >= m_control->maxFrames) {
            EmitVerify();
            m_control->app->RequestExit(0);
        }
    }

    // 한글 대화창을 씬 RT 좌표(픽셀=unit, 스크린 카메라)로 SpriteBatch + TextRenderer 로 그린다.
    void RenderDialogueOverlay(rhi::ICommandContext& cmd) {
        // 픽셀퍼펙트 내부 RT 는 960×540 논리 해상도. 스크린 카메라(픽셀=unit, 좌상단 원점, +Y 아래)를
        //   직교 투영으로 구성해 SpriteBatch/TextRenderer 로 제출한다.
        const float W = 960.0f, H = 540.0f;
        Mat4 viewProj = OrthoScreen(W, H);

        // 대화 텍스트 조회.
        std::string speaker = m_dialogueSpeaker;
        std::string body = m_dialogueBody;
        RefreshDialogueText(speaker, body);

        const float boxX = 40.0f, boxW = W - 80.0f;
        const float boxH = 130.0f, boxY = H - boxH - 30.0f;

        m_spriteBatch.Begin(viewProj);
        // 반투명 어두운 패널(white.png 틴트) — 배경.
        SubmitQuad(boxX, boxY, boxW, boxH, Color{0.06f, 0.07f, 0.11f, 0.92f});
        // 이름표 배경(상단 좌측).
        SubmitQuad(boxX + 12.0f, boxY - 26.0f, 180.0f, 28.0f, Color{0.15f, 0.20f, 0.34f, 0.95f});
        // 선택지 버튼(있으면).
        if (m_dialogue->IsWaitingChoice()) {
            for (int i = 0; i < static_cast<int>(m_choiceLabels.size()); ++i) {
                float by = boxY + 40.0f + static_cast<float>(i) * 30.0f;
                SubmitQuad(boxX + 40.0f, by, boxW - 80.0f, 26.0f,
                           Color{0.22f, 0.26f, 0.36f, 0.95f});
            }
        }
        m_spriteBatch.End(cmd);

        // 텍스트(맑은 고딕). TextRenderer::DrawText 가 레이아웃+아틀라스+제출을 한 번에.
        if (m_koFont) {
            m_spriteBatch.Begin(viewProj);
            text::TextStyle nameStyle; nameStyle.font = m_koFontId; nameStyle.size = 18;
            nameStyle.color = Color{0.95f, 0.92f, 0.60f, 1.0f};
            text::TextStyle bodyStyle; bodyStyle.font = m_koFontId; bodyStyle.size = 22;
            bodyStyle.color = Color{0.96f, 0.96f, 0.92f, 1.0f};
            text::LayoutParams bodyParams; bodyParams.maxWidth = boxW - 40.0f;
            bodyParams.wrap = text::WrapMode::WordChar;

            m_textRenderer.DrawText(cmd, m_spriteBatch, m_atlas, *m_fonts, speaker, nameStyle,
                                    Vec2i{static_cast<int>(boxX + 20.0f), static_cast<int>(boxY - 22.0f)});
            m_textRenderer.DrawText(cmd, m_spriteBatch, m_atlas, *m_fonts, body, bodyStyle,
                                    Vec2i{static_cast<int>(boxX + 20.0f), static_cast<int>(boxY + 16.0f)},
                                    bodyParams);
            if (m_dialogue->IsWaitingChoice()) {
                text::TextStyle chStyle; chStyle.font = m_koFontId; chStyle.size = 18;
                chStyle.color = Color{0.90f, 0.94f, 1.0f, 1.0f};
                for (int i = 0; i < static_cast<int>(m_choiceLabels.size()); ++i) {
                    float by = boxY + 42.0f + static_cast<float>(i) * 30.0f;
                    m_textRenderer.DrawText(cmd, m_spriteBatch, m_atlas, *m_fonts,
                                            m_choiceLabels[static_cast<size_t>(i)], chStyle,
                                            Vec2i{static_cast<int>(boxX + 52.0f), static_cast<int>(by)});
                }
            }
            m_spriteBatch.End(cmd);
        }
    }

    // 현재 대화 라인의 화자·본문을 loc 로 해석(우리가 자체 표시하므로 상태에서 재구성).
    // DialogueSystem 은 box=nullptr 이라 라인 데이터를 직접 노출하지 않으므로, say_key/choose 시
    //   Lua 래퍼가 채운 전역(_dlg_speaker/_dlg_body/_dlg_choices)을 읽는다(아래 훅 설치).
    void RefreshDialogueText(std::string& speaker, std::string& body) {
        sol::state& lua = m_runtime->State();
        sol::object sp = lua["_dlg_speaker"]; sol::object bd = lua["_dlg_body"];
        if (sp.is<std::string>()) speaker = sp.as<std::string>();
        if (bd.is<std::string>()) body = bd.as<std::string>();
        m_choiceLabels.clear();
        sol::object ch = lua["_dlg_choices"];
        if (ch.is<sol::table>()) {
            sol::table t = ch.as<sol::table>();
            for (auto& kv : t) if (kv.second.is<std::string>())
                m_choiceLabels.push_back(kv.second.as<std::string>());
        }
        m_dialogueSpeaker = speaker; m_dialogueBody = body;
    }

    void SubmitQuad(float x, float y, float w, float h, Color c) {
        render::SpriteDraw d;
        d.position = {x, y};
        d.size = {w, h};
        d.pivot = {0.0f, 0.0f};   // 좌상단 기준
        d.texture = WhiteTexture();
        d.tint = c;
        d.sortY = y;
        m_spriteBatch.Submit(d);
    }

    rhi::TextureHandle WhiteTexture() {
        auto it = m_resolvers.textures.find(AssetResolvers::Key(m_guidWhite));
        return it != m_resolvers.textures.end() ? it->second->gpuTexture : rhi::TextureHandle{};
    }

    // 스크린 직교 투영(픽셀=unit, 좌상단 (0,0), +Y 아래, LH NDC 0~1). SpriteBatch VS 가 소비.
    //   left=0,right=w,bottom=h,top=0 로 두면 y 가 아래로 갈수록 증가(스크린 규약).
    static Mat4 OrthoScreen(float w, float h) {
        return Mat4::OrthoOffCenterLH(0.0f, w, h, 0.0f, 0.0f, 1.0f);
    }

    void DrawDebugOverlay(rhi::ICommandContext& cmd, rhi::TextureHandle backbuffer,
                          Vec2i winSize, const render::HybridRenderer::Stats& stats) {
        m_debugUi.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("VillageDemo (M6)");
        ImGui::Text("FPS: %.1f", m_fps);
        ImGui::Text("Draw calls: %u  Sprites: %u  Tiles: %u  Meshes: %u",
                    stats.drawCalls, stats.spriteCount, stats.tileQuadCount, stats.meshCount);
        ImGui::Separator();
        if (const auto* lt = m_world->TryGet<scene::LocalTransform>(m_player)) {
            const auto* fl = m_world->TryGet<scene::FloorLevel>(m_player);
            ImGui::Text("Player pos=(%.2f,%.2f) floor=%d", lt->position.x, lt->position.y,
                        fl ? fl->level : 0);
        }
        ImGui::Text("Footsteps: %llu  ScriptErrors: %llu",
                    static_cast<unsigned long long>(m_footstepCount),
                    static_cast<unsigned long long>(m_scriptErrorCount));
        ImGui::Text("NPCs: %zu  DialogueActive: %d", m_npc ? m_npc->Count() : 0,
                    (m_dialogue && m_dialogue->IsActive()) ? 1 : 0);
        ImGui::Separator();
        ImGui::TextWrapped("WASD move. E to talk. Live-edit assets/scripts/*.lua or loc/ko.json.");
        ImGui::End();

        rhi::RenderPassColorAttachment uiColor{};
        uiColor.texture = backbuffer; uiColor.loadOp = rhi::LoadOp::Load;
        rhi::RenderPassBeginDesc uiPass{};
        uiPass.colorAttachments = {&uiColor, 1};
        uiPass.debugName = "imgui.overlay";
        cmd.BeginRenderPass(uiPass);
        rhi::Viewport uiVp{};
        uiVp.width = static_cast<float>(winSize.x);
        uiVp.height = static_cast<float>(winSize.y);
        uiVp.maxDepth = 1.0f;
        cmd.SetViewport(uiVp);
        m_debugUi.EndFrame();
        cmd.EndRenderPass();
    }

    void HandleDumps(rhi::TextureHandle backbuffer) {
        if ((m_control->dumpEnabled || m_control->dumpWithUi) && !m_dumped) {
            const bool last = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
            if (last || (!m_control->frameLimit && m_frameCount >= 5)) {
                auto cap = rhi::CaptureBackbuffer(*m_device, backbuffer, m_control->dumpPath);
                if (cap) MYE_LOG_INFO("VillageDemo", "dump -> '{}' (frame {})",
                                      m_control->dumpPath, m_frameCount);
                else MYE_LOG_ERROR("VillageDemo", "dump failed: {}", cap.GetError().message);
                m_dumped = true;
            }
        }
    }

    void EmitVerify() {
        Vec2 pp{m_curPlayer.x, m_curPlayer.y};
        int floor = 0;
        if (const auto* fl = m_world ? m_world->TryGet<scene::FloorLevel>(m_player) : nullptr)
            floor = fl->level;
        const bool dlgActive = m_dialogue && m_dialogue->IsActive();
        const Vec2 camPos = m_camera.Position();
        MYE_LOG_INFO("VillageDemo", "M6-VERIFY-CAM: camPos=({:.2f},{:.2f}) camFixed={}",
                     camPos.x, camPos.y, m_camFixed ? 1 : 0);
        MYE_LOG_INFO("VillageDemo",
            "M6-VERIFY: scenario={} frames={} playerPos=({:.2f},{:.2f}) floor={} footsteps={} "
            "npcs={} dialogueActive={} scriptErrors={} sprites={} tiles={} meshes={} draws={} moved={:.3f}",
            static_cast<int>(m_control->scenario), m_frameCount, pp.x, pp.y, floor,
            m_footstepCount, m_npc ? m_npc->Count() : 0, dlgActive ? 1 : 0, m_scriptErrorCount,
            m_lastStats.spriteCount, m_lastStats.tileQuadCount, m_lastStats.meshCount, m_lastStats.drawCalls,
            std::sqrt((pp.x - m_startPlayer.x) * (pp.x - m_startPlayer.x) +
                      (pp.y - m_startPlayer.y) * (pp.y - m_startPlayer.y)));
    }

    // ---- 상수 위치 ----
    static constexpr Vec3 kChiefPos{-6.5f, 4.0f, 0.0f};
    static constexpr Vec3 kMerchantPos{2.5f, 0.0f, 0.0f};
    static constexpr Vec3 kGuardPos{8.5f, 0.0f, 0.0f};

    RunControl* m_control = nullptr;
    mye::EngineContext* m_ctx = nullptr;
    mye::EventBus* m_bus = nullptr;
    mye::InputState* m_input = nullptr;
    bool m_gpuOk = false;

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;
    std::string m_assetsDir;

    std::vector<asset::AssetHandle<asset::Texture>> m_texHandles;
    std::vector<asset::AssetHandle<asset::Mesh>>    m_meshHandles;
    std::vector<asset::AssetHandle<asset::AudioClip>> m_audioClipHandles;
    std::vector<asset::AssetHandle<script::ScriptAsset>> m_scriptHandles;
    asset::AssetHandle<asset::AudioClip> m_footstepClip, m_bgmClip;
    AssetResolvers m_resolvers;
    std::unordered_map<uint64_t, Vec2i> m_texSizes;

    asset::AssetGuid m_guidGrass, m_guidPath, m_guidWater, m_guidBridge, m_guidSlope, m_guidPlaza, m_guidWhite;
    asset::AssetGuid m_guidPlayerSheet, m_guidChiefSheet, m_guidMerchantSheet, m_guidGuardSheet;
    asset::AssetGuid m_guidStatue, m_guidFountain;

    std::unique_ptr<ecs::World>             m_world;
    std::unique_ptr<scene::SystemScheduler> m_scheduler;
    std::unique_ptr<phys::PhysicsWorld2D>   m_physics;
    std::unique_ptr<tilemap::TilemapWorld>  m_tilemapGround, m_tilemapPath, m_tilemapWater,
                                            m_tilemapPlaza, m_tilemapSlope, m_tilemapBridge;
    scene::RenderProxyList m_proxies;

    ecs::Entity m_player, m_chief, m_merchant, m_guard;
    std::vector<NpcSpawn> m_npcSpawns;

    // 애니 데이터.
    std::vector<std::unique_ptr<asset::SpriteSheet>> m_sheets;
    std::vector<asset::AnimationClipData> m_walkClips, m_idleClips;
    anim::AnimStateMachine m_machine;

    // 오디오.
    std::unique_ptr<audio::AudioEngine> m_audio;
    audio::AudioCue m_footstepCue;
    uint64_t m_footstepCount = 0;
    mye::ScopedSubscription m_footstepSub;

    // 스크립트.
    std::unique_ptr<script::ScriptRuntime> m_runtime;
    std::unique_ptr<script::ScriptSystem>  m_scriptSystem;
    std::unique_ptr<script::EcsBindingModule> m_ecsBinding;
    mye::ScopedSubscription m_errorSub;
    uint64_t m_scriptErrorCount = 0;
    std::string m_lastError;

    // 런타임 서브시스템.
    std::unique_ptr<runtime::LocalizationSystem>  m_loc;
    std::unique_ptr<runtime::DialogueSystem>      m_dialogue;
    std::unique_ptr<runtime::MoveController>      m_move;
    std::unique_ptr<runtime::CameraFocusController> m_camFocus;
    std::unique_ptr<runtime::CutsceneRuntime>     m_cutscene;
    std::unique_ptr<runtime::AudioListenerBridge> m_audioListener;
    std::unique_ptr<runtime::NpcSystem>           m_npc;
    std::unique_ptr<runtime::RuntimeBindings>     m_runtimeBindings;
    std::unique_ptr<runtime::NpcBindings>         m_npcBindings;
    bool m_pendingIntro = false;

    // 한글 텍스트 스택.
    std::unique_ptr<text::FontRegistry> m_fonts;
    std::vector<uint8_t> m_fontBlob;
    text::FontId m_koFontId{};
    text::IFont* m_koFont = nullptr;
    text::GlyphAtlas m_atlas;
    text::TextRenderer m_textRenderer;
    render::SpriteBatch m_spriteBatch;
    std::string m_dialogueSpeaker, m_dialogueBody;
    std::vector<std::string> m_choiceLabels;

    render::PixelPerfectTarget m_rt;
    render::HybridRenderer     m_hybrid;
    render::HybridRenderer::Stats m_lastStats{};
    render::Camera2D           m_camera;
    mye::imgui::DebugUi        m_debugUi;
    mye::ScopedSubscription    m_onResized;

    Vec3 m_curPlayer{0.0f, -3.0f, 0.0f};
    Vec3 m_prevPlayer{0.0f, -3.0f, 0.0f};
    Vec3 m_startPlayer{0.0f, -3.0f, 0.0f};
    bool m_camFixed = false;
    bool m_camFocusActive = false;
    bool m_allowCameraFocus = false;   // 컷신 카메라 포커스가 실제 카메라를 제어해도 되는지
    bool m_introRunning = false;

    std::optional<ecs::Entity> m_forceTalk;
    bool m_forcedTalkFired = false;
    uint64_t m_dialogueVisibleFrames = 0;

    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
    double   m_secondsAccum = 0.0;
    uint64_t m_lastFrameCount = 0;
    float    m_fps = 0.0f;
};

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
class VillageDemoApp final : public mye::Application {
public:
    explicit VillageDemoApp(const mye::LaunchArgs& args) {
        m_control.app = this;
        const auto& a = args.args;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_control.frameLimit = true;
                m_control.maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_control.dumpEnabled = true; m_control.dumpPath = a[++i];
            } else if (a[i] == "--dump-ui" && i + 1 < a.size()) {
                m_control.dumpWithUi = true; m_control.dumpPath = a[++i];
            } else if (a[i] == "--scenario" && i + 1 < a.size()) {
                m_control.scenario = ParseScenario(a[++i]); m_control.scenarioSet = true;
            } else if (a[i] == "--headless") {
                m_control.headless = true;
            }
        }
    }

    void OnConfigure(mye::ConfigSystem& /*config*/) override {}
    void OnRegisterModules(mye::ModuleRegistry& modules) override {
        modules.Register(std::make_unique<VillageDemoModule>(&m_control));
    }
    void OnStart(mye::EngineContext& /*ctx*/) override {
        MYE_LOG_INFO("VillageDemo", "village_demo start (frames={} scenario={})",
                     m_control.frameLimit ? static_cast<long long>(m_control.maxFrames) : -1LL,
                     static_cast<int>(m_control.scenario));
    }

private:
    RunControl m_control;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) { return new VillageDemoApp(args); }
} // namespace mye

static LONG WINAPI VillageVeh(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        HMODULE base = ::GetModuleHandleW(nullptr);
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        uintptr_t rva = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(base);
        MYE_LOG_FATAL("VillageVEH", "AV at rva=0x{:x} addr=0x{:x} op={} target=0x{:x}",
                      rva, reinterpret_cast<uintptr_t>(addr),
                      ep->ExceptionRecord->ExceptionInformation[0],
                      ep->ExceptionRecord->ExceptionInformation[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int /*argc*/, char** /*argv*/) {
    if (std::getenv("MYE_VEH")) ::AddVectoredExceptionHandler(1, &VillageVeh);
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    }
    launch.mainWindow.title = "MyEngine — village_demo (M6)";
    launch.mainWindow.clientSize = {960, 540};
    launch.mainWindow.resizable = true;
    return mye::GuardedMain(launch);
}
