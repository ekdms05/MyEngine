// character_demo — M3 통합 데모·검증 (docs/00 §7 M3 완료 기준, M3-C)
//
// M3 를 관통 증명한다:
//   (1) 8방향 걷기↔대기 전환이 자연스럽다(SpriteAnimator 상태머신 + Dir8).
//   (2) 발소리가 애니메이션 이벤트→AudioCue 로 재생된다(월드버스 구독 + Lua on_event).
//   (3) NPC/플레이어 .lua 를 수정·저장하면 실행 중 즉시 반영된다(핫 리로드, self.state 생존).
//   (4) 스크립트 런타임 에러가 나도 해당 엔티티만 멈추고 게임은 계속된다(에러 격리).
//
// 관통 계층: GuardedMain · Application · RHI(DX11) · InputState · AssetManager(PNG/WAV/.lua) ·
//   ECS World · SystemScheduler · AnimationSystem · AudioEngine(오프라인) · ScriptRuntime/
//   ScriptSystem · RenderExtract → HybridRenderer · PixelPerfectTarget · DebugUi.
//
// 검증 CLI:
//   --frames N          N 렌더 프레임 후 exit 0
//   --dump path.bmp     마지막 프레임 백버퍼 BMP 저장
//   --scenario <name>   결정적 시나리오. name ∈
//       {free, 8way, walk_down, walk_left, walk_up, walk_right, walk_up_left, ...,
//        footstep, hotreload, error}
//     8way 계열은 강제 이동 방향을 주입(Lua on_update 가 소비)하고, 프레임 진행으로 걷기
//     애니메이션이 돌게 한다. footstep 은 오프라인 믹서 출력이 non-zero 인지 검증한다.
//     hotreload 는 실행 중 player.lua 소스를 바꿔 self.state 생존·on_hot_reload 를 확인한다.
//     error 는 에러 스크립트 엔티티가 정지하고 ScriptErrorEvent 가 1회 발행되며 게임이 계속됨을
//     확인한다. 결과는 로그 "M3-VERIFY:" 라인으로 요약한다(검증 스크립트가 파싱).
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/core/platform/Win32Window.h"

#include "mye/rhi/Rhi.h"

#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AudioClip.h"
#include "mye/asset/AudioImporter.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/asset/SpriteImporter.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/asset/Texture.h"

#include "mye/ecs/World.h"
#include "mye/ecs/View.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/SystemScheduler.h"

#include "mye/anim/AnimationSystem.h"
#include "mye/anim/AnimationTypes.h"
#include "mye/anim/SpriteAnimator.h"

#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioTypes.h"

#include "mye/script/ScriptClass.h"
#include "mye/script/ScriptComponent.h"
#include "mye/script/ScriptImporter.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/ScriptSystem.h"
#include "mye/script/ScriptTypes.h"
#include "mye/script/bindings/EngineBindings.h"

#include "mye/render/Camera2D.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"

#include "mye/imgui/DebugUi.h"
#include "imgui.h"

#include <sol/sol.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace rhi   = mye::rhi;
namespace asset = mye::asset;
namespace render= mye::render;
namespace ecs   = mye::ecs;
namespace scene = mye::scene;
namespace anim  = mye::anim;
namespace audio = mye::audio;
namespace script= mye::script;

using mye::Vec2;
using mye::Vec2i;
using mye::Vec3;
using mye::Color;
using anim::Dir8;

constexpr Color kClearColor{0.05f, 0.06f, 0.09f, 1.0f};
constexpr int   kCellW = 24, kCellH = 32, kCols = 4, kRows = 8;

// ---------------------------------------------------------------------------
// 시나리오
// ---------------------------------------------------------------------------
enum class Scenario {
    Free, EightWay, WalkDir, Footstep, HotReload, Error,
};

struct RunControl {
    mye::Application* app = nullptr;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    std::string dumpPath;
    Scenario scenario = Scenario::Free;
    Dir8     walkDir = Dir8::Down;   // WalkDir/EightWay 진행 방향
    bool     scenarioSet = false;
};

// GUID → 텍스처 해석기(렌더러 콜백).
struct AssetResolvers {
    std::unordered_map<uint64_t, const asset::Texture*> textures;
    static uint64_t Key(asset::AssetGuid g) { return g.hi ^ (g.lo * 1099511628211ull); }
    static const asset::Texture* ResolveTexture(void* user, asset::AssetGuid guid) {
        auto* self = static_cast<AssetResolvers*>(user);
        auto it = self->textures.find(Key(guid));
        return it == self->textures.end() ? nullptr : it->second;
    }
};

// ---------------------------------------------------------------------------
// CharacterDemoModule
// ---------------------------------------------------------------------------
class CharacterDemoModule final : public mye::IModule {
public:
    explicit CharacterDemoModule(RunControl* control) : m_control(control) {}
    const char* GetName() const override { return "CharacterDemo"; }

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

        // VFS + AssetManager
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        m_assetsDir = ResolveAssetsDir();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(m_assetsDir), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::AudioImporter>());
        m_assets->RegisterImporter(std::make_unique<script::ScriptImporter>());

        if (!LoadAssets()) return;

        // 오디오: 오프라인(무음) 모드 — 장치 스레드 없이 믹서만. RenderOffline 로 검증.
        m_audio = std::make_unique<audio::AudioEngine>();
        (void)m_audio->Initialize(nullptr, 44100);   // backend=nullptr → offline
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

        render::Camera2DDesc camDesc{};
        camDesc.position = {0.0f, 0.0f};
        camDesc.pixelSnap = true;
        m_camera = render::Camera2D(camDesc);

        BuildWorldAndScripts();
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

        MYE_LOG_INFO("CharacterDemo", "initialized (assets='{}', scenario={})",
                     m_assetsDir, static_cast<int>(m_control->scenario));
    }

    void OnPostInitialize(mye::EngineContext& ctx) override {
        ctx.Modules().AddTick(this, mye::UpdatePhase::Update,
                              [this](const mye::TimeStep& s) { Update(s); }, 0);
        ctx.Modules().AddTick(this, mye::UpdatePhase::PreRender,
                              [this](const mye::TimeStep& s) { Render(s); }, 0);
    }

    void OnShutdown(mye::EngineContext& /*ctx*/) override {
        // 셧다운 순서: 스크립트/애니 먼저(오디오 큐 참조 해제), 그 다음 오디오, 렌더, 에셋.
        m_onResized.Reset();
        m_debugUi.Shutdown();
        m_scriptSystem.reset();
        // ScriptComponent(sol::table/coroutine)는 소멸 시 lua_State에 luaL_unref 한다.
        // 반드시 world(컴포넌트 보유)를 sol::state보다 먼저 파괴 — 역순이면 죽은
        // lua_State 접근으로 0xC0000005 크래시(정지된 에러 엔티티도 동일 경로).
        m_world.reset();
        m_runtime.reset();       // sol::state 파기(메인 스레드 전용 규약)
        if (m_audio) { m_audio->Shutdown(); m_audio.reset(); }
        m_hybrid.Shutdown();
        m_rt.Shutdown();
        m_texHandles.clear();
        m_footstepClipHandle = {};
        m_scriptHandle = {};
        m_errorScriptHandle = {};
        m_scheduler.reset();
        m_assets.reset();
        m_vfs.reset();
        m_swapChain.reset();
        m_device.reset();
        MYE_LOG_INFO("CharacterDemo", "shutdown ({} frames)", m_frameCount);
    }

private:
    void Fatal(const char* msg, int code) {
        MYE_LOG_FATAL("CharacterDemo", "{}", msg);
        m_control->app->RequestExit(code);
    }

    std::string ResolveAssetsDir() const {
        namespace fs = std::filesystem;
        auto has = [](const fs::path& dir) {
            std::error_code ec;
            return fs::exists(dir / "player_sheet.png", ec);
        };
        wchar_t exePathW[MAX_PATH] = {};
        if (::GetModuleFileNameW(nullptr, exePathW, MAX_PATH) > 0) {
            fs::path dir = fs::path(exePathW).parent_path();
            for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
                const fs::path cand = dir / "samples" / "character_demo" / "assets";
                if (has(cand)) return cand.string();
                dir = dir.parent_path();
            }
        }
        const char* candidates[] = {
            "samples/character_demo/assets", "../samples/character_demo/assets",
            "../../samples/character_demo/assets", "../../../samples/character_demo/assets",
        };
        for (const char* c : candidates) if (has(fs::path(c))) return c;
        return "samples/character_demo/assets";
    }

    asset::AssetGuid LoadTex(const char* vpath) {
        auto h = m_assets->LoadSync<asset::Texture>(vpath);
        if (h.State() != asset::AssetState::Loaded || !h.Get()) {
            MYE_LOG_ERROR("CharacterDemo", "texture load failed: {}", vpath);
            return {};
        }
        asset::AssetGuid g = h.Guid();
        m_resolvers.textures[AssetResolvers::Key(g)] = h.Get();
        m_texHandles.push_back(std::move(h));
        return g;
    }

    bool LoadAssets() {
        m_guidSheet = LoadTex("assets://player_sheet.png");
        m_guidGrid  = LoadTex("assets://tile_grid.png");
        m_guidWhite = LoadTex("assets://white.png");
        if (!m_guidSheet.IsValid()) { Fatal("player_sheet.png load failed", -12); return false; }

        // 발소리 WAV → AudioClip(상주 PCM).
        m_footstepClipHandle = m_assets->LoadSync<asset::AudioClip>("assets://footstep.wav");
        if (m_footstepClipHandle.State() != asset::AssetState::Loaded ||
            !m_footstepClipHandle.Get()) {
            MYE_LOG_ERROR("CharacterDemo", "footstep.wav load failed");
        }

        // 스크립트(.lua) — 소스는 원문 임베드. 핫 리로드는 재임포트로.
        m_scriptHandle = m_assets->LoadSync<script::ScriptAsset>("assets://scripts/player.lua");
        if (m_scriptHandle.State() != asset::AssetState::Loaded || !m_scriptHandle.Get()) {
            Fatal("player.lua load failed", -15);
            return false;
        }
        return true;
    }

    // 발소리 AudioCue(비소유 clip 포인터 — 핸들이 수명 유지).
    void BuildAudioCue() {
        m_footstepCue.clips.clear();
        if (m_footstepClipHandle.Get())
            m_footstepCue.clips.push_back(m_footstepClipHandle.Get());
        m_footstepCue.selection = audio::CueSelect::Random;
        m_footstepCue.bus = audio::BusId::SFX;
        m_footstepCue.volumeMin = 0.9f; m_footstepCue.volumeMax = 1.0f;
        m_footstepCue.pitchMin = 0.95f; m_footstepCue.pitchMax = 1.05f;
        m_footstepCue.maxVoices = 8;
    }

    // 8방향 walk/idle 클립을 코드로 결정적 생성(M3-A 데이터 타입 소비).
    //   시트 프레임 레이아웃(SliceGrid, row-major): row=방향(Dir8 0..7), col=프레임(0..3).
    //   walk_<dir>: 프레임 0,1,2,3 순환. footstep 마커는 접촉 프레임(slot 1,3)에.
    //   idle_<dir>: 프레임 0 고정(단일).
    void BuildAnimData(Vec2i sheetSize) {
        asset::SpriteSheetImportSettings s;
        s.frameSize = {kCellW, kCellH};
        m_sheet.frames = asset::SpriteSheetImporter::SliceGrid(sheetSize, s);
        m_sheet.texture.guid = m_guidSheet;
        // 발밑 피벗(하단 중앙): (12, 32).
        for (auto& f : m_sheet.frames) { f.pivot = {12.0f, 32.0f}; f.pivotInPixels = true; }

        m_walkClips.resize(kRows);
        m_idleClips.resize(kRows);
        for (int dir = 0; dir < kRows; ++dir) {
            const uint32_t base = static_cast<uint32_t>(dir * kCols);
            // walk: 4프레임 순환, 각 0.12초.
            asset::AnimationClipData& wc = m_walkClips[static_cast<size_t>(dir)];
            wc.name = std::string("walk_") + anim::Dir8Suffix(static_cast<Dir8>(dir));
            wc.loop = true;
            wc.direction = asset::AnimationClipData::Direction::Forward;
            for (int c = 0; c < kCols; ++c) {
                wc.frameIndices.push_back(base + static_cast<uint32_t>(c));
                wc.frameDurations.push_back(0.12f);
            }
            // 접촉 프레임(slot 1·3)에 footstep 마커(stringArg=큐 이름).
            wc.events.push_back(asset::AnimEventMarker{1, "footstep", "cue_footstep", 0.0f});
            wc.events.push_back(asset::AnimEventMarker{3, "footstep", "cue_footstep", 0.0f});

            // idle: 프레임 0 고정(단일 프레임 클립).
            asset::AnimationClipData& ic = m_idleClips[static_cast<size_t>(dir)];
            ic.name = std::string("idle_") + anim::Dir8Suffix(static_cast<Dir8>(dir));
            ic.loop = true;
            ic.frameIndices.push_back(base);
            ic.frameDurations.push_back(0.2f);
        }

        // 상태머신: state 0 = idle(directional), state 1 = walk(directional).
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
        // idle→walk : isMoving true. walk→idle : isMoving false. keepPhase 로 자연 전환.
        { anim::AnimTransition t; t.from = 0; t.to = 1;
          t.conditions.push_back({"isMoving", anim::CmpOp::IsTrue, 0}); m_machine.transitions.push_back(t); }
        { anim::AnimTransition t; t.from = 1; t.to = 0;
          t.conditions.push_back({"isMoving", anim::CmpOp::IsFalse, 0}); m_machine.transitions.push_back(t); }
    }

    void BuildWorldAndScripts() {
        m_world = std::make_unique<ecs::World>();
        m_world->SetEventBus(m_bus);
        m_scheduler = std::make_unique<scene::SystemScheduler>(*m_world, m_bus);

        // 애니 데이터 생성(시트 크기 = cols*cellW, rows*cellH).
        BuildAnimData(Vec2i{kCols * kCellW, kRows * kCellH});

        // ---- 배경 격자(스프라이트 타일) ----
        for (int gy = -6; gy <= 6; ++gy)
            for (int gx = -9; gx <= 9; ++gx) {
                ecs::Entity e = m_world->Create();
                auto& lt = m_world->Add<scene::LocalTransform>(e);
                lt.position = {static_cast<float>(gx), static_cast<float>(gy), 0.0f};
                m_world->Add<scene::WorldTransform>(e);
                auto& sr = m_world->Add<scene::SpriteRenderer>(e);
                sr.sprite.guid = m_guidGrid;
                sr.pivotPx = {24.0f, 24.0f};
                sr.sort.sortLayer = render::kSortLayerGround;
                sr.sort.orderInLayer = render::kOrderTileGround;
            }

        // ---- 플레이어 엔티티: SpriteAnimator + ScriptComponent(player.lua) ----
        m_player = m_world->Create();
        {
            auto& lt = m_world->Add<scene::LocalTransform>(m_player);
            lt.position = {0.0f, 0.0f, 0.0f};
            m_world->Add<scene::WorldTransform>(m_player);
            auto& sr = m_world->Add<scene::SpriteRenderer>(m_player);
            sr.sprite.guid = m_guidSheet;
            sr.pivotPx = {12.0f, 32.0f};
            sr.sort.sortLayer = render::kSortLayerGround;
            sr.sort.orderInLayer = render::kOrderCharacter;

            auto& an = m_world->Add<anim::SpriteAnimator>(m_player);
            an.machine = &m_machine;
            an.sheet = &m_sheet;
            an.facing = Dir8::Down;
            an.SetBool("isMoving", false);
        }

        // 애니메이션 시스템 등록(Update 페이즈).
        anim::RegisterAnimationSystem(*m_scheduler);
        // RenderExtract 시스템 등록.
        scene::RegisterRenderExtractSystem(*m_scheduler, m_proxies);

        // ---- 스크립트 런타임 배선 ----
        m_runtime = std::make_unique<script::ScriptRuntime>();
        m_runtime->Initialize(script::StdLibPolicy{}, m_bus, m_assets.get());

        // 바인딩 모듈(Math·ECS·Input·Audio·Event). Initialize 이후 등록.
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
        // 각 모듈은 AddBindingModule(초기화 후)이 이미 1회 등록함 — ReapplyBindings를
        // 또 부르면 sol2 usertype 이중 등록으로 메타테이블이 메서드 없이 재생성된다.

        m_scriptSystem = std::make_unique<script::ScriptSystem>(
            *m_runtime, *m_world, m_bus, m_assets.get());
        m_scriptSystem->RegisterComponent();

        // 플레이어에 스크립트 부착.
        auto& sc = m_world->Add<script::ScriptComponent>(m_player);
        sc.script = m_scriptHandle;

        // ---- 발소리 애니 이벤트 → AudioCue (월드버스 구독; 결정적 오디오 경로) ----
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

        // ---- ScriptErrorEvent 구독(오버레이·격리 검증) ----
        m_errorSub = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<script::ScriptErrorEvent>([this](const script::ScriptErrorEvent& e) {
                ++m_scriptErrorCount;
                m_lastError = "[" + e.callback + "] " + e.file + ":" +
                              std::to_string(e.line) + " " + e.message;
                MYE_LOG_WARN("CharacterDemo", "ScriptError: {}", m_lastError);
                return false;
            })};

        // 에러 시나리오: 별도 엔티티에 일부러 터지는 스크립트를 인메모리로 설치.
        if (m_control->scenario == Scenario::Error)
            InstallErrorEntity();
    }

    // 인메모리 에러 스크립트(에셋 파이프라인 우회) — on_update 에서 nil 인덱싱.
    void InstallErrorEntity() {
        m_errorEntity = m_world->Create();
        m_world->Add<scene::LocalTransform>(m_errorEntity).position = {-3.0f, 0.0f, 0.0f};
        m_world->Add<scene::WorldTransform>(m_errorEntity);
        m_world->Add<script::ScriptComponent>(m_errorEntity);
        const char* badSrc = R"LUA(
            local C = {}
            function C:on_update(dt) local x = nil; return x.field end
            return C
        )LUA";
        script::ScriptComponent* sc = m_world->TryGet<script::ScriptComponent>(m_errorEntity);
        if (!sc) return;
        auto cls = script::LoadClass(*m_runtime, badSrc, "error.lua");
        if (!cls) return;
        sc->classTable = cls.Value();
        sc->instance = script::MakeInstance(*m_runtime, sc->classTable, m_errorEntity,
                                            sc->properties.values);
        sc->callbacks = script::ScanCallbacks(sc->classTable);
        sc->started = false;
        sc->hasError = false;
    }

    // 강제 이동 방향 주입(_demo_move 전역). Lua on_update 가 소비. nil 세팅 = 입력 사용.
    void SetForcedMove(std::optional<Vec2> dir) {
        if (!m_runtime || !m_runtime->IsInitialized()) return;
        sol::state& lua = m_runtime->State();
        if (dir) lua["_demo_move"] = *dir;
        else     lua["_demo_move"] = sol::nil;
    }

    void ApplyScenario() {
        switch (m_control->scenario) {
            case Scenario::WalkDir:
            case Scenario::EightWay:
            case Scenario::Footstep:
                SetForcedMove(anim::Dir8Vector(m_control->walkDir));
                m_camFixed = true;
                break;
            case Scenario::HotReload:
                // 정지 상태로 시작; 지정 프레임에 소스 변경.
                SetForcedMove(std::nullopt);
                m_camFixed = true;
                break;
            case Scenario::Error:
                SetForcedMove(std::nullopt);
                m_camFixed = true;
                break;
            case Scenario::Free:
            default:
                m_camFixed = false;
                break;
        }
        m_camera.SetPosition({0.0f, 0.0f});
        scene::UpdateWorldTransforms(*m_world);
    }

    // 실행 중 player.lua 를 v2 로 바꿔 재임포트 + AssetReloadedEvent 발행(핫 리로드 트리거).
    void DoHotReload() {
        namespace fs = std::filesystem;
        const fs::path scriptPath = fs::path(m_assetsDir) / "scripts" / "player.lua";
        // v2: 속도를 8.0 으로 바꾸고 on_hot_reload 가 마킹. self.state.speed 는 생존하지만
        //   on_hot_reload 가 명시적으로 speed 를 8.0 으로 올려 "즉시 반영"을 관찰 가능하게 한다.
        const char* v2 = R"LUA(
local Player = {}
function Player:on_init()
    self.state.speed = self.state.speed or 4.0
    self.state.steps = self.state.steps or 0
    self.state.inits = (self.state.inits or 0) + 1
end
function Player:on_update(dt)
    local e = mye.world.entity_from_packed(self.entity)
    if not e:is_valid() then return end
    local dx, dy = 0.0, 0.0
    if _demo_move ~= nil then dx = _demo_move.x; dy = _demo_move.y end
    local moving = (dx ~= 0.0) or (dy ~= 0.0)
    if moving then
        local len = math.sqrt(dx*dx + dy*dy); dx = dx/len; dy = dy/len
        local p = e:get_position()
        e:set_position(mye.Vec2(p.x + dx*self.state.speed*dt, p.y + dy*self.state.speed*dt))
        e:face_move(mye.Vec2(dx, dy))
    end
    e:set_bool("isMoving", moving)
    e:set_float("speed", moving and self.state.speed or 0.0)
end
function Player:on_hot_reload()
    self.state.reloaded = true
    self.state.speed = 8.0   -- 변경이 즉시 반영됨을 관찰 가능하게 (기존 self.state 는 생존)
    self.state.reloadCount = (self.state.reloadCount or 0) + 1
end
return Player
)LUA";
        {
            std::ofstream f(scriptPath, std::ios::binary | std::ios::trunc);
            f.write(v2, static_cast<std::streamsize>(std::strlen(v2)));
        }
        auto rr = m_assets->ReimportPath("assets://scripts/player.lua");
        if (rr.guid.IsValid()) {
            // AssetReloadedEvent 발행 → ScriptSystem 구독 핸들러가 HotReload 실행.
            asset::AssetReloadedEvent ev;
            ev.guid = rr.guid;
            ev.type = script::ScriptAsset::kAssetTypeId;
            m_bus->Publish(ev);
            m_hotReloadFired = true;
            MYE_LOG_INFO("CharacterDemo", "hot reload fired (guid valid, swapped={})", rr.swapped);
        } else {
            MYE_LOG_ERROR("CharacterDemo", "hot reload reimport failed");
        }
    }

    // 원본 player.lua 소스 복원(핫 리로드 시나리오가 파일을 덮어쓰므로 종료 시 되돌림).
    void RestoreScriptSource() {
        if (m_originalScriptSource.empty()) return;
        namespace fs = std::filesystem;
        const fs::path scriptPath = fs::path(m_assetsDir) / "scripts" / "player.lua";
        std::ofstream f(scriptPath, std::ios::binary | std::ios::trunc);
        f.write(m_originalScriptSource.data(),
                static_cast<std::streamsize>(m_originalScriptSource.size()));
    }

    void Update(const mye::TimeStep& step) {
        if (m_input && m_input->WasPressed(mye::KeyCode::Escape))
            m_control->app->RequestExit(0);

        const float dt = static_cast<float>(step.deltaSeconds);

        // 원본 소스 1회 백업(핫 리로드 복원용).
        if (m_originalScriptSource.empty() && m_scriptHandle.Get())
            m_originalScriptSource = m_scriptHandle.Get()->source;

        // 스크립트 인스턴스화(첫 프레임) — 스케줄러 대신 직접 구동해 순서를 명확히 한다.
        m_scriptSystem->EnsureInstances();
        // 스크립트 on_update/on_late_update + 코루틴 tick.
        m_scriptSystem->Update(dt);
        // 스크립트가 기록한 지연 spawn/destroy 반영.
        m_ecsBinding->FlushDeferred();

        // WorldTransform 갱신 후 애니메이션 시스템(전이·샘플·footstep 이벤트 발행).
        scene::UpdateWorldTransforms(*m_world);
        anim::RunAnimationSystem(*m_world, dt);

        // 오디오 프레임 갱신(오프라인).
        if (m_audio) m_audio->Update(dt);

        // 핫 리로드 시나리오: 지정 프레임에 소스 변경.
        if (m_control->scenario == Scenario::HotReload && !m_hotReloadFired &&
            m_frameCount >= kHotReloadFrame) {
            RecordPreReloadState();
            DoHotReload();
        }
    }

    void RecordPreReloadState() {
        // 리로드 전 self.state 를 기록(생존 검증용).
        script::ScriptComponent* sc = m_world->TryGet<script::ScriptComponent>(m_player);
        if (!sc || !sc->instance.valid()) return;
        sol::table inst = sc->instance;
        sol::table state = inst["state"];
        // 리로드 전에 임의 값을 남겨 생존을 관찰.
        state["marker"] = 12345;
        m_preReloadSteps = state["steps"].valid() ? int(state["steps"]) : 0;
        m_preReloadInits = state["inits"].valid() ? int(state["inits"]) : 0;
    }

    Dir8 CurrentFacing() const {
        const auto* an = m_world->TryGet<anim::SpriteAnimator>(m_player);
        return an ? an->facing : Dir8::Down;
    }
    const char* CurrentStateName() const {
        const auto* an = m_world->TryGet<anim::SpriteAnimator>(m_player);
        if (!an || !an->machine || an->currentState < 0 ||
            an->currentState >= static_cast<int>(an->machine->states.size()))
            return "?";
        return an->machine->states[static_cast<size_t>(an->currentState)].name.c_str();
    }
    uint32_t CurrentFrameIndex() const {
        const auto* an = m_world->TryGet<anim::SpriteAnimator>(m_player);
        return an ? an->currentFrameIndex : 0;
    }

    void Render(const mye::TimeStep& /*step*/) {
        if (!m_device || !m_swapChain || !m_rt.IsInitialized()) return;

        if (!m_camFixed) {
            const auto* lt = m_world->TryGet<scene::LocalTransform>(m_player);
            if (lt) m_camera.SetPosition({lt->position.x, lt->position.y});
        }

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

        const bool drawUi = m_debugUi.IsInitialized() && !m_control->dumpEnabled;
        if (drawUi) {
            m_debugUi.BeginFrame();
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("CharacterDemo (M3)");
            ImGui::Text("FPS: %.1f", m_fps);
            ImGui::Text("State: %s  Facing: %d (%s)", CurrentStateName(),
                        static_cast<int>(CurrentFacing()),
                        anim::Dir8Suffix(CurrentFacing()));
            ImGui::Text("Frame idx: %u  Footsteps: %llu", CurrentFrameIndex(),
                        static_cast<unsigned long long>(m_footstepCount));
            ImGui::Separator();
            ImGui::Text("Script errors: %llu",
                        static_cast<unsigned long long>(m_scriptErrorCount));
            if (!m_lastError.empty())
                ImGui::TextWrapped("Last: %s", m_lastError.c_str());
            ImGui::Separator();
            ImGui::TextWrapped("WASD move. Live-edit assets/scripts/player.lua to hot-reload.");
            ImGui::End();

            rhi::RenderPassColorAttachment uiColor{};
            uiColor.texture = backbuffer;
            uiColor.loadOp = rhi::LoadOp::Load;
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
            EmitVerifySummary();
            RestoreScriptSource();
            m_control->app->RequestExit(0);
        }
    }

    // 오프라인 믹서 출력의 peak 절대값(발소리 non-zero 검증).
    float OfflineAudioPeak() {
        if (!m_audio) return 0.0f;
        std::vector<float> buf(4096 * 2, 0.0f);   // stereo
        m_audio->RenderOffline(buf.data(), 4096);
        float peak = 0.0f;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        return peak;
    }

    void EmitVerifySummary() {
        // 스크립트 self.state 조회(핫 리로드 검증).
        int steps = 0, reloadCount = 0; bool reloaded = false; double speed = 0.0; int marker = 0;
        if (auto* sc = m_world->TryGet<script::ScriptComponent>(m_player); sc && sc->instance.valid()) {
            sol::table inst = sc->instance;
            sol::table state = inst["state"];
            if (state.valid()) {
                if (state["steps"].valid())       steps = int(state["steps"]);
                if (state["reloadCount"].valid()) reloadCount = int(state["reloadCount"]);
                if (state["reloaded"].valid())    reloaded = bool(state["reloaded"]);
                if (state["speed"].valid())       speed = double(state["speed"]);
                if (state["marker"].valid())      marker = int(state["marker"]);
            }
        }
        const float audioPeak = OfflineAudioPeak();
        bool errorEntityStopped = true;
        if (m_control->scenario == Scenario::Error) {
            auto* sc = m_world->TryGet<script::ScriptComponent>(m_errorEntity);
            errorEntityStopped = sc && sc->hasError;
        }
        MYE_LOG_INFO("CharacterDemo",
            "M3-VERIFY: scenario={} frames={} facing={} state={} frameIdx={} footsteps={} "
            "luaSteps={} audioPeak={:.5f} scriptErrors={} errorStopped={} "
            "hotReloadFired={} reloaded={} reloadCount={} stateMarker={} preSteps={} preInits={} speed={:.2f}",
            static_cast<int>(m_control->scenario), m_frameCount,
            static_cast<int>(CurrentFacing()), CurrentStateName(), CurrentFrameIndex(),
            m_footstepCount, steps, audioPeak, m_scriptErrorCount,
            errorEntityStopped ? 1 : 0, m_hotReloadFired ? 1 : 0, reloaded ? 1 : 0,
            reloadCount, marker, m_preReloadSteps, m_preReloadInits, speed);
    }

    void HandleDumps(rhi::TextureHandle backbuffer) {
        if (m_control->dumpEnabled && !m_dumped) {
            const bool last = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
            if (last || (!m_control->frameLimit && m_frameCount >= 5)) {
                auto cap = rhi::CaptureBackbuffer(*m_device, backbuffer, m_control->dumpPath);
                if (cap) MYE_LOG_INFO("CharacterDemo", "dump -> '{}' (frame {})",
                                      m_control->dumpPath, m_frameCount);
                else MYE_LOG_ERROR("CharacterDemo", "dump failed: {}", cap.GetError().message);
                m_dumped = true;
            }
        }
    }

    static constexpr uint64_t kHotReloadFrame = 20;

    RunControl* m_control = nullptr;
    mye::EngineContext* m_ctx = nullptr;
    mye::EventBus* m_bus = nullptr;
    mye::InputState* m_input = nullptr;

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;
    std::string m_assetsDir;

    std::vector<asset::AssetHandle<asset::Texture>> m_texHandles;
    asset::AssetHandle<asset::AudioClip>            m_footstepClipHandle;
    asset::AssetHandle<script::ScriptAsset>         m_scriptHandle;
    asset::AssetHandle<script::ScriptAsset>         m_errorScriptHandle;
    AssetResolvers m_resolvers;

    asset::AssetGuid m_guidSheet, m_guidGrid, m_guidWhite;

    std::unique_ptr<ecs::World>             m_world;
    std::unique_ptr<scene::SystemScheduler> m_scheduler;
    scene::RenderProxyList m_proxies;
    ecs::Entity m_player, m_errorEntity;

    // 애니 데이터(수명: 모듈).
    asset::SpriteSheet m_sheet;
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
    std::string m_originalScriptSource;
    bool m_hotReloadFired = false;
    int m_preReloadSteps = 0, m_preReloadInits = 0;

    render::PixelPerfectTarget m_rt;
    render::HybridRenderer     m_hybrid;
    render::Camera2D           m_camera;
    mye::imgui::DebugUi        m_debugUi;
    mye::ScopedSubscription    m_onResized;

    bool     m_camFixed = false;
    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
    double   m_secondsAccum = 0.0;
    uint64_t m_lastFrameCount = 0;
    float    m_fps = 0.0f;
};

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
Scenario ParseScenario(const std::string& s, Dir8& outDir) {
    if (s == "8way")      return Scenario::EightWay;
    if (s == "footstep")  return Scenario::Footstep;
    if (s == "hotreload") return Scenario::HotReload;
    if (s == "error")     return Scenario::Error;
    // walk_<dir>
    for (int d = 0; d < 8; ++d) {
        std::string name = std::string("walk_") + anim::Dir8Suffix(static_cast<Dir8>(d));
        if (s == name) { outDir = static_cast<Dir8>(d); return Scenario::WalkDir; }
    }
    if (s == "walk") { outDir = Dir8::Down; return Scenario::WalkDir; }
    return Scenario::Free;
}

class CharacterDemoApp final : public mye::Application {
public:
    explicit CharacterDemoApp(const mye::LaunchArgs& args) {
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
                m_control.scenario = ParseScenario(a[++i], m_control.walkDir);
                m_control.scenarioSet = true;
            }
        }
    }

    void OnConfigure(mye::ConfigSystem& /*config*/) override {}
    void OnRegisterModules(mye::ModuleRegistry& modules) override {
        modules.Register(std::make_unique<CharacterDemoModule>(&m_control));
    }
    void OnStart(mye::EngineContext& /*ctx*/) override {
        MYE_LOG_INFO("CharacterDemo", "character_demo start (frames={} scenario={})",
                     m_control.frameLimit ? static_cast<long long>(m_control.maxFrames) : -1LL,
                     static_cast<int>(m_control.scenario));
    }

private:
    RunControl m_control;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) { return new CharacterDemoApp(args); }
} // namespace mye

int main(int /*argc*/, char** /*argv*/) {
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    }
    launch.mainWindow.title = "MyEngine — character_demo (M3)";
    launch.mainWindow.clientSize = {960, 540};
    launch.mainWindow.resizable = true;
    return mye::GuardedMain(launch);
}
