// MyGame — 스탠드얼론 게임 런타임 (docs/mmorpg, M7/M9)
//
// 에디터 없이 프로젝트(assets + .scene)를 로드해 실행하는 게임 실행 파일. 엔진 모듈 스택
// (SceneModule + GameRuntimeModule) 위에서 RHI 디바이스·스왑체인·픽셀퍼펙트 RT·하이브리드
// 렌더러·에셋·오디오를 조립하고, 데이터드리븐 씬을 mye::scene::SceneSerializer 로 로드해 매
// 프레임 렌더한다. village_demo(하드코딩 C++ 데모)를 데이터드리븐으로 일반화한 것.
//
// 타이틀 화면(M9+): 게임 시작 시 픽셀아트 타이틀(기차 풍경·패럴랙스 스크롤·증기 연기) 위에
//   한글 메뉴(게임 시작/환경설정)를 띄운다. 환경설정에서 캐릭터(남자/여자 모험가 — 사용자가
//   넣어준 아트에서 추출) 선택과 BGM/SFX 볼륨(실제 오디오 버스)을 조정한다.
//
// --connect 네트워크 모드(M9): 권위 서버(MyServer)에 접속해 서버권위 이동 + 클라 예측/재조정 +
// 원격 플레이어 스냅샷 보간 렌더를 수행한다. 같은 존의 다른 접속자(봇 포함)가 원격 도트로 보인다.
//
// CLI:
//   --project <dir>        프로젝트 루트(assets/ 를 포함). 기본 samples/game_sample.
//   --scene <vpath|path>   로드할 씬(예: assets://scenes/sample.scene). 기본 위 프로젝트의 sample.
//   --make-sample          텍스처 하나로 데모 씬을 생성·저장하고 종료(자체 검증 시드).
//   --auto-start           타이틀 화면 거치지 않고 곧바로 플레이(E2E/CI 종래 경로).
//   --settings             부팅 시 환경설정 탭부터 열기(화면 검증용).
//   --connect <ip:port>    네트워크 모드 — 권위 서버에 접속(예: 127.0.0.1:27015).
//   --account <user>       접속 계정(--connect 시 필요. 서버가 --register 로 등록한 계정).
//   --password <pass>      계정 비밀번호.
//   --frames N             N 프레임 후 종료(자동 검증).
//   --dump <path.bmp>      마지막 프레임(내부 RT)을 BMP 로 덤프.
//   --headless             창 없이(오프스크린) — 실GPU present 회피.
#include "mye/core/App.h"
#include "mye/core/Module.h"
#include "mye/core/Log.h"
#include "mye/core/Math.h"
#include "mye/core/Window.h"
#include "mye/core/Input.h"
#include "mye/core/Base.h"
#include "mye/core/Config.h"

#include "mye/rhi/Rhi.h"

#include "mye/asset/FileSystem.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/Importer.h"
#include "mye/asset/MeshImporter.h"
#include "mye/asset/AudioClip.h"
#include "mye/asset/AudioImporter.h"
#include "mye/asset/Texture.h"

#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/Camera2D.h"
#include "mye/render/SpriteBatch.h"

#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioModule.h"
#include "mye/audio/AudioTypes.h"

#include "mye/text/FontFace.h"
#include "mye/text/GlyphAtlas.h"
#include "mye/text/TextRenderer.h"
#include "mye/text/TextTypes.h"

#include "mye/net/NetClient.h"
#include "mye/net/SnapshotInterpolator.h"
#include "mye/net/UdpSocket.h"

#include "mye/scene/SceneModule.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"
#include "mye/anim/AnimationSystem.h"
#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/View.h"

#include <Windows.h>
#include <shellapi.h>

// windows.h 의 DrawText 매크로가 text::TextRenderer::DrawText 를 오염시킨다. 해제.
#ifdef DrawText
#undef DrawText
#endif

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace mye;

namespace {

// vpath → 결정론적 GUID(=vpath 해시). .meta 없이도 씬의 AssetRef 와 런타임 로드가 같은 GUID로
// 만나게 한다(런타임 자산 해석의 최소 경로). 스킴 접두 포함 vpath 전체를 해시.
asset::AssetGuid DeterministicGuid(std::string_view vpath) {
    const uint64_t hi = HashFnv1a64(vpath);
    std::string lo = std::string(vpath) + "#lo";
    return asset::AssetGuid{hi, HashFnv1a64(lo)};
}

// GUID → 로드된 에셋 포인터 해석기(하이브리드 렌더러 콜백).
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

struct GameCli {
    std::string projectDir = "samples/mmo_demo";   // 기본 프로젝트 — 타이틀 아트·모험가 보유
    std::string scenePath;          // 비면 <project>/assets/scenes/sample.scene
    bool        makeSample = false;
    bool        makeHunt = false;   // 3직업 파티 vs 슬라임 사냥 대치 씬 생성
    bool        autoStart = false;  // 타이틀 건너뛰기(E2E/CI)
    bool        openSettings = false; // 부팅 시 환경설정 탭(검증용)
    bool        headless = false;
    Color       ambient{1.0f, 1.0f, 1.0f, 1.0f};   // 전역 앰비언트(데이나이트 틴트) — 스프라이트 tint에 곱
    bool        frameLimit = false;
    uint64_t    maxFrames = 0;
    bool        dumpEnabled = false;
    std::string dumpPath;
    // 네트워크 모드(M9) — --connect 가 있으면 권위 서버에 접속.
    std::string connectAddr;        // "ip:port"
    std::string account;
    std::string password;
};

// 캐릭터(사용자 제공 모험가 아트 — tools/extract_characters.mjs 로 추출).
constexpr std::string_view kCharMale   = "assets://sprites/adventurer_m.png";
constexpr std::string_view kCharFemale = "assets://sprites/adventurer_f.png";

// 타이틀 아트(tools/gen_title_art.mjs 로 생성).
constexpr std::string_view kTitleBg    = "assets://sprites/title_bg.png";     // 1920×540 타일링
constexpr std::string_view kTitleFg    = "assets://sprites/title_fg.png";     // 960×96 전경 타일링
constexpr std::string_view kTitleTrain = "assets://sprites/title_train.png";  // 440×176, 피벗=바퀴 하단 중앙
constexpr std::string_view kTitleSmoke = "assets://sprites/smoke.png";        // 32×32 연기 펍
constexpr float kRailWorldY = -4.0417f;   // 레일 상면(배경 464px)의 월드 Y(PPU 48, RT 540)

// 게임 진행 상태 — 타이틀 ↔ 환경설정 ↔ 플레이.
enum class GameState { Title, Settings, Playing };

// -----------------------------------------------------------------------------
// GameRuntimeModule — 조립 + 씬 로드 + 프레임 루프(렌더).
// -----------------------------------------------------------------------------
class GameRuntimeModule final : public IModule {
public:
    const char* GetName() const override { return "GameRuntimeModule"; }
    std::span<const char* const> GetDependencies() const override {
        static const char* deps[] = {"SceneModule", "AudioModule"};
        return deps;
    }

    void SetCli(const GameCli& cli, std::function<void()> onExit) {
        m_cli = cli;
        m_onExit = std::move(onExit);
    }

    void OnInitialize(EngineContext& ctx) override {
        m_ctx = &ctx;
        m_scene = ctx.GetService<scene::SceneModule>();
        m_input = ctx.GetService<InputState>();

        auto dev = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!dev) { MYE_LOG_ERROR("Game", "CreateDevice 실패: {}", dev.GetError().message); return; }
        m_device = std::move(dev).Value();

        // 창이 있으면 스왑체인.
        IWindow* window = ctx.GetServiceRaw(kMainWindowServiceId) ? &ctx.MainWindow() : nullptr;
        m_hasWindow = (window != nullptr) && !m_cli.headless;
        if (m_hasWindow) {
            rhi::SwapChainDesc sc{};
            sc.width = 0; sc.height = 0;
            sc.format = rhi::Format::BGRA8UnormSrgb;
            m_swapChain = m_device->CreateSwapChain(window->GetNativeHandle(), sc);
            if (!m_swapChain) { MYE_LOG_ERROR("Game", "CreateSwapChain 실패"); m_hasWindow = false; }
        }

        // VFS + AssetManager.
        m_assetsDir = (fs::path(m_cli.projectDir) / "assets").string();
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(m_assetsDir), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::MeshImporter>());
        m_assets->RegisterImporter(std::make_unique<asset::AudioImporter>());

        m_audio = static_cast<audio::AudioEngine*>(ctx.GetServiceRaw(audio::AudioModule::kServiceId));

        // 픽셀퍼펙트 RT + 하이브리드 렌더러.
        render::PixelPerfectDesc ppd{};
        ppd.useDepth = true;
        ppd.backbufferFormat = m_swapChain ? m_swapChain->GetFormat() : rhi::Format::BGRA8UnormSrgb;
        m_rt.Init(*m_device, ppd);

        render::HybridRendererDesc hrd{};
        hrd.colorFormat = m_rt.ColorFormat();
        hrd.depthFormat = rhi::Format::D24UnormS8Uint;
        hrd.alphaCutoff = 0.5f;
        m_hybrid.Init(*m_device, hrd);
        m_hybrid.SetTextureResolver(&AssetResolvers::ResolveTexture, &m_resolvers);
        m_hybrid.SetMeshResolver(&AssetResolvers::ResolveMesh, &m_resolvers);
        m_hybrid.SetDirectionalLight(Vec3{-0.3f, -0.6f, 0.5f}, Color::White(), 0.45f);

        render::Camera2DDesc cam{};
        cam.position = {0.0f, 0.0f};
        cam.pixelSnap = true;
        m_camera = render::Camera2D(cam);

        // 씬 컴포넌트 등록(로드 시 AddDynamic 이 풀을 필요로 함).
        ecs::World& world = m_scene->World();
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::LocalTransform>("LocalTransform"));
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::WorldTransform>("WorldTransform"));
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::Parent>("Parent"));
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::Children>("Children"));
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::SpriteRenderer>("SpriteRenderer"));
        world.RegisterComponent(ecs::MakeComponentTypeDesc<scene::FloorLevel>("FloorLevel"));

        InitTextStack();
        LoadBgm();

        m_ready = m_rt.IsInitialized() && m_hybrid.IsInitialized();
    }

    void OnPostInitialize(EngineContext& ctx) override {
        if (!m_ready) { RequestExit(); return; }
        ctx.Config().RegisterSection("game", ConfigScope::User);
        m_gender = static_cast<int>(std::clamp(ctx.Config().GetInt("game.character", 0), int64_t{0}, int64_t{1}));
        m_bgmVol = m_audio->GetBusVolume(audio::BusId::BGM);
        m_sfxVol = m_audio->GetBusVolume(audio::BusId::SFX);

        PreloadTextures();
        m_whiteTex = TextureHandleOf("assets://sprites/white.png");   // 오버레이 패널용

        if (m_cli.makeSample) {
            MakeSampleScene();
            RequestExit();
            return;
        }

        if (m_cli.makeHunt) {
            MakeHuntScene();
            RequestExit();
            return;
        }

        if (m_cli.autoStart) {
            StartGame();   // 타이틀 없이 곧바로 플레이(E2E/CI 종래 경로)
        } else {
            EnterTitle();
            if (m_cli.openSettings) m_state = GameState::Settings;
        }

        ctx.Modules().AddTick(this, UpdatePhase::Update,
                              [this](const TimeStep& s) { Update(s); }, 0);
        ctx.Modules().AddTick(this, UpdatePhase::FixedUpdate,
                              [this](const TimeStep& s) { FixedUpdate(s); }, 0);
        ctx.Modules().AddTick(this, UpdatePhase::PreRender,
                              [this](const TimeStep& s) { Render(s); }, 100);
    }

    void OnShutdown(EngineContext&) override {
        SaveSettings();
        if (m_net) m_net->Disconnect();
        m_net.reset();
        m_netSys.reset();
        // AudioModule의 콜백은 아직 살아 있으므로 클립 해제 전에 재생 참조를 끊는다.
        if (m_audio) m_audio->Music().Stop();
        m_texHandles.clear();
        m_bgmClip = {};
        m_spriteBatch.Shutdown();
        m_atlas.Shutdown();
        m_fonts.reset();
        m_koFont = nullptr;
        m_audio = nullptr;
        m_hybrid.Shutdown();
        m_rt.Shutdown();
        m_assets.reset();
        m_vfs.reset();
        m_swapChain.reset();
        m_device.reset();
    }

private:
    void RequestExit() { if (m_onExit) m_onExit(); }

    // assets/ 아래 모든 이미지(.png)를 로드해 DeterministicGuid(vpath) 로 리졸버에 등록.
    void PreloadTextures() {
        std::error_code ec;
        const fs::path base(m_assetsDir);
        if (!fs::is_directory(base, ec)) return;
        for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(::tolower(c));
            if (ext != ".png") continue;
            fs::path rel = fs::relative(it->path(), base, ec);
            if (ec) continue;
            std::string vpath = "assets://" + rel.generic_string();
            auto h = m_assets->LoadSync<asset::Texture>(vpath);
            const asset::Texture* tex = h.Get();
            if (!tex) { MYE_LOG_WARN("Game", "텍스처 로드 실패: {}", vpath); continue; }
            m_resolvers.textures[AssetResolvers::Key(DeterministicGuid(vpath))] = tex;
            m_texHandles.push_back(std::move(h));
            MYE_LOG_INFO("Game", "텍스처 등록: {}", vpath);
        }
    }

    // ---- 한글 텍스트 스택(FontRegistry·GlyphAtlas·TextRenderer — village_demo 패턴) ----
    void InitTextStack() {
        m_fonts = std::make_unique<text::FontRegistry>();
        // 시스템 맑은 고딕(한글) 로드 — 실패해도 게임은 계속(메뉴 텍스트만 비활성).
        wchar_t windowsDir[MAX_PATH]{};
        const UINT length = GetWindowsDirectoryW(windowsDir, MAX_PATH);
        const fs::path fontPath = length > 0 && length < MAX_PATH
            ? fs::path(windowsDir) / "Fonts" / "malgun.ttf" : fs::path{};
        std::ifstream f(fontPath, std::ios::binary | std::ios::ate);
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
        if (!m_koFont) MYE_LOG_WARN("Game", "malgun.ttf 로드 실패 — 메뉴 텍스트 비활성");

        text::GlyphAtlasConfig acfg{};
        m_atlas.Init(*m_device, acfg);
        m_spriteBatch.Init(*m_device, m_rt.ColorFormat(), true);
    }

    void LoadBgm() {
        m_bgmClip = m_assets->LoadSync<asset::AudioClip>("assets://audio/title_bgm.wav");
        if (m_bgmClip.State() != asset::AssetState::Loaded || !m_bgmClip.Get())
            MYE_LOG_WARN("Game", "BGM 로드 실패(무음 계속) — assets://audio/title_bgm.wav");
    }

    void StartBgm() {
        if (m_bgmStarted || !m_audio || !m_bgmClip.Get()) return;
        m_bgmStarted = true;
        m_audio->Music().Play(*m_bgmClip.Get(), 0.8f, 0.55f);
    }

    std::string ResolveScenePath() const {
        if (!m_cli.scenePath.empty()) {
            constexpr std::string_view kScheme = "assets://";
            if (m_cli.scenePath.rfind(std::string(kScheme), 0) == 0)
                return (fs::path(m_assetsDir) / m_cli.scenePath.substr(kScheme.size())).string();
            return m_cli.scenePath;
        }
        return (fs::path(m_assetsDir) / "scenes" / "sample.scene").string();
    }

    void LoadScene() {
        const std::string path = ResolveScenePath();
        scene::SceneSerializer ser;
        auto r = ser.LoadFromFile(m_scene->World(), path);
        if (!r) { MYE_LOG_ERROR("Game", "씬 로드 실패 '{}': {}", path, r.GetError().message); return; }
        MYE_LOG_INFO("Game", "씬 로드: '{}' (루트 {}개)", path, r.Value().size());
        EnsureWorldTransforms();
        FindPlayer();
    }

    // 첫 스프라이트 엔티티를 플레이어로 지정(입력 이동 + 카메라 팔로우 대상). 데모 편의.
    void FindPlayer() {
        ecs::World& world = m_scene->World();
        m_player = ecs::Entity::Null();
        world.Query<scene::LocalTransform, scene::SpriteRenderer>().Each(
            [&](ecs::Entity e, scene::LocalTransform&, scene::SpriteRenderer&) {
                if (m_player.IsNull()) m_player = e;
            });
        if (!m_player.IsNull())
            m_camera.SetPosition(PlayerPos());
    }

    Vec2 PlayerPos() const {
        if (m_player.IsNull()) return {};
        auto* lt = m_scene->World().TryGet<scene::LocalTransform>(m_player);
        return lt ? Vec2{lt->position.x, lt->position.y} : Vec2{};
    }

    // WASD/방향키/게임패드 왼쪽 스틱 → 이동 벡터(+Y 위). 정규화 없이 축 합.
    Vec2 MoveInput() const {
        Vec2 mv{0.0f, 0.0f};
        if (!m_input) return mv;
        if (m_input->IsDown(KeyCode::A) || m_input->IsDown(KeyCode::Left))  mv.x -= 1.0f;
        if (m_input->IsDown(KeyCode::D) || m_input->IsDown(KeyCode::Right)) mv.x += 1.0f;
        if (m_input->IsDown(KeyCode::W) || m_input->IsDown(KeyCode::Up))    mv.y += 1.0f;
        if (m_input->IsDown(KeyCode::S) || m_input->IsDown(KeyCode::Down))  mv.y -= 1.0f;
        const Vec2 stick = m_input->LeftStick(0);
        mv.x += stick.x; mv.y += stick.y;
        // 대각 과속 방지(길이 1로 제한).
        const float len = std::sqrt(mv.x * mv.x + mv.y * mv.y);
        if (len > 1.0f) { mv.x /= len; mv.y /= len; }
        return mv;
    }

    // 씬 로드 엔티티는 파생 컴포넌트 WorldTransform이 없다(직렬화 제외). LocalTransform 보유
    // 엔티티마다 WorldTransform을 붙여야 UpdateWorldTransforms가 월드 행렬을 채운다(렌더 위치).
    void EnsureWorldTransforms() {
        ecs::World& world = m_scene->World();
        std::vector<ecs::Entity> ents;
        world.Query<scene::LocalTransform>().Each(
            [&](ecs::Entity e, scene::LocalTransform&) { ents.push_back(e); });
        for (ecs::Entity e : ents)
            if (!world.TryGet<scene::WorldTransform>(e))
                (void)world.Add<scene::WorldTransform>(e);
    }

    // 데모 씬 생성: hero 텍스처를 참조하는 스프라이트 3×3 격자 → sample.scene 저장.
    void MakeSampleScene() {
        const std::string heroVpath = "assets://sprites/hero.png";
        const asset::AssetGuid heroGuid = DeterministicGuid(heroVpath);

        ecs::World& world = m_scene->World();
        for (int gy = 0; gy < 3; ++gy) {
            for (int gx = 0; gx < 3; ++gx) {
                ecs::Entity e = world.Create();
                auto* lt = static_cast<scene::LocalTransform*>(
                    world.AddDynamic(e, scene::LocalTransform::kComponentTypeId));
                lt->position = Vec3{static_cast<float>(gx - 1) * 3.5f,
                                    static_cast<float>(gy - 1) * 3.5f, 0.0f};
                lt->scale = Vec3{0.5f, 0.5f, 1.0f};   // 256px 텍스처를 절반으로(격자가 겹치지 않게)
                auto* sr = static_cast<scene::SpriteRenderer*>(
                    world.AddDynamic(e, scene::SpriteRenderer::kComponentTypeId));
                sr->sprite.guid = heroGuid;
                sr->pivotPx = Vec2{128.0f, 128.0f};   // 텍스처 중심 피벗
                sr->sort.sortLayer = scene::kSortLayerWorldBase;
            }
        }
        const fs::path out = fs::path(m_assetsDir) / "scenes" / "sample.scene";
        std::error_code ec; fs::create_directories(out.parent_path(), ec);
        scene::SceneSerializer ser;
        auto r = ser.SaveToFile(world, out.string());
        if (r) MYE_LOG_INFO("Game", "샘플 씬 저장: {}", out.string());
        else   MYE_LOG_ERROR("Game", "샘플 씬 저장 실패: {}", r.GetError().message);
    }

    // 스프라이트 엔티티 하나 생성(48x48 도트, 중심 피벗). 낮은 y일수록 앞(2.5D Y소트).
    ecs::Entity SpawnSprite(std::string_view vpath, Vec3 pos, float scale) {
        ecs::World& world = m_scene->World();
        ecs::Entity e = world.Create();
        auto* lt = static_cast<scene::LocalTransform*>(
            world.AddDynamic(e, scene::LocalTransform::kComponentTypeId));
        lt->position = pos;
        lt->scale = Vec3{scale, scale, 1.0f};
        auto* sr = static_cast<scene::SpriteRenderer*>(
            world.AddDynamic(e, scene::SpriteRenderer::kComponentTypeId));
        sr->sprite.guid = DeterministicGuid(vpath);
        sr->pivotPx = Vec2{24.0f, 24.0f};   // 48px 도트의 중심
        sr->sort.sortLayer = scene::kSortLayerWorldBase;
        (void)world.Add<scene::WorldTransform>(e);
        return e;
    }

    // 사냥 대치 씬: 검사(앞라인)·마법사·버퍼가 왼쪽, 슬라임이 오른쪽. 아기자기한 파티 사냥 한 컷.
    void MakeHuntScene() {
        // 파티 — 검사 앞라인(탱), 마법사·버퍼 후열(위/아래로 스태거).
        SpawnSprite("assets://sprites/swordsman.png", Vec3{-2.2f, -1.0f, 0.0f}, 3.0f);
        SpawnSprite("assets://sprites/mage.png",      Vec3{-5.4f,  0.6f, 0.0f}, 2.8f);
        SpawnSprite("assets://sprites/buffer.png",    Vec3{-5.4f, -2.6f, 0.0f}, 2.8f);
        // 몹 — 오른쪽에서 파티와 대치(살짝 크게).
        SpawnSprite("assets://sprites/slime.png",     Vec3{ 4.2f, -1.2f, 0.0f}, 3.6f);

        const fs::path out = fs::path(m_assetsDir) / "scenes" / "hunt.scene";
        std::error_code ec; fs::create_directories(out.parent_path(), ec);
        scene::SceneSerializer ser;
        auto r = ser.SaveToFile(m_scene->World(), out.string());
        if (r) MYE_LOG_INFO("Game", "사냥 씬 저장: {}", out.string());
        else   MYE_LOG_ERROR("Game", "사냥 씬 저장 실패: {}", r.GetError().message);
    }

    // 로드된 텍스처 포인터(없으면 null).
    const asset::Texture* TextureOf(std::string_view vpath) const {
        auto it = m_resolvers.textures.find(AssetResolvers::Key(DeterministicGuid(vpath)));
        return it == m_resolvers.textures.end() ? nullptr : it->second;
    }

    // 엔티티의 스프라이트를 vpath 텍스처로 교체(텍스처 중심 피벗). 캐릭터 교체·미리보기용.
    void SetSpriteTexture(ecs::Entity e, std::string_view vpath) {
        const asset::Texture* tex = TextureOf(vpath);
        if (!tex) return;
        if (auto* sr = m_scene->World().TryGet<scene::SpriteRenderer>(e)) {
            sr->sprite.guid = DeterministicGuid(vpath);
            sr->pivotPx = Vec2{static_cast<float>(tex->width) * 0.5f,
                               static_cast<float>(tex->height) * 0.5f};
        }
    }

    // 이동 상태에 따른 표현 — 좌우 플립(진행 방향) + 도보 바운스(피벗 시프트, 위치 불변).
    void AnimateCharacter(ecs::Entity e, bool moving, float velX, float basePivotY) {
        auto* sr = m_scene->World().TryGet<scene::SpriteRenderer>(e);
        if (!sr) return;
        if (std::fabs(velX) > 0.05f) sr->flipX = (velX < 0.0f);
        const float bob = moving ? (std::fabs(std::sin(m_animTime * 9.0f)) * 2.5f) : 0.0f;
        sr->pivotPx.y = basePivotY - bob;
    }

    // -------------------------------------------------------------------------
    // 타이틀 화면 — 기차 풍경 패럴랙스 + 한글 메뉴 + 환경설정.
    // -------------------------------------------------------------------------
    void EnterTitle() {
        m_state = GameState::Title;
        m_menuCursor = 0;
        m_settingsRow = 0;
        CreateTitleScene();
        m_camera.SetPosition({0.0f, 0.0f});
        StartBgm();
        MYE_LOG_INFO("Game", "타이틀 화면 진입");
    }

    // 타이틀 전용 엔티티(배경 2장·전경 2장·기차·연기 6·캐릭터 1).
    void CreateTitleScene() {
        ecs::World& world = m_scene->World();
        m_titleEnts.clear();

        const auto spawn = [&](std::string_view vpath, Vec3 pos, Vec2 pivotPx, uint16_t order) {
            ecs::Entity e = world.Create();
            auto* lt = static_cast<scene::LocalTransform*>(
                world.AddDynamic(e, scene::LocalTransform::kComponentTypeId));
            lt->position = pos;
            auto* sr = static_cast<scene::SpriteRenderer*>(
                world.AddDynamic(e, scene::SpriteRenderer::kComponentTypeId));
            sr->sprite.guid = DeterministicGuid(vpath);
            sr->pivotPx = pivotPx;
            sr->sort.sortLayer = scene::kSortLayerWorldBase;
            sr->sort.orderInLayer = order;
            (void)world.AddDynamic(e, scene::WorldTransform::kComponentTypeId);
            m_titleEnts.push_back(e);
            return e;
        };

        // 배경(타일링 2장 — 좌루프 스크롤). 피벗=좌상단(오프셋 계산 단순화).
        m_titleBgA = spawn(kTitleBg, Vec3{-40.0f, -5.625f, 0.0f}, Vec2{0.0f, 540.0f}, 0);
        m_titleBgB = spawn(kTitleBg, Vec3{0.0f, -5.625f, 0.0f}, Vec2{0.0f, 540.0f}, 0);
        // 전경(빠른 스크롤 2장) — 화면 하단에 걸친다(상단 침엽만 레일을 살짝 가림).
        m_titleFgA = spawn(kTitleFg, Vec3{-20.0f, -5.625f, 0.0f}, Vec2{0.0f, 96.0f}, 24);
        m_titleFgB = spawn(kTitleFg, Vec3{0.0f, -5.625f, 0.0f},   Vec2{0.0f, 96.0f}, 24);
        // 기차(레일 위 고정, 피벗=바퀴 하단 중앙).
        m_titleTrain = spawn(kTitleTrain, Vec3{1.6f, kRailWorldY, 0.0f}, Vec2{220.0f, 176.0f}, 8);
        // 연기 펍 6(굴뚝에서 순환 재생).
        m_smokeEnts.clear();
        for (int i = 0; i < 6; ++i) {
            ecs::Entity e = spawn(kTitleSmoke, Vec3{0.0f, 0.0f, 0.0f}, Vec2{16.0f, 16.0f}, 16);
            m_smokeEnts.push_back(e);
            m_smokeLife.push_back(static_cast<float>(i) / 6.0f);
        }
        // 선택된 캐릭터 — 선로 옆에 서서 기차를 바라본다.
        m_titleChar = world.Create();
        {
            auto* lt = static_cast<scene::LocalTransform*>(
                world.AddDynamic(m_titleChar, scene::LocalTransform::kComponentTypeId));
            lt->position = Vec3{7.2f, kRailWorldY - 0.35f, 0.0f};   // 기차 오른쪽(메뉴 패널과 분리)
            lt->scale = Vec3{1.25f, 1.25f, 1.0f};
            auto* sr = static_cast<scene::SpriteRenderer*>(
                world.AddDynamic(m_titleChar, scene::SpriteRenderer::kComponentTypeId));
            sr->sprite.guid = DeterministicGuid(ChosenCharVpath());
            const asset::Texture* tex = TextureOf(ChosenCharVpath());
            const float w = tex ? static_cast<float>(tex->width) : 64.0f;
            const float h = tex ? static_cast<float>(tex->height) : 128.0f;
            sr->pivotPx = Vec2{w * 0.5f, h};   // 발밑 피벗
            sr->sort.sortLayer = scene::kSortLayerWorldBase;
            sr->sort.orderInLayer = 20;
            (void)world.AddDynamic(m_titleChar, scene::WorldTransform::kComponentTypeId);
            m_titleEnts.push_back(m_titleChar);
        }
    }

    void DestroyTitleScene() {
        ecs::World& world = m_scene->World();
        for (ecs::Entity e : m_titleEnts) world.Destroy(e);
        m_titleEnts.clear();
        m_smokeEnts.clear();
        m_smokeLife.clear();
        m_titleBgA = m_titleBgB = m_titleFgA = m_titleFgB = m_titleTrain = m_titleChar = ecs::Entity::Null();
    }

    std::string_view ChosenCharVpath() const { return m_gender == 0 ? kCharMale : kCharFemale; }
    std::string_view OtherCharVpath() const  { return m_gender == 0 ? kCharFemale : kCharMale; }

    // 게임 시작 — 타이틀 정리 → 씬 로드 → 로컬/넷 플레이 시작.
    void StartGame() {
        DestroyTitleScene();
        m_state = GameState::Playing;
        LoadScene();
        if (m_cli.connectAddr.empty()) {
            FindPlayer();   // 로컬 모드: 씬의 첫 스프라이트를 조작 대상으로.
            if (!m_player.IsNull() && TextureOf(ChosenCharVpath())) {
                SetSpriteTexture(m_player, ChosenCharVpath());
                MYE_LOG_INFO("Game", "캐릭터 적용(로컬): {}", ChosenCharVpath());
            }
            if (m_player.IsNull() && TextureOf(ChosenCharVpath())) {
                // 씬에 스프라이트가 없어도 선택 캐릭터로 바로 플레이(프로젝트 최소 동작 보장).
                m_player = SpawnSprite(ChosenCharVpath(), Vec3{0.0f, 0.0f, 0.0f}, 1.0f);
            }
            if (const asset::Texture* tex = TextureOf(ChosenCharVpath())) {
                m_playerPivotY = static_cast<float>(tex->height) * 0.5f;
                if (auto* sr = m_scene->World().TryGet<scene::SpriteRenderer>(m_player))
                    sr->pivotPx = Vec2{static_cast<float>(tex->width) * 0.5f, m_playerPivotY};
            }
        } else if (SetupNetworking()) {
            SetupNetEntities();
        }
        StartBgm();
    }

    // 플레이 → 타이틀 복귀(ESC). 넷은 정상 접속 해제 후 월드 정리.
    void BackToTitle() {
        if (m_net) {
            m_net->Disconnect();
            m_net.reset();
            m_netSys.reset();
            m_interp = net::SnapshotInterpolator{};
            m_remoteEnts.clear();
            m_remotePrevX.clear();
            m_remoteSnaps.clear();
            m_inputSeq = 1;
            m_lastPushedTick = 0;
            MYE_LOG_INFO("Game", "넷 접속 해제 — 타이틀로 복귀");
        }
        auto& world = m_scene->World();
        std::vector<ecs::Entity> entities;
        world.Query<scene::LocalTransform>().Each(
            [&](ecs::Entity entity, scene::LocalTransform&) { entities.push_back(entity); });
        for (ecs::Entity entity : entities) world.Destroy(entity);
        m_player = ecs::Entity::Null();
        EnterTitle();
    }

    // 블릿과 동일한 정수 배율·레터박스로 입력 좌표를 변환한다.
    Vec2 MouseToRt() const {
        if (!m_input) return {-1.0f, -1.0f};
        Vec2 m = Vec2{static_cast<float>(m_input->MousePosition().x),
                      static_cast<float>(m_input->MousePosition().y)};
        float w = 960.0f, h = 540.0f;
        if (m_swapChain) {
            const Vec2i sz = m_swapChain->GetSize();
            w = static_cast<float>(sz.x > 0 ? sz.x : 960);
            h = static_cast<float>(sz.y > 0 ? sz.y : 540);
        }
        const auto layout = render::PixelPerfectTarget::ComputeLayout({960, 540},
            {static_cast<int32_t>(w), static_cast<int32_t>(h)});
        return {(m.x - layout.destRect.x) / layout.scale, (m.y - layout.destRect.y) / layout.scale};
    }

    void Update(const TimeStep& step) {
        const float dt = static_cast<float>(step.deltaSeconds > 0 ? step.deltaSeconds : (1.0 / 60.0));
        m_animTime += dt;

        switch (m_state) {
        case GameState::Title:
        case GameState::Settings:
            UpdateTitle(dt);
            break;
        case GameState::Playing:
            UpdatePlaying(dt);
            break;
        }

        m_camera.TickShake(dt);
        anim::RunAnimationSystem(m_scene->World(), dt);
        if (m_assets) m_assets->Update();
    }

    // ---- 타이틀/설정 갱신: 스크롤·연기·기차 진동·메뉴 입력 ----
    void UpdateTitle(float dt) {
        ecs::World& world = m_scene->World();

        // 배경/전경 패럴랙스 — 좌루프(모듈로 랩). 피벗이 좌상단이라 x=오프셋 그대로.
        m_bgScroll = std::fmod(m_bgScroll + 1.1f * dt, 40.0f);
        m_fgScroll = std::fmod(m_fgScroll + 2.4f * dt, 20.0f);
        if (auto* lt = world.TryGet<scene::LocalTransform>(m_titleBgA))
            lt->position.x = m_bgScroll - 40.0f;
        if (auto* lt = world.TryGet<scene::LocalTransform>(m_titleBgB))
            lt->position.x = m_bgScroll;
        if (auto* lt = world.TryGet<scene::LocalTransform>(m_titleFgA))
            lt->position.x = m_fgScroll - 20.0f;
        if (auto* lt = world.TryGet<scene::LocalTransform>(m_titleFgB))
            lt->position.x = m_fgScroll;

        // 기차 미세 진동(주행감).
        if (auto* lt = world.TryGet<scene::LocalTransform>(m_titleTrain))
            lt->position.y = kRailWorldY + std::sin(m_animTime * 42.0f) * 0.008f;

        // 굴뚝 연기 — 기차 기준 상대 좌표로 순환(왼쪽으로 흘러가며 상승·팽창).
        {
            const float chimneyX = 1.6f + 3.55f;   // 기차 중심 + 굴뚝 오프셋(172px)
            const float chimneyY = kRailWorldY + 3.55f;
            for (size_t i = 0; i < m_smokeEnts.size(); ++i) {
                auto* lt = world.TryGet<scene::LocalTransform>(m_smokeEnts[i]);
                auto* sr = world.TryGet<scene::SpriteRenderer>(m_smokeEnts[i]);
                if (!lt || !sr) continue;
                float& life = m_smokeLife[i];
                life += dt * 0.22f;
                if (life >= 1.0f) life -= 1.0f;
                const float t = life;
                lt->position.x = chimneyX - t * 4.2f + std::sin(t * 9.0f + static_cast<float>(i)) * 0.12f;
                lt->position.y = chimneyY + t * 2.1f;
                const float s = 0.35f + t * 1.1f;
                lt->scale = Vec3{s, s, 1.0f};
                sr->tint.a = 1.0f - t * 0.55f;
            }
        }

        // 메뉴 입력.
        if (m_state == GameState::Title) UpdateTitleMenuInput();
        else                             UpdateSettingsInput();
    }

    void UpdateTitleMenuInput() {
        if (!m_input) return;
        if (m_input->WasPressed(KeyCode::Up) || m_input->WasPressed(KeyCode::W))
            m_menuCursor = (m_menuCursor + 1) % 2;
        if (m_input->WasPressed(KeyCode::Down) || m_input->WasPressed(KeyCode::S))
            m_menuCursor = (m_menuCursor + 1) % 2;
        // 마우스 호버.
        const Vec2 m = MouseToRt();
        for (int i = 0; i < 2; ++i)
            if (MenuHit(i, m)) m_menuCursor = i;
        const bool confirm = m_input->WasPressed(KeyCode::Enter) || m_input->WasPressed(KeyCode::Space) ||
                             (m_input->WasPressed(MouseButton::Left) && MenuHit(m_menuCursor, m));
        if (!confirm) return;
        if (m_menuCursor == 0) StartGame();
        else m_state = GameState::Settings;
    }

    static Rect MenuRect(int index) {
        return Rect{80.0f, 292.0f + index * 48.0f, 300.0f, 42.0f};   // RT 픽셀
    }
    bool MenuHit(int index, const Vec2& m) const {
        const Rect r = MenuRect(index);
        return m.x >= r.x && m.x <= r.x + r.w && m.y >= r.y && m.y <= r.y + r.h;
    }

    // 환경설정 행: 0=캐릭터, 1=BGM, 2=SFX, 3=뒤로가기.
    void UpdateSettingsInput() {
        if (!m_input) return;
        constexpr int kRows = 4;
        if (m_input->WasPressed(KeyCode::Up) || m_input->WasPressed(KeyCode::W))
            m_settingsRow = (m_settingsRow + kRows - 1) % kRows;
        if (m_input->WasPressed(KeyCode::Down) || m_input->WasPressed(KeyCode::S))
            m_settingsRow = (m_settingsRow + 1) % kRows;

        const bool left  = m_input->WasPressed(KeyCode::Left) || m_input->WasPressed(KeyCode::A);
        const bool right = m_input->WasPressed(KeyCode::Right) || m_input->WasPressed(KeyCode::D);
        if (m_settingsRow == 0 && (left || right)) {
            m_gender = left ? 0 : 1;
            m_settingsDirty = true;
            SetSpriteTexture(m_titleChar, ChosenCharVpath());
            MYE_LOG_INFO("Game", "캐릭터 선택: {}", m_gender == 0 ? "남자 모험가" : "여자 모험가");
        }
        if (m_settingsRow == 1 || m_settingsRow == 2) {
            float& vol = (m_settingsRow == 1) ? m_bgmVol : m_sfxVol;
            if (left)  vol = std::max(0.0f, vol - 0.1f);
            if (right) vol = std::min(1.0f, vol + 0.1f);
            if (left || right) {
                m_audio->SetBusVolume(m_settingsRow == 1 ? audio::BusId::BGM : audio::BusId::SFX, vol);
                m_settingsDirty = true;
            }
        }
        if (m_input->WasPressed(KeyCode::Escape) ||
            (m_settingsRow == 3 && (m_input->WasPressed(KeyCode::Enter) ||
                                    m_input->WasPressed(KeyCode::Space)))) {
            SaveSettings();
            m_state = GameState::Title;
        }
    }

    // ---- 플레이 갱신(로컬/넷) ----
    void SaveSettings() {
        if (!m_settingsDirty) return;
        ConfigSystem& config = m_ctx->Config();
        config.Set("game.character", ConfigValue{int64_t{m_gender}}, ConfigScope::User);
        config.Set("audio.bgm", ConfigValue{static_cast<double>(m_bgmVol)}, ConfigScope::User);
        config.Set("audio.sfx", ConfigValue{static_cast<double>(m_sfxVol)}, ConfigScope::User);
        if (auto result = config.Save(ConfigScope::User); !result)
            MYE_LOG_ERROR("Game", "설정 저장 실패: {}", result.GetError().message);
        else m_settingsDirty = false;
    }

    void FixedUpdate(const TimeStep& step) {
        if (m_state != GameState::Playing || m_net || m_player.IsNull()) return;
        const Vec2 movement = MoveInput();
        if (auto* transform = m_scene->World().TryGet<scene::LocalTransform>(m_player)) {
            const float distance = m_moveSpeed * static_cast<float>(step.deltaSeconds);
            transform->position.x += movement.x * distance;
            transform->position.y += movement.y * distance;
            transform->dirty = true;
        }
    }

    void UpdatePlaying(float dt) {
        if (m_input && m_input->WasPressed(KeyCode::Escape)) { BackToTitle(); return; }

        if (m_net) {
            NetUpdate(dt);
        } else if (!m_player.IsNull()) {
            const Vec2 mv = MoveInput();
            const bool moving = (mv.x != 0.0f || mv.y != 0.0f);
            AnimateCharacter(m_player, moving, mv.x, m_playerPivotY);
            // 데드존 카메라 팔로우(캐릭터가 화면 중앙 박스 안이면 카메라 고정).
            m_camera.FollowDeadzone(PlayerPos(), Vec2{2.5f, 1.5f});
        }
    }

    void Render(const TimeStep&) {
        if (!m_ready || !m_device) return;
        ecs::World& world = m_scene->World();
        scene::UpdateWorldTransforms(world);
        m_proxies.Clear();
        scene::ExtractRenderItems(world, m_proxies);

        // 전역 앰비언트(데이나이트) — 각 스프라이트 tint에 색을 곱해 씬 전체를 물들인다(셰이더 무변경
        //   2D 라이팅 1단계). 이후 포인트라이트/라이트버퍼는 M7 P1에서 렌더패스로 확장.
        const Color amb = m_cli.ambient;
        if (amb.r != 1.0f || amb.g != 1.0f || amb.b != 1.0f) {
            for (scene::RenderItem& it : m_proxies.items) {
                it.tint.r *= amb.r; it.tint.g *= amb.g; it.tint.b *= amb.b;
            }
        }
        const Color clear{0.10f * amb.r, 0.12f * amb.g, 0.16f * amb.b, 1.0f};

        m_device->BeginFrame();
        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        m_rt.BeginScenePass(cmd, clear);
        m_hybrid.Render(m_proxies, m_camera, cmd);
        // 오버레이(메뉴/HUD)는 씬 RT 안에 그려 픽셀퍼펙트 블릿에 포함.
        if (m_state == GameState::Title || m_state == GameState::Settings) DrawMenuOverlay(cmd);
        else if (m_state == GameState::Playing) DrawHud(cmd);
        m_rt.EndScenePass(cmd);

        if (m_hasWindow && m_swapChain) {
            rhi::TextureHandle backbuffer = m_swapChain->GetCurrentBackBuffer();
            const Vec2i winSize = m_swapChain->GetSize();
            m_rt.Blit(cmd, backbuffer, winSize, m_camera.SubpixelResidual());
        }

        ++m_frameCount;
        HandleDump();

        m_device->EndFrame();
        if (m_hasWindow && m_swapChain) m_swapChain->Present(false);

        if (m_cli.frameLimit && m_frameCount >= m_cli.maxFrames) RequestExit();
    }

    // ---- 오버레이 공통 도구(SpriteBatch 쿼드 + 한글 텍스트, RT 960×540 스크린 규약) ----
    static Mat4 OrthoScreen(float w, float h) {
        return Mat4::OrthoOffCenterLH(0.0f, w, h, 0.0f, 0.0f, 1.0f);
    }

    void SubmitQuad(float x, float y, float w, float h, Color c) {
        if (!m_whiteTex.IsValid()) return;   // 화이트 텍스처 없으면 패널 생략(텍스트는 유지)
        render::SpriteDraw d;
        d.yDown = true;
        d.position = {x, y};
        d.size = {w, h};
        d.pivot = {0.0f, 0.0f};   // 좌상단 기준
        d.texture = WhiteTexture();
        d.tint = c;
        d.sortY = 0; // 화면 레이어는 제출 순서로 합성
        m_spriteBatch.Submit(d);
    }

    rhi::TextureHandle TextureHandleOf(std::string_view vpath) {
        const asset::Texture* tex = TextureOf(vpath);
        return tex ? tex->gpuTexture : rhi::TextureHandle{};
    }

    rhi::TextureHandle WhiteTexture() { return m_whiteTex; }

    void DrawTextAt(rhi::ICommandContext& cmd, std::string_view s, float x, float y, uint16_t size,
                    Color color, bool shadow = true) {
        if (!m_koFont || s.empty()) return;
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        if (shadow) {
            text::TextStyle sh; sh.font = m_koFontId; sh.size = size; sh.color = Color{0.05f, 0.04f, 0.10f, 0.85f};
            m_textRenderer.DrawText(cmd, m_spriteBatch, m_atlas, *m_fonts, s, sh, Vec2i{ix + 2, iy + 2});
        }
        text::TextStyle st; st.font = m_koFontId; st.size = size; st.color = color;
        m_textRenderer.DrawText(cmd, m_spriteBatch, m_atlas, *m_fonts, s, st, Vec2i{ix, iy});
    }

    // ---- 타이틀/환경설정 오버레이 ----
    void DrawMenuOverlay(rhi::ICommandContext& cmd) {
        const Mat4 vp = OrthoScreen(960.0f, 540.0f);
        m_spriteBatch.Begin(vp);

        // 좌측 어두운 컬럼(텍스트 가독성 — 풍경 위 반투명).
        SubmitQuad(0.0f, 0.0f, 430.0f, 540.0f, Color{0.05f, 0.05f, 0.12f, 0.42f});

        // 게임 타이틀 로고 + 부제.
        DrawTextAt(cmd, "모험가의 여정", 84, 76, 58, Color{1.00f, 0.93f, 0.72f, 1.0f});
        DrawTextAt(cmd, "ADVENTURER'S JOURNEY — MyEngine MMO 데모", 88, 148, 15,
                   Color{0.82f, 0.78f, 0.88f, 1.0f}, false);

        if (m_state == GameState::Title) {
            // 메뉴 패널(게임 시작/환경설정).
            const float py = 268.0f;
            SubmitQuad(70.0f, py, 320.0f, 140.0f, Color{0.07f, 0.08f, 0.15f, 0.88f});
            SubmitQuad(70.0f, py, 320.0f, 2.0f, Color{0.95f, 0.83f, 0.50f, 0.9f});
            SubmitQuad(70.0f, py + 138.0f, 320.0f, 2.0f, Color{0.95f, 0.83f, 0.50f, 0.9f});
            static const char* kItems[2] = {"게임 시작", "환경설정"};
            for (int i = 0; i < 2; ++i) {
                const bool sel = (i == m_menuCursor);
                if (sel) SubmitQuad(80.0f, 292.0f + i * 48.0f, 300.0f, 42.0f, Color{0.16f, 0.17f, 0.28f, 0.95f});
                DrawTextAt(cmd, std::string(sel ? "▶ " : "  ") + kItems[i], 96.0f, 300.0f + i * 48.0f, 26,
                           sel ? Color{1.00f, 0.95f, 0.55f, 1.0f} : Color{0.80f, 0.82f, 0.90f, 1.0f},
                           sel);
            }
            DrawTextAt(cmd, "↑↓ 선택 · Enter 확인 · 마우스 클릭 가능", 84, 470, 14,
                       Color{0.66f, 0.68f, 0.78f, 1.0f}, false);
        } else {
            DrawSettingsPanel(cmd);
        }

        DrawTextAt(cmd, "개발 빌드", 878, 516, 13, Color{0.55f, 0.57f, 0.66f, 1.0f}, false);
        m_spriteBatch.End(cmd);
    }

    // 환경설정 패널 — 캐릭터(남/여)·BGM/SFX 볼륨·뒤로가기. 좌측 컬럼에 이어 붙임.
    void DrawSettingsPanel(rhi::ICommandContext& cmd) {
        const float px = 60.0f, py = 200.0f, pw = 360.0f, ph = 300.0f;
        SubmitQuad(px, py, pw, ph, Color{0.07f, 0.08f, 0.15f, 0.92f});
        SubmitQuad(px, py, pw, 2.0f, Color{0.95f, 0.83f, 0.50f, 0.9f});
        SubmitQuad(px, py + ph - 2.0f, pw, 2.0f, Color{0.95f, 0.83f, 0.50f, 0.9f});
        DrawTextAt(cmd, "환경설정", px + 24, py + 18, 30, Color{1.00f, 0.93f, 0.72f, 1.0f});

        struct Row { const char* label; };
        const Row rows[3] = {{"캐릭터"}, {"BGM 음량"}, {"효과음 음량"}};
        for (int i = 0; i < 3; ++i) {
            const float ry = py + 74.0f + i * 52.0f;
            const bool sel = (i == m_settingsRow);
            if (sel) SubmitQuad(px + 10, ry - 6, pw - 20, 40, Color{0.16f, 0.17f, 0.28f, 0.95f});
            DrawTextAt(cmd, rows[i].label, px + 26, ry, 21,
                       sel ? Color{1.00f, 0.95f, 0.55f, 1.0f} : Color{0.80f, 0.82f, 0.90f, 1.0f},
                       sel);
            if (i == 0) {
                DrawTextAt(cmd, m_gender == 0 ? "◀ 남자 모험가" : "◀ 여자 모험가", px + 150, ry, 20,
                           Color{0.86f, 0.90f, 1.0f, 1.0f}, sel);
            } else {
                const float vol = (i == 1) ? m_bgmVol : m_sfxVol;
                // 볼륨 바(12블록).
                const int filled = static_cast<int>(std::lround(vol * 12.0f));
                for (int b = 0; b < 12; ++b) {
                    SubmitQuad(px + 150.0f + b * 12.0f, ry + 4.0f, 10.0f, 16.0f,
                               b < filled ? Color{0.42f, 0.78f, 0.45f, 1.0f}
                                          : Color{0.16f, 0.18f, 0.24f, 1.0f});
                }
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(vol * 100.0f)));
                DrawTextAt(cmd, buf, px + 300, ry, 18, Color{0.86f, 0.90f, 1.0f, 1.0f}, sel);
            }
        }
        // 뒤로가기(행 3).
        {
            const float ry = py + 74.0f + 3 * 52.0f;
            const bool sel = (m_settingsRow == 3);
            if (sel) SubmitQuad(px + 10, ry - 6, pw - 20, 40, Color{0.16f, 0.17f, 0.28f, 0.95f});
            DrawTextAt(cmd, sel ? "▶ 뒤로가기" : "  뒤로가기", px + 26, ry, 21,
                       sel ? Color{1.00f, 0.95f, 0.55f, 1.0f} : Color{0.80f, 0.82f, 0.90f, 1.0f},
                       sel);
        }
        DrawTextAt(cmd, "←→ 조정 · ↑↓ 이동 · Esc 닫기", px + 24, py + ph - 34, 14,
                   Color{0.66f, 0.68f, 0.78f, 1.0f}, false);

        // 선택 캐릭터 미리보기 — 패널 우측 풍경 위에.
        if (const asset::Texture* tex = TextureOf(ChosenCharVpath())) {
            render::SpriteDraw d;
            const float h = 128.0f, w = h * (static_cast<float>(tex->width) /
                                             static_cast<float>(tex->height));
            d.position = {452.0f + w * 0.5f, 344.0f};   // 발밑 피벗(레일 위 서 있는 모습)
            d.size = {w, h};
            d.pivot = {0.5f, 1.0f};
            d.texture = tex->gpuTexture;
            d.sortY = 0.0f;
            d.yDown = true;
            m_spriteBatch.Submit(d);
            DrawTextAt(cmd, m_gender == 0 ? "남자 모험가" : "여자 모험가",
                       static_cast<int>(452.0f - 12.0f), 352, 15,
                       Color{1.0f, 0.95f, 0.7f, 1.0f});
        }
    }

    // ---- 플레이 HUD(최소) ----
    void DrawHud(rhi::ICommandContext& cmd) {
        const Mat4 vp = OrthoScreen(960.0f, 540.0f);
        m_spriteBatch.Begin(vp);
        char buf[160];
        if (m_net) {
            std::snprintf(buf, sizeof(buf), "서버 %s · 원격 %zu명",
                          m_serverEp.ToString().c_str(), m_remoteEnts.size());
        } else {
            std::snprintf(buf, sizeof(buf), "로컬 모드 — WASD 이동");
        }
        SubmitQuad(8.0f, 8.0f, 320.0f, 26.0f, Color{0.05f, 0.05f, 0.12f, 0.55f});
        DrawTextAt(cmd, buf, 16, 13, 15, Color{0.92f, 0.94f, 0.98f, 1.0f}, false);
        DrawTextAt(cmd, "ESC: 타이틀", 866, 516, 13, Color{0.7f, 0.72f, 0.8f, 1.0f}, false);
        m_spriteBatch.End(cmd);
    }

    void HandleDump() {
        if (!m_cli.dumpEnabled || m_dumped) return;
        const bool last = m_cli.frameLimit ? (m_frameCount >= m_cli.maxFrames) : (m_frameCount >= 3);
        if (!last) return;
        auto cap = rhi::CaptureBackbuffer(*m_device, m_rt.ColorTarget(), m_cli.dumpPath);
        if (cap) MYE_LOG_INFO("Game", "프레임 덤프 -> '{}'", m_cli.dumpPath);
        else     MYE_LOG_ERROR("Game", "덤프 실패: {}", cap.GetError().message);
        m_dumped = true;
    }

    // -------------------------------------------------------------------------
    // 네트워크 모드(M9) — 권위 서버 접속: 클라 예측/재조정 + 원격 엔티티 보간.
    // -------------------------------------------------------------------------
    static uint64_t SteadyMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // "ip:port" 파싱(포트 없으면 27015).
    static bool ParseEndpoint(std::string_view text, net::Endpoint& out) {
        const size_t colon = text.rfind(':');
        std::string_view ip = text;
        uint16_t port = 27015;
        if (colon != std::string_view::npos) {
            ip = text.substr(0, colon);
            port = static_cast<uint16_t>(std::strtoul(std::string(text.substr(colon + 1)).c_str(), nullptr, 10));
        }
        auto ep = net::Endpoint::Parse(ip, port);
        if (!ep) return false;
        out = ep.Value();
        return true;
    }

    bool SetupNetworking() {
        if (!ParseEndpoint(m_cli.connectAddr, m_serverEp)) {
            MYE_LOG_ERROR("Game", "--connect 주소 파싱 실패: '{}'", m_cli.connectAddr);
            return false;
        }
        m_netSys = std::make_unique<net::NetSubsystem>();
        if (!m_netSys->ok) { MYE_LOG_ERROR("Game", "Winsock 초기화 실패"); return false; }

        m_net = std::make_unique<net::NetClient>();
        if (!m_net->Open(0)) { MYE_LOG_ERROR("Game", "UDP 소켓 열기 실패"); return false; }
        // 서버 이동 파라미터와 동일하게(예측 수렴 조건) — NetServer 기본값과 일치.
        m_net->SetMoveSpeed(m_moveSpeed);
        m_net->Connect(m_serverEp, m_cli.account, m_cli.password);
        MYE_LOG_INFO("Game", "넷 모드 — {} 접속 시도(account='{}')", m_serverEp.ToString(), m_cli.account);
        return true;
    }

    // 로드된 텍스처 중 첫 사용 가능한 스프라이트 vpath 탐색(프로젝트 에셋에 따라 다름).
    std::string FirstLoadedSprite(std::initializer_list<std::string_view> candidates) const {
        for (std::string_view v : candidates)
            if (m_resolvers.textures.count(AssetResolvers::Key(DeterministicGuid(v)))) return std::string(v);
        return "assets://sprites/hero.png";   // 최후 폴백(없으면 스프라이트 미표시 — 치명적 아님)
    }

    void SetupNetEntities() {
        // 자기 캐릭터 — 환경설정에서 고른 모험가(없으면 종래 직업 폴백).
        const std::string self = FirstLoadedSprite({ChosenCharVpath(),
                                                    "assets://sprites/swordsman.png",
                                                    "assets://sprites/hero.png"});
        m_player = SpawnSprite(self, Vec3{0.0f, 0.0f, 0.0f}, 1.0f);
        m_playerPivotY = 24.0f;
        if (const asset::Texture* tex = TextureOf(self)) {
            m_playerPivotY = static_cast<float>(tex->height) * 0.5f;
            if (auto* sr = m_scene->World().TryGet<scene::SpriteRenderer>(m_player))
                sr->pivotPx = Vec2{static_cast<float>(tex->width) * 0.5f, m_playerPivotY};
        }

        // 원격 플레이어 — 반대 성별 모험가(구분 틴트 유지) + 종래 폴백.
        m_remoteSprite = FirstLoadedSprite({OtherCharVpath(),
                                            "assets://sprites/mage.png",
                                            "assets://sprites/buffer.png",
                                            "assets://sprites/slime.png",
                                            "assets://sprites/hero.png"});
        m_remotePivotY = 24.0f;
        if (const asset::Texture* tex = TextureOf(m_remoteSprite))
            m_remotePivotY = static_cast<float>(tex->height) * 0.5f;
    }

    // netId → 안정적인 구분 틴트(원격 플레이어 식별 보조).
    static Color RemoteTint(uint32_t netId) {
        switch (netId % 3) {
        case 0:  return Color{1.00f, 0.72f, 0.72f, 1.0f};   // 붉은 기
        case 1:  return Color{0.72f, 0.86f, 1.00f, 1.0f};   // 푸른 기
        default: return Color{0.78f, 1.00f, 0.78f, 1.0f};   // 푸른빛 녹색
        }
    }

    void NetUpdate(float dt) {
        // 핸드셰이크 — UDP 손실 대비 Connect 재전송(0.5s 간격, 수락될 때까지).
        if (!m_net->Connected()) {
            m_connectRetry += dt;
            if (m_connectRetry >= 0.5f) {
                m_connectRetry = 0.0f;
                m_net->Connect(m_serverEp, m_cli.account, m_cli.password);
            }
        }
        m_net->Receive();

        // 입력 송신 + 클라 예측 — 고정 60Hz 스텝(프레임레이트와 분리, 폭주 가드 8스텝).
        m_netAccum += dt;
        const float fixedDt = 1.0f / 60.0f;
        Vec2 lastMv{0.0f, 0.0f};
        for (int guard = 0; m_netAccum >= fixedDt && guard < 8; ++guard) {
            const Vec2 mv = MoveInput();
            m_net->SendInput(m_inputSeq++, mv.x, mv.y, fixedDt);
            lastMv = mv;
            m_netAccum -= fixedDt;
        }

        // 자기 캐릭터 위치 = 예측(스냅샷 수신 시 NetClient 내부에서 권위 재조정).
        float px = 0.0f, py = 0.0f;
        if (m_net->GetPredicted(px, py)) {
            if (auto* lt = m_scene->World().TryGet<scene::LocalTransform>(m_player)) {
                lt->position.x = px;
                lt->position.y = py;
                lt->dirty = true;
            }
            AnimateCharacter(m_player, (lastMv.x != 0.0f || lastMv.y != 0.0f), lastMv.x, m_playerPivotY);
            m_camera.FollowDeadzone(Vec2{px, py}, Vec2{2.5f, 1.5f});
        }

        // 원격 엔티티 — 틱 변경 시에만 보간 버퍼에 적재, 표시 시각(now−지연)으로 샘플.
        if (m_net->LastTick() != m_lastPushedTick) {
            m_lastPushedTick = m_net->LastTick();
            m_interp.Push(SteadyMs(), m_net->LatestSnapshot());
        }
        m_interp.Sample(SteadyMs() - m_interp.InterpolationDelayMs(), m_net->Id(), m_remoteSnaps);
        SyncRemoteEntities();
    }

    // 보간 결과 → 원격 스프라이트 엔티티 스폰/제거/위치 갱신.
    void SyncRemoteEntities() {
        ecs::World& world = m_scene->World();
        std::unordered_set<uint32_t> live;
        for (const net::EntitySnap& s : m_remoteSnaps) {
            live.insert(s.netId);
            auto it = m_remoteEnts.find(s.netId);
            if (it == m_remoteEnts.end()) {
                const ecs::Entity e = SpawnSprite(m_remoteSprite, Vec3{s.x, s.y, 0.0f}, 0.95f);
                if (auto* sr = world.TryGet<scene::SpriteRenderer>(e)) {
                    if (const asset::Texture* tex = TextureOf(m_remoteSprite))
                        sr->pivotPx = Vec2{static_cast<float>(tex->width) * 0.5f,
                                           static_cast<float>(tex->height) * 0.5f};
                    sr->tint = RemoteTint(s.netId);
                }
                it = m_remoteEnts.emplace(s.netId, e).first;
                m_remotePrevX[s.netId] = s.x;
                MYE_LOG_INFO("Game", "원격 플레이어 #{} 스폰", s.netId);
            }
            const float prevX = m_remotePrevX[s.netId];
            const float velX = s.x - prevX;
            m_remotePrevX[s.netId] = s.x;
            if (auto* lt = world.TryGet<scene::LocalTransform>(it->second)) {
                lt->position.x = s.x;
                lt->position.y = s.y;
                lt->dirty = true;
            }
            AnimateCharacter(it->second, std::fabs(velX) > 0.0005f, velX, m_remotePivotY);
        }
        for (auto it = m_remoteEnts.begin(); it != m_remoteEnts.end();) {
            if (live.count(it->first)) { ++it; continue; }
            world.Destroy(it->second);
            m_remotePrevX.erase(it->first);
            MYE_LOG_INFO("Game", "원격 플레이어 #{} 제거", it->first);
            it = m_remoteEnts.erase(it);
        }
    }

    EngineContext*      m_ctx = nullptr;
    scene::SceneModule* m_scene = nullptr;
    InputState*         m_input = nullptr;
    GameCli             m_cli;
    std::function<void()> m_onExit;
    ecs::Entity         m_player = ecs::Entity::Null();
    float               m_moveSpeed = 6.0f;   // unit/sec

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;
    audio::AudioEngine*                      m_audio = nullptr; // AudioModule이 소유
    render::PixelPerfectTarget m_rt;
    render::HybridRenderer     m_hybrid;
    render::Camera2D           m_camera;
    AssetResolvers             m_resolvers;
    std::vector<asset::AssetHandle<asset::Texture>> m_texHandles;
    scene::RenderProxyList     m_proxies;
    std::string m_assetsDir;

    // 오디오/BGM.
    asset::AssetHandle<asset::AudioClip> m_bgmClip;
    bool m_bgmStarted = false;
    float m_bgmVol = 1.0f, m_sfxVol = 1.0f;

    // 한글 텍스트 스택(메뉴/HUD 오버레이).
    std::unique_ptr<text::FontRegistry> m_fonts;
    std::vector<uint8_t> m_fontBlob;
    text::FontId   m_koFontId{};
    text::IFont*   m_koFont = nullptr;
    text::GlyphAtlas    m_atlas;
    text::TextRenderer  m_textRenderer;
    render::SpriteBatch m_spriteBatch;
    rhi::TextureHandle  m_whiteTex{};

    // 게임 상태(타이틀/설정/플레이) + 메뉴 커서·설정값.
    GameState m_state = GameState::Title;
    int  m_menuCursor = 0;
    int  m_settingsRow = 0;
    int  m_gender = 0;            // 0=남자 모험가, 1=여자 모험가
    bool m_settingsDirty = false;
    float m_animTime = 0.0f;
    float m_playerPivotY = 24.0f; // 캐릭터 스프라이트 기준 피벗Y(밥 애니메이션 기저)
    float m_remotePivotY = 24.0f;

    // 타이틀 씬 엔티티·애니메이션 상태.
    std::vector<ecs::Entity> m_titleEnts;
    ecs::Entity m_titleBgA = ecs::Entity::Null(), m_titleBgB = ecs::Entity::Null();
    ecs::Entity m_titleFgA = ecs::Entity::Null(), m_titleFgB = ecs::Entity::Null();
    ecs::Entity m_titleTrain = ecs::Entity::Null(), m_titleChar = ecs::Entity::Null();
    std::vector<ecs::Entity> m_smokeEnts;
    std::vector<float> m_smokeLife;
    float m_bgScroll = 0.0f, m_fgScroll = 0.0f;

    // 네트워크 모드(M9) — 권위 서버 접속 상태.
    std::unique_ptr<net::NetSubsystem>        m_netSys;   // Winsock RAII(넷 모드만 생성)
    std::unique_ptr<net::NetClient>           m_net;
    net::SnapshotInterpolator                 m_interp;
    std::vector<net::EntitySnap>              m_remoteSnaps;   // 보간 샘플 결과(재사용 버퍼)
    std::unordered_map<uint32_t, ecs::Entity> m_remoteEnts;    // netId → 원격 스프라이트
    std::unordered_map<uint32_t, float>       m_remotePrevX;   // 원격 이동방향(플립용)
    net::Endpoint                             m_serverEp{};
    std::string                               m_remoteSprite;
    uint32_t                                  m_inputSeq = 1;
    uint32_t                                  m_lastPushedTick = 0;
    float                                     m_netAccum = 0.0f;
    float                                     m_connectRetry = 0.0f;

    bool     m_hasWindow = false;
    bool     m_ready = false;
    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
};

// -----------------------------------------------------------------------------
// GameApp — CLI 파싱 + 모듈 등록.
// -----------------------------------------------------------------------------
class GameApp final : public Application {
public:
    explicit GameApp(const LaunchArgs& args) {
        const auto& a = args.args;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--project" && i + 1 < a.size()) m_cli.projectDir = a[++i];
            else if (a[i] == "--scene" && i + 1 < a.size()) m_cli.scenePath = a[++i];
            else if (a[i] == "--make-sample") m_cli.makeSample = true;
            else if (a[i] == "--make-hunt") m_cli.makeHunt = true;
            else if (a[i] == "--auto-start") m_cli.autoStart = true;
            else if (a[i] == "--settings") m_cli.openSettings = true;
            else if (a[i] == "--connect" && i + 1 < a.size()) m_cli.connectAddr = a[++i];
            else if (a[i] == "--account" && i + 1 < a.size()) m_cli.account = a[++i];
            else if (a[i] == "--password" && i + 1 < a.size()) m_cli.password = a[++i];
            else if (a[i] == "--headless") m_cli.headless = true;
            else if (a[i] == "--night") m_cli.ambient = Color{0.42f, 0.50f, 0.85f, 1.0f};   // 밤(푸른 앰비언트)
            else if (a[i] == "--ambient" && i + 3 < a.size()) {
                m_cli.ambient.r = std::strtof(a[++i].c_str(), nullptr);
                m_cli.ambient.g = std::strtof(a[++i].c_str(), nullptr);
                m_cli.ambient.b = std::strtof(a[++i].c_str(), nullptr);
            }
            else if (a[i] == "--frames" && i + 1 < a.size()) {
                m_cli.frameLimit = true; m_cli.maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_cli.dumpEnabled = true; m_cli.dumpPath = a[++i];
            }
        }
    }

    void OnConfigure(ConfigSystem&) override {}

    void OnRegisterModules(ModuleRegistry& modules) override {
        modules.Register(std::make_unique<scene::SceneModule>());
        auto audio = std::make_unique<audio::AudioModule>();
        audio->SetHeadless(m_cli.headless);
        modules.Register(std::move(audio));
        auto game = std::make_unique<GameRuntimeModule>();
        // CLI 를 모듈 OnInitialize/OnPostInitialize 이전에 주입(projectDir·makeSample 반영).
        game->SetCli(m_cli, [this]() { RequestExit(0); });
        modules.Register(std::move(game));
    }

    void OnStart(EngineContext&) override {}
    void OnStop(EngineContext&) override {}

private:
    GameCli m_cli;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) { return new GameApp(args); }
} // namespace mye

int main(int /*argc*/, char** /*argv*/) {
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<std::size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    }
    launch.mainWindow.title = "MyGame — 모험가의 여정";
    launch.mainWindow.clientSize = {1920, 1080};
    return mye::GuardedMain(launch);
}
