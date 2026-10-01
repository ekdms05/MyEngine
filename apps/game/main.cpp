// MyGame uses the public asset, scene and runtime APIs to play an authored project.
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/asset/Importer.h"
#include "mye/render/Camera2D.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/anim/AnimationSystem.h"
#include "mye/phys/Collision.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/gameplay/Progression.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"

#include <Windows.h>
#include <shellapi.h>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace mye {
namespace {
namespace fs = std::filesystem;
struct GameCli {
    std::string project, scene, dump;
    uint64_t frames = 0;
};

Expected<GameCli, Error> ParseCli(const std::vector<std::string>& args) {
    GameCli cli;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& option = args[i];
        if (option == "--headless") continue;
        if (option.starts_with("--project=")) { cli.project = option.substr(10); continue; }
        if (option != "--project" && option != "--scene" && option != "--frames" && option != "--dump")
            return Error{"Unknown option: " + option, 64};
        if (++i == args.size() || args[i].empty()) return Error{"Missing value: " + option, 64};
        const auto& value = args[i];
        if (option == "--project") cli.project = value;
        else if (option == "--scene") cli.scene = value;
        else if (option == "--dump") cli.dump = value;
        else {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), cli.frames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || cli.frames == 0)
                return Error{"--frames requires a positive integer", 64};
        }
    }
    if (cli.project.empty() || Utf8Path(cli.project).extension() != ".myeproj")
        return Error{"--project requires a .myeproj manifest", 64};
    return cli;
}

// Events outlive the world, and Lua/physics state is destroyed before the world.
struct GameScene {
    EventBus events;
    ecs::World world;
    std::unique_ptr<runtime::ObjectSystem> objects;
    GameScene() {
        world.SetEventBus(&events);
        scene::RegisterCoreComponentReflection();
        gameplay::RegisterProgressionReflection();
        runtime::RegisterObjectComponents(world);
        world.RegisterComponent<scene::ObjectName>("ObjectName");
        world.RegisterComponent<scene::LocalTransform>("LocalTransform");
        world.RegisterComponent<scene::WorldTransform>("WorldTransform");
        world.RegisterComponent<scene::Parent>("Parent");
        world.RegisterComponent<scene::Children>("Children");
        world.RegisterComponent<scene::SpriteRenderer>("SpriteRenderer");
        world.RegisterComponent<scene::FloorLevel>("FloorLevel");
        world.RegisterComponent<phys::Collider2D>("Collider2D");
        world.RegisterComponent<phys::KinematicBody2D>("KinematicBody2D");
        world.RegisterComponent<anim::SpriteAnimator>("SpriteAnimator");
        world.RegisterComponent<gameplay::Progression>("Progression");
    }
};

class ProjectPlayer final : public IModule {
public:
    ProjectPlayer(GameCli cli, std::function<void(int)> exit) : m_cli(std::move(cli)), m_exit(std::move(exit)) {}
    const char* GetName() const override { return "ProjectPlayer"; }
    void OnInitialize(EngineContext& ctx) override {
        m_input = ctx.GetService<InputState>();
        ecs::CommandBuffer::SetReparentHook(&scene::ApplyReparent);
        auto initialized = Initialize(ctx);
        if (!initialized) { Fail(initialized.GetError()); return; }
        m_ready = true;
    }
    void OnPostInitialize(EngineContext& ctx) override {
        if (!m_ready) return;
        ctx.Modules().AddTick(this, UpdatePhase::Update, [this](const TimeStep&) {
            if (!m_input) return;
            m_interact |= m_input->WasPressed(KeyCode::E);
            if (m_input->WasPressed(KeyCode::Escape)) m_exit(0);
        });
        ctx.Modules().AddTick(this, UpdatePhase::FixedUpdate, [this](const TimeStep& step) { Tick(static_cast<float>(step.deltaSeconds)); });
        ctx.Modules().AddTick(this, UpdatePhase::PreRender, [this](const TimeStep&) { Render(); });
    }
    void OnShutdown(EngineContext&) override {
        m_resize.Reset();
        m_scene.reset();
        m_textures.clear();
        m_animations.clear();
        m_assetDb.reset();
        m_assets.reset();
        m_vfs.reset();
        m_hybrid.Shutdown();
        m_target.Shutdown();
        m_swapChain.reset();
        m_device.reset();
    }
private:
    Expected<fs::path, Error> SceneFile(std::string_view relative) const {
        if (relative.find('\0') != std::string_view::npos || Utf8Path(relative).is_absolute())
            return Error{"Scene path must be project relative", 1};
        std::error_code ec;
        const auto path = fs::weakly_canonical(m_root / Utf8Path(relative), ec);
        const auto scope = path.lexically_relative(m_root / "assets");
        if (ec || scope.empty() || scope.is_absolute() || *scope.begin() == ".." || path.extension() != ".scene")
            return Error{"Scene must be a .scene file inside project assets", 1};
        return path;
    }
    Expected<void, Error> LoadScene(std::string_view relative, std::string_view spawnName = {}) {
        auto file = SceneFile(relative);
        if (!file) return file.GetError();
        // ponytail: small authored scenes load synchronously; use async asset loading if measured stalls warrant it.
        auto candidate = std::make_unique<GameScene>();
        auto loaded = scene::SceneSerializer{}.LoadFromFile(candidate->world, Utf8String(file.Value()));
        if (!loaded) return loaded.GetError();
        auto valid = runtime::ValidateObjectComponents(candidate->world);
        if (!valid) return valid.GetError();
        scene::UpdateWorldTransforms(candidate->world);
        if (!spawnName.empty()) {
            bool found = false;
            Vec3 spawn{};
            candidate->world.Query<scene::ObjectName, scene::WorldTransform>().Each([&](ecs::Entity, const auto& name, const auto& transform) {
                if (name.value == spawnName) { found = true; spawn = {transform.matrix.m[3][0], transform.matrix.m[3][1], transform.matrix.m[3][2]}; }
            });
            if (!found) return Error{"Destination spawn not found: " + std::string(spawnName), 1};
            const gameplay::Progression* progression = nullptr;
            m_scene->world.Query<runtime::CharacterController2D, gameplay::Progression>().Each([&](ecs::Entity, const auto& controller, const auto& value) {
                if (controller.enabled) progression = &value;
            });
            bool playerFound = false;
            candidate->world.Query<runtime::CharacterController2D, scene::LocalTransform>().Each([&](ecs::Entity entity, const auto& controller, auto& transform) {
                if (!controller.enabled) return;
                playerFound = true; transform.position = spawn; transform.dirty = true;
                if (progression) if (auto* value = candidate->world.TryGet<gameplay::Progression>(entity)) *value = *progression;
            });
            if (!playerFound) return Error{"Destination scene needs an enabled character controller", 1};
        }
        candidate->objects = std::make_unique<runtime::ObjectSystem>(candidate->world);
        auto initialized = candidate->objects->Initialize();
        if (!initialized) return initialized.GetError();
        m_scene = std::move(candidate);
        scene::UpdateWorldTransforms(m_scene->world);
        m_scene->world.Query<runtime::CharacterController2D, scene::LocalTransform>().Each([&](ecs::Entity, const auto& controller, const auto& transform) {
            if (controller.enabled) m_camera.SetPosition({transform.position.x, transform.position.y});
        });
        MYE_LOG_INFO("Game", "Scene loaded: {}", relative);
        return {};
    }
    Expected<void, Error> Initialize(EngineContext& ctx) {
        auto manifest = ReadJsonFile(Utf8Path(m_cli.project));
        if (!manifest) return manifest.GetError();
        const auto* version = manifest.Value().Find("version");
        const auto* name = manifest.Value().Find("name");
        const auto* mainScene = manifest.Value().Find("mainScene");
        if (!manifest.Value().IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
            !name || !name->IsString() || !mainScene || !mainScene->IsString())
            return Error{"Invalid project manifest (version 1 required)", 1};
        m_title = "MyGame - " + std::string(name->AsString());
        std::error_code ec;
        m_root = fs::canonical(Utf8Path(m_cli.project).parent_path().empty() ? fs::path(".") : Utf8Path(m_cli.project).parent_path(), ec);
        if (ec) return Error{"Project directory unavailable: " + ec.message(), ec.value()};
        auto device = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!device) return device.GetError();
        m_device = std::move(device).Value();
        if (ctx.GetServiceRaw(kMainWindowServiceId)) {
            m_window = &ctx.MainWindow();
            rhi::SwapChainDesc desc{};
            m_swapChain = m_device->CreateSwapChain(m_window->GetNativeHandle(), desc);
            if (!m_swapChain) return Error{"Game swap chain initialization failed", 1};
            m_resize = ScopedSubscription(ctx.Events(), ctx.Events().Subscribe<WindowResizedEvent>([this](const auto& event) {
                if (event.window == m_window && event.clientSize.x > 0 && event.clientSize.y > 0)
                    m_swapChain->Resize(static_cast<uint32_t>(event.clientSize.x), static_cast<uint32_t>(event.clientSize.y));
                return false;
            }));
        }
        render::PixelPerfectDesc target{};
        if (m_swapChain) target.backbufferFormat = m_swapChain->GetFormat();
        m_target.Init(*m_device, target);
        render::HybridRendererDesc renderer{};
        renderer.colorFormat = m_target.ColorFormat();
        m_hybrid.Init(*m_device, renderer);
        if (!m_target.IsInitialized() || !m_hybrid.IsInitialized()) return Error{"Game render targets initialization failed", 1};
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(m_root / "assets")), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        m_assetDb = std::make_unique<asset::AssetDatabase>(*m_assets, nullptr);
        auto scanned = m_assetDb->ScanDirectory(Utf8String(m_root / "assets"));
        if (!scanned) return scanned.GetError();
        m_hybrid.SetTextureResolver([](void* user, asset::AssetGuid guid) { return static_cast<ProjectPlayer*>(user)->Texture(guid); }, this);
        return LoadScene(m_cli.scene.empty() ? mainScene->AsString() : m_cli.scene);
    }
    const asset::Texture* Texture(asset::AssetGuid guid) {
        if (!guid.IsValid()) return nullptr;
        auto found = m_textures.find(guid);
        if (found == m_textures.end()) {
            const auto path = m_assetDb->PathFromGuid(guid);
            const auto* importer = m_assets->FindImporterForPath(path);
            if (!importer || importer->ProducedType() != asset::Texture::kAssetTypeId) return nullptr;
            found = m_textures.emplace(guid, m_assets->LoadSync<asset::Texture>(path)).first;
            if (!found->second.Get()) MYE_LOG_ERROR("Game", "Texture could not be loaded: {}", path);
        }
        return found->second.Get();
    }
    const asset::AnimationAsset* Animation(asset::AssetGuid guid) {
        if (!guid.IsValid()) return nullptr;
        auto found = m_animations.find(guid);
        if (found != m_animations.end()) return &found->second;
        const auto path = m_assetDb->PathFromGuid(guid);
        if (!path.starts_with("assets://") || !path.ends_with(".anim")) return nullptr;
        auto json = ReadJsonFile(m_root / "assets" / Utf8Path(path.substr(9)));
        if (!json) { Fail(json.GetError()); return nullptr; }
        auto animation = asset::AnimationAsset::FromJson(json.Value());
        if (!animation) { Fail(animation.GetError()); return nullptr; }
        return &m_animations.emplace(guid, std::move(animation).Value()).first->second;
    }
    void BindAnimations() {
        m_scene->world.Query<anim::SpriteAnimator, scene::SpriteRenderer>().Each([&](ecs::Entity, auto& animator, auto& sprite) {
            const auto* data = Animation(animator.animation.guid);
            if (data && animator.playing && animator.cursor.finished && data->nextAnimation.guid.IsValid()) {
                if (const auto* next = Animation(data->nextAnimation.guid)) {
                    animator.animation = data->nextAnimation; animator.cursor = {}; animator.started = false; data = next;
                }
            }
            const auto* texture = data ? Texture(data->sheet.texture.guid) : nullptr;
            if (!data || !texture || data->imageSize.x != static_cast<int32_t>(texture->width) || data->imageSize.y != static_cast<int32_t>(texture->height)) {
                animator.sheet = nullptr; animator.directClip = nullptr; return;
            }
            if (animator.directClip != &data->clip) { animator.cursor = {}; animator.started = false; }
            animator.sheet = &data->sheet; animator.directClip = &data->clip;
            anim::UpdateAnimator(animator, 0.0f, &sprite, [](const asset::AnimEventMarker&) {});
        });
    }
    void Tick(float dt) {
        if (!m_ready) return;
        Vec2 movement{};
        if (m_input) {
            movement.x = float(m_input->IsDown(KeyCode::D) || m_input->IsDown(KeyCode::Right)) - float(m_input->IsDown(KeyCode::A) || m_input->IsDown(KeyCode::Left));
            movement.y = float(m_input->IsDown(KeyCode::W) || m_input->IsDown(KeyCode::Up)) - float(m_input->IsDown(KeyCode::S) || m_input->IsDown(KeyCode::Down));
            movement = movement + m_input->LeftStick(0);
        }
        m_scene->objects->Tick(dt, movement, std::exchange(m_interact, false));
        const auto request = m_scene->objects->TakeMapRequest();
        if (!request.scenePath.empty()) {
            auto loaded = LoadScene(request.scenePath, request.spawnName);
            if (!loaded) { Fail(loaded.GetError()); return; }
        }
        BindAnimations();
        anim::RunAnimationSystem(m_scene->world, dt);
        scene::UpdateWorldTransforms(m_scene->world);
        m_scene->world.Query<runtime::CharacterController2D, scene::LocalTransform>().Each([&](ecs::Entity, const auto& controller, const auto& transform) {
            if (controller.enabled) m_camera.FollowDeadzone({transform.position.x, transform.position.y}, {2.5f, 1.5f});
        });
        if (m_window) {
            const auto title = m_title + " | " + std::string(m_scene->objects->Prompt()) + " | " + std::string(m_scene->objects->Message());
            if (title != m_currentTitle) { m_window->SetTitle(title); m_currentTitle = title; }
        }
    }
    void Render() {
        if (!m_ready) return;
        BindAnimations();
        scene::UpdateWorldTransforms(m_scene->world);
        m_proxies.Clear();
        scene::ExtractRenderItems(m_scene->world, m_proxies);
        m_device->BeginFrame();
        auto& cmd = m_device->GetImmediateContext();
        m_target.BeginScenePass(cmd, Color{0.09f, 0.10f, 0.13f, 1.0f});
        m_hybrid.Render(m_proxies, m_camera, cmd);
        m_target.EndScenePass(cmd);
        if (m_swapChain) m_target.Blit(cmd, m_swapChain->GetCurrentBackBuffer(), m_swapChain->GetSize(), m_camera.SubpixelResidual());
        ++m_frame;
        if (!m_cli.dump.empty() && m_frame == (m_cli.frames ? m_cli.frames : 3)) {
            auto captured = rhi::CaptureBackbuffer(*m_device, m_target.ColorTarget(), m_cli.dump);
            if (!captured) Fail(captured.GetError());
        }
        m_device->EndFrame();
        if (m_swapChain) m_swapChain->Present(false);
        if (m_cli.frames && m_frame >= m_cli.frames) m_exit(m_ready ? 0 : 1);
    }
    void Fail(const Error& error) { m_ready = false; MYE_LOG_ERROR("Game", "{}", error.message); m_exit(1); }

    GameCli m_cli;
    std::function<void(int)> m_exit;
    fs::path m_root;
    InputState* m_input = nullptr;
    IWindow* m_window = nullptr;
    ScopedSubscription m_resize;
    std::unique_ptr<GameScene> m_scene;
    std::unique_ptr<rhi::IDevice> m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    render::PixelPerfectTarget m_target;
    render::HybridRenderer m_hybrid;
    render::Camera2D m_camera;
    scene::RenderProxyList m_proxies;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager> m_assets;
    std::unique_ptr<asset::AssetDatabase> m_assetDb;
    std::map<asset::AssetGuid, asset::AssetHandle<asset::Texture>> m_textures;
    std::map<asset::AssetGuid, asset::AnimationAsset> m_animations;
    std::string m_title, m_currentTitle;
    uint64_t m_frame = 0;
    bool m_ready = false, m_interact = false;
};

class GameApp final : public Application {
public:
    explicit GameApp(const LaunchArgs& args) : m_cli(ParseCli(args.args).Value()) {}
    void OnConfigure(ConfigSystem&) override {}
    void OnRegisterModules(ModuleRegistry& modules) override {
        modules.Register(std::make_unique<ProjectPlayer>(m_cli, [this](int code) { RequestExit(code); }));
    }
private:
    GameCli m_cli;
};
}
Application* CreateApplication(const LaunchArgs& args) { return new GameApp(args); }
}

int main() {
    mye::LaunchArgs launch;
    int argc = 0;
    if (auto** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
        for (int i = 0; i < argc; ++i) launch.args.emplace_back(mye::Narrow(argv[i]));
        ::LocalFree(argv);
    }
    for (const auto& arg : launch.args) if (arg == "--help") {
        std::puts("MyGame --project <project.myeproj> [--scene assets/scenes/name.scene] [--frames N] [--dump frame.bmp] [--headless]\nWASD/arrows or gamepad: movement, E: interact, Escape: exit.");
        return 0;
    }
    auto cli = mye::ParseCli(launch.args);
    if (!cli) { std::fprintf(stderr, "%s\n", cli.GetError().message.c_str()); return 64; }
    launch.projectPath = mye::Utf8String(mye::Utf8Path(cli.Value().project).parent_path());
    launch.mainWindow.title = "MyGame";
    launch.mainWindow.clientSize = {1920, 1080};
    return mye::GuardedMain(launch);
}
