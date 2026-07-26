// MyGame — 스탠드얼론 게임 런타임 (docs/mmorpg, M7)
//
// 에디터 없이 프로젝트(assets + .scene)를 로드해 실행하는 게임 실행 파일. 엔진 모듈 스택
// (SceneModule + GameRuntimeModule) 위에서 RHI 디바이스·스왑체인·픽셀퍼펙트 RT·하이브리드
// 렌더러·에셋·오디오를 조립하고, 데이터드리븐 씬을 mye::scene::SceneSerializer 로 로드해 매
// 프레임 렌더한다. village_demo(하드코딩 C++ 데모)를 데이터드리븐으로 일반화한 것.
//
// CLI:
//   --project <dir>        프로젝트 루트(assets/ 를 포함). 기본 samples/game_sample.
//   --scene <vpath|path>   로드할 씬(예: assets://scenes/sample.scene). 기본 위 프로젝트의 sample.
//   --make-sample          텍스처 하나로 데모 씬을 생성·저장하고 종료(자체 검증 시드).
//   --frames N             N 프레임 후 종료(자동 검증).
//   --dump <path.bmp>      마지막 프레임(내부 RT)을 BMP 로 덤프.
//   --headless             창 없이(오프스크린) — 실GPU present 회피.
#include "mye/core/App.h"
#include "mye/core/Module.h"
#include "mye/core/Log.h"
#include "mye/core/Math.h"
#include "mye/core/Window.h"
#include "mye/core/Base.h"

#include "mye/rhi/Rhi.h"

#include "mye/asset/FileSystem.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/Importer.h"
#include "mye/asset/MeshImporter.h"
#include "mye/asset/Texture.h"

#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/Camera2D.h"

#include "mye/audio/AudioEngine.h"

#include "mye/scene/SceneModule.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/RenderExtract.h"
#include "mye/anim/AnimationSystem.h"
#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"

#include <Windows.h>
#include <shellapi.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
    std::string projectDir = "samples/game_sample";
    std::string scenePath;          // 비면 <project>/assets/scenes/sample.scene
    bool        makeSample = false;
    bool        headless = false;
    bool        frameLimit = false;
    uint64_t    maxFrames = 0;
    bool        dumpEnabled = false;
    std::string dumpPath;
};

// -----------------------------------------------------------------------------
// GameRuntimeModule — 조립 + 씬 로드 + 프레임 루프(렌더).
// -----------------------------------------------------------------------------
class GameRuntimeModule final : public IModule {
public:
    const char* GetName() const override { return "GameRuntimeModule"; }
    std::span<const char* const> GetDependencies() const override {
        static const char* deps[] = {"SceneModule"};
        return deps;
    }

    void SetCli(const GameCli& cli, std::function<void()> onExit) {
        m_cli = cli;
        m_onExit = std::move(onExit);
    }

    void OnInitialize(EngineContext& ctx) override {
        m_ctx = &ctx;
        m_scene = ctx.GetService<scene::SceneModule>();

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

        // 오디오(오프라인 믹서 — 헤드리스/CI 안전).
        m_audio = std::make_unique<audio::AudioEngine>();
        (void)m_audio->Initialize(nullptr, 44100);

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

        m_ready = m_rt.IsInitialized() && m_hybrid.IsInitialized();
    }

    void OnPostInitialize(EngineContext& ctx) override {
        if (!m_ready) { RequestExit(); return; }

        PreloadTextures();

        if (m_cli.makeSample) {
            MakeSampleScene();
            RequestExit();
            return;
        }

        LoadScene();

        ctx.Modules().AddTick(this, UpdatePhase::Update,
                              [this](const TimeStep& s) { Update(s); }, 0);
        ctx.Modules().AddTick(this, UpdatePhase::PreRender,
                              [this](const TimeStep& s) { Render(s); }, 100);
    }

    void OnShutdown(EngineContext&) override {
        m_texHandles.clear();
        m_audio.reset();
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

    void Update(const TimeStep& step) {
        const float dt = static_cast<float>(step.deltaSeconds > 0 ? step.deltaSeconds : (1.0 / 60.0));
        anim::RunAnimationSystem(m_scene->World(), dt);
        if (m_audio) m_audio->Update(dt);
        if (m_assets) m_assets->Update();
    }

    void Render(const TimeStep&) {
        if (!m_ready || !m_device) return;
        ecs::World& world = m_scene->World();
        scene::UpdateWorldTransforms(world);
        m_proxies.Clear();
        scene::ExtractRenderItems(world, m_proxies);

        m_device->BeginFrame();
        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        m_rt.BeginScenePass(cmd, Color{0.10f, 0.12f, 0.16f, 1.0f});
        m_hybrid.Render(m_proxies, m_camera, cmd);
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

    void HandleDump() {
        if (!m_cli.dumpEnabled || m_dumped) return;
        const bool last = m_cli.frameLimit ? (m_frameCount >= m_cli.maxFrames) : (m_frameCount >= 3);
        if (!last) return;
        auto cap = rhi::CaptureBackbuffer(*m_device, m_rt.ColorTarget(), m_cli.dumpPath);
        if (cap) MYE_LOG_INFO("Game", "프레임 덤프 -> '{}'", m_cli.dumpPath);
        else     MYE_LOG_ERROR("Game", "덤프 실패: {}", cap.GetError().message);
        m_dumped = true;
    }

    EngineContext*      m_ctx = nullptr;
    scene::SceneModule* m_scene = nullptr;
    GameCli             m_cli;
    std::function<void()> m_onExit;

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;
    std::unique_ptr<audio::AudioEngine>       m_audio;
    render::PixelPerfectTarget m_rt;
    render::HybridRenderer     m_hybrid;
    render::Camera2D           m_camera;
    AssetResolvers             m_resolvers;
    std::vector<asset::AssetHandle<asset::Texture>> m_texHandles;
    scene::RenderProxyList     m_proxies;
    std::string m_assetsDir;

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
            else if (a[i] == "--headless") m_cli.headless = true;
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
        auto game = std::make_unique<GameRuntimeModule>();
        m_game = game.get();
        // CLI 를 모듈 OnInitialize/OnPostInitialize 이전에 주입(projectDir·makeSample 반영).
        m_game->SetCli(m_cli, [this]() { RequestExit(0); });
        modules.Register(std::move(game));
    }

    void OnStart(EngineContext&) override {}
    void OnStop(EngineContext&) override {}

private:
    GameCli m_cli;
    GameRuntimeModule* m_game = nullptr;
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
    launch.mainWindow.title = "MyGame";
    launch.mainWindow.clientSize = {1920, 1080};
    return mye::GuardedMain(launch);
}
