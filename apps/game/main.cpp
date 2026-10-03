// MyGame uses the public asset, scene and runtime APIs to play an authored project.
#include "mye/anim/AnimationSystem.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/Importer.h"
#include "mye/asset/MeshImporter.h"
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Log.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"
#include "mye/gameplay/Progression.h"
#include "mye/net/NetClient.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/render/Camera2D.h"
#include "mye/render/HybridRenderer.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/rhi/Rhi.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/runtime/OnlineScene.h"
#include "mye/scene/Camera3D.h"
#include "mye/scene/Camera2D.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/SceneReflection.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Transform.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

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
    std::string project, scene, dump, connect, credentials, input;
    uint64_t frames = 0, ticks = 0;
    uint64_t character = 0;
};

Expected<GameCli, Error> ParseCli(const std::vector<std::string>& args) {
    GameCli cli;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& option = args[i];
        if (option == "--headless") continue;
        if (option.starts_with("--project=")) { cli.project = option.substr(10); continue; }
        if (option != "--project" && option != "--scene" && option != "--frames" && option != "--ticks" && option != "--dump" &&
            option != "--connect" && option != "--credentials" && option != "--character" && option != "--input")
            return Error{"Unknown option: " + option, 64};
        if (++i == args.size() || args[i].empty()) return Error{"Missing value: " + option, 64};
        const auto& value = args[i];
        if (option == "--project") cli.project = value;
        else if (option == "--scene") cli.scene = value;
        else if (option == "--dump") cli.dump = value;
        else if (option == "--connect") cli.connect = value;
        else if (option == "--credentials") cli.credentials = value;
        else if (option == "--input") cli.input = value;
        else if (option == "--character") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), cli.character);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || cli.character == 0)
                return Error{"--character requires a positive character ID", 64};
        } else {
            auto& limit = option == "--ticks" ? cli.ticks : cli.frames;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || limit == 0)
                return Error{option + " requires a positive integer", 64};
        }
    }
    if (cli.project.empty() || Utf8Path(cli.project).extension() != ".myeproj")
        return Error{"--project requires a .myeproj manifest", 64};
    if (cli.connect.empty() ? (!cli.credentials.empty() || cli.character != 0) : cli.credentials.empty())
        return Error{"Online mode requires --connect 127.0.0.1:port and --credentials file.json", 64};
    return cli;
}

// Events outlive the world, and Lua/physics state is destroyed before the world.
struct GameScene {
    EventBus events;
    ecs::World world;
    std::unique_ptr<runtime::ObjectSystem> objects;
    GameScene() {
        world.SetEventBus(&events);
        scene::RegisterCoreComponents(world);
        gameplay::RegisterProgressionReflection();
        runtime::RegisterObjectComponents(world);
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
        ctx.Modules().AddTick(this, UpdatePhase::PreUpdate, [this](const TimeStep&) {
            if (!m_input) return;
            m_inputFocused = !m_window || GetForegroundWindow() == m_window->GetNativeHandle();
            m_input->SetKeyboardSuppressed(!m_inputFocused);
            m_input->SetMouseSuppressed(!m_inputFocused);
            if (!m_inputFocused) {
                m_gameInput.Clear();
                return;
            }
            m_gameInput.Capture(*m_input, true);
        });
        ctx.Modules().AddTick(this, UpdatePhase::FixedUpdate, [this](const TimeStep& step) { Tick(static_cast<float>(step.deltaSeconds)); });
        ctx.Modules().AddTick(this, UpdatePhase::PreRender, [this](const TimeStep&) { Render(); });
    }
    void OnShutdown(EngineContext&) override {
        if (m_client.Connected()) m_client.Disconnect();
        m_client.Close();
        m_resize.Reset();
        m_scene.reset();
        m_textures.clear();
        m_meshes.clear();
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
    Expected<void, Error> LoadInputReplay() {
        if (m_cli.input.empty()) return {};
        auto file = ReadJsonFile(Utf8Path(m_cli.input));
        if (!file) return file.GetError();
        const auto* version = file.Value().Find("version");
        const auto* steps = file.Value().Find("steps");
        if (!version || !version->IsInteger() || version->AsInt() != 1 || !steps || !steps->IsArray() ||
            steps->AsArray().empty() || steps->AsArray().size() > 256)
            return Error{"Input replay requires version 1 and 1..256 steps", 1};
        for (const auto& step : steps->AsArray()) {
            const auto* ticks = step.Find("ticks");
            const auto* x = step.Find("x");
            const auto* y = step.Find("y");
            const auto* jump = step.Find("jump");
            const auto* zoom = step.Find("cameraZoomSteps");
            if (!ticks || !ticks->IsInteger() || ticks->AsInt() < 1 || ticks->AsInt() > 36000 ||
                !x || !x->IsNumber() || !y || !y->IsNumber() ||
                !std::isfinite(x->AsDouble()) || !std::isfinite(y->AsDouble()) ||
                std::abs(x->AsDouble()) > 1 || std::abs(y->AsDouble()) > 1 || (jump && !jump->IsBool()) ||
                (zoom && (!zoom->IsNumber() || !std::isfinite(zoom->AsDouble()) || std::abs(zoom->AsDouble()) > 16)) ||
                m_replay.size() + static_cast<std::size_t>(ticks->AsInt()) > 36000)
                return Error{"Input replay requires integer ticks, axes in [-1,1], finite cameraZoomSteps in [-16,16] and at most 36000 total ticks", 1};
            runtime::GameInput input{{static_cast<float>(x->AsDouble()), static_cast<float>(y->AsDouble())},
                                          false, jump && jump->AsBool()};
            input.cameraZoomSteps = zoom ? static_cast<float>(zoom->AsDouble()) : 0;
            m_replay.insert(m_replay.end(), static_cast<std::size_t>(ticks->AsInt()), input);
        }
        return {};
    }
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
            m_scene->world.Query<runtime::CharacterController3D, gameplay::Progression>().Each(
                [&](ecs::Entity, const auto& controller, const auto& value) {
                    if (controller.enabled) progression = &value;
                });
            bool playerFound = false;
            candidate->world.Query<runtime::CharacterController2D, scene::LocalTransform>().Each([&](ecs::Entity entity, const auto& controller, auto& transform) {
                if (!controller.enabled) return;
                playerFound = true; transform.position = spawn; transform.dirty = true;
                if (progression) if (auto* value = candidate->world.TryGet<gameplay::Progression>(entity)) *value = *progression;
            });
            candidate->world.Query<runtime::CharacterController3D, scene::LocalTransform>().Each(
                [&](ecs::Entity entity, const auto& controller, auto& transform) {
                    if (!controller.enabled) return;
                    playerFound = true;
                    transform.position = spawn;
                    transform.dirty = true;
                    if (progression)
                        if (auto* value = candidate->world.TryGet<gameplay::Progression>(entity))
                            *value = *progression;
                });
            if (!playerFound) return Error{"Destination scene needs an enabled character controller", 1};
        }
        scene::UpdateWorldTransforms(candidate->world);
        if (auto camera = scene::UpdateGameCamera2D(candidate->world, 0); !camera) return camera.GetError();
        if (m_cli.connect.empty()) {
            candidate->objects = std::make_unique<runtime::ObjectSystem>(candidate->world);
            auto initialized = candidate->objects->Initialize();
            if (!initialized) return initialized.GetError();
        }
        m_scene = std::move(candidate);
        scene::UpdateWorldTransforms(m_scene->world);
        runtime::UpdateDefaultCamera2D(m_scene->world, m_camera, true);
        MYE_LOG_INFO("Game", "Scene loaded: {}", relative);
        return {};
    }
    Expected<void, Error> Initialize(EngineContext& ctx) {
        if (auto replay = LoadInputReplay(); !replay) return replay.GetError();
        auto manifest = ReadJsonFile(Utf8Path(m_cli.project));
        if (!manifest) return manifest.GetError();
        const auto* version = manifest.Value().Find("version");
        const auto* name = manifest.Value().Find("name");
        const auto* mainScene = manifest.Value().Find("mainScene");
        if (!manifest.Value().IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
            !name || !name->IsString() || !mainScene || !mainScene->IsString())
            return Error{"Invalid project manifest (version 1 required)", 1};
        m_title = "MyGame - " + std::string(name->AsString());
        auto inputMap = runtime::LoadGameInputMap(manifest.Value().Find("inputMap"));
        if (!inputMap) return inputMap.GetError();
        if (auto configured = m_gameInput.Configure(std::move(inputMap).Value()); !configured) return configured.GetError();
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
        m_assets->RegisterImporter(std::make_unique<asset::MeshImporter>());
        m_assetDb = std::make_unique<asset::AssetDatabase>(*m_assets, nullptr);
        auto scanned = m_assetDb->ScanDirectory(Utf8String(m_root / "assets"));
        if (!scanned) return scanned.GetError();
        m_hybrid.SetTextureResolver([](void* user, asset::AssetGuid guid) { return static_cast<ProjectPlayer*>(user)->Texture(guid); }, this);
        m_hybrid.SetMeshResolver([](void* user, asset::AssetGuid guid) { return static_cast<ProjectPlayer*>(user)->Mesh(guid); }, this);
        auto loaded = LoadScene(m_cli.scene.empty() ? mainScene->AsString() : m_cli.scene);
        if (!loaded) return loaded.GetError();
        return m_cli.connect.empty() ? Expected<void, Error>{} : InitializeOnline();
    }
    Expected<void, Error> InitializeOnline() {
        const auto colon = m_cli.connect.find(':');
        uint16_t port = 0;
        if (colon == std::string::npos || m_cli.connect.substr(0, colon) != "127.0.0.1")
            return Error{"Online development transport requires 127.0.0.1:port", 64};
        const auto text = std::string_view(m_cli.connect).substr(colon + 1);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !port)
            return Error{"Invalid online port", 64};
        auto credentials = ReadJsonFile(Utf8Path(m_cli.credentials));
        if (!credentials) return credentials.GetError();
        const auto* user = credentials.Value().Find("username");
        const auto* password = credentials.Value().Find("password");
        if (!user || !password || !user->IsString() || !password->IsString() || user->AsString().empty() ||
            user->AsString().size() > 64 || password->AsString().empty() || password->AsString().size() > 128)
            return Error{"Credentials file requires bounded username and password strings", 64};
        auto online = runtime::LoadOnlineScene(m_cli.project, m_cli.scene);
        if (!online) return online.GetError();
        m_online = std::move(online).Value();
        m_network.emplace();
        if (!m_network->ok || !m_client.Open()) return Error{"Online socket initialization failed", 1};
        Expected<void, Error> configured;
        if (const auto* twoD = std::get_if<runtime::OnlineScene2D>(&*m_online)) {
            m_scene->world.Query<runtime::CharacterController2D>().Each([&](ecs::Entity e, const auto& c) {
                if (c.enabled) m_player = e;
            });
            const phys::MotionSettings2D settings{twoD->character, twoD->offset, twoD->speed, twoD->maxSlideIters};
            configured = m_client.Configure2D(twoD->colliders, settings, twoD->hash, m_cli.character);
        } else {
            const auto& threeD = std::get<runtime::OnlineScene3D>(*m_online);
            m_scene->world.Query<runtime::CharacterController3D>().Each([&](ecs::Entity e, const auto& c) {
                if (c.enabled) m_player = e;
            });
            configured = m_client.Configure3D(threeD.physics, threeD.settings, threeD.hash, m_cli.character);
        }
        if (m_player.IsNull()) return Error{"Online character prototype changed during project loading", 1};
        if (!configured) return configured.GetError();
        m_client.Connect(net::Endpoint::Loopback(port), user->AsString(), password->AsString());
        m_networkActivity = std::chrono::steady_clock::now();
        return {};
    }
    Expected<void, Error> TickOnline(float dt, const runtime::GameInput& input) {
        if (std::abs(dt - net::kFixedDelta3D) > 1e-6f)
            return Error{"Online game requires the fixed 60 Hz simulation contract", 1};
        const auto previousTick = m_client.LastTick();
        m_client.Receive();
        if (!m_client.Failure().empty()) return Error{std::string(m_client.Failure()), 1};
        if (previousTick != m_client.LastTick()) m_networkActivity = std::chrono::steady_clock::now();
        if (std::chrono::steady_clock::now() - m_networkActivity > std::chrono::seconds(5))
            return Error{"Online connection or snapshot timed out", 1};
        const bool send = m_cli.input.empty() || m_replayTick < m_replay.size();
        auto result = std::holds_alternative<runtime::OnlineScene2D>(*m_online)
                          ? TickOnline2D(dt, input, send) : TickOnline3D(dt, input, send);
        if (!result) return result.GetError();
        if (m_onlineSpawnLogged && !m_cli.input.empty()) {
            if (send) ++m_replayTick;
            else if (m_client.PendingInputs() == 0) {
                m_replayFinished = true;
                MYE_LOG_INFO("Game", "Input replay confirmed: steps={}, pending=0, netId={}", m_replayTick, m_client.Id());
            }
        }
        RemoveRemotes();
        return {};
    }
    Expected<void, Error> TickOnline2D(float dt, const runtime::GameInput& input, bool send) {
        phys::MotionState2D predicted;
        if (!m_client.GetPredicted2D(predicted)) return {};
        if (!m_onlineSpawnLogged) {
            m_onlineSpawnLogged = true;
            MYE_LOG_INFO("Game", "Online spawn confirmed: netId={}, scene={}, entities={}, dimension=2D, position=({}, {}), floor={}",
                         m_client.Id(), std::get<runtime::OnlineScene2D>(*m_online).sceneId,
                         m_client.EntityCount(), predicted.position.x, predicted.position.y, int(predicted.floorLevel));
        }
        if (send) if (auto sent = m_client.SendInput2D(input.movement); !sent) return sent.GetError();
        m_client.GetPredicted2D(predicted);
        auto* body = m_scene->world.TryGet<phys::KinematicBody2D>(m_player);
        body->lastMove = predicted.lastMove;
        body->velocity = predicted.lastMove / dt;
        body->hitWall = predicted.onWall;
        const auto* controller = m_scene->world.TryGet<runtime::CharacterController2D>(m_player);
        const auto apply = [&](ecs::Entity entity, const phys::MotionState2D& state) {
            auto& pose = *m_scene->world.TryGet<scene::LocalTransform>(entity);
            pose.position.x = state.position.x;
            pose.position.y = state.position.y;
            pose.dirty = true;
            if (auto* floor = m_scene->world.TryGet<scene::FloorLevel>(entity)) floor->level = state.floorLevel;
            if (auto* animator = m_scene->world.TryGet<anim::SpriteAnimator>(entity))
                runtime::UpdateCharacterAnimation2D(*animator, *controller, state.lastMove,
                    {std::sin(state.facingRadians), std::cos(state.facingRadians)});
        };
        apply(m_player, predicted);
        for (const auto& snap : m_client.LatestSnapshot2D()) {
            if (snap.netId != m_client.Id()) apply(Remote(snap.netId), snap.state);
            else if (!send && m_client.PendingInputs() == 0)
                MYE_LOG_INFO("Game", "Online state confirmed: ack={}, position=({}, {}), floor={}, facing={}",
                             snap.ack, snap.state.position.x, snap.state.position.y,
                             int(snap.state.floorLevel), snap.state.facingRadians);
        }
        return {};
    }
    Expected<void, Error> TickOnline3D(float dt, const runtime::GameInput& input, bool send) {
        const auto& online = std::get<runtime::OnlineScene3D>(*m_online);
        scene::UpdateWorldTransforms(m_scene->world);
        auto camera = scene::UpdateGameCamera(m_scene->world, online.physics, dt, input.cameraAxis,
                                              input.cameraMouseX);
        if (!camera) return camera.GetError();
        phys::MotionState3D predicted;
        if (!m_client.GetPredicted3D(predicted)) return {};
        if (!m_onlineSpawnLogged) {
            m_onlineSpawnLogged = true;
            MYE_LOG_INFO("Game", "Online spawn confirmed: netId={}, scene={}, entities={}", m_client.Id(),
                         online.sceneId, m_client.EntityCount());
        }
        const auto* controller = m_scene->world.TryGet<runtime::CharacterController3D>(m_player);
        const auto movement = controller->cameraRelative
                                  ? scene::CameraRelativeMovement(m_scene->world, input.movement)
                                  : input.movement;
        if (send) if (auto sent = m_client.SendInput3D(movement, input.jump); !sent) return sent.GetError();
        m_client.GetPredicted3D(predicted);
        auto* body = m_scene->world.TryGet<phys::KinematicBody3D>(m_player);
        body->lastMove = predicted.position - body->state.position;
        body->state = predicted;
        body->initialized = true;
        auto& pose = *m_scene->world.TryGet<scene::LocalTransform>(m_player);
        pose.position = predicted.position;
        pose.dirty = true;
        const auto applyMotion = [&](ecs::Entity entity, const phys::MotionState3D& state) {
            const bool moving = state.velocity.x * state.velocity.x + state.velocity.z * state.velocity.z > 1e-6f;
            ApplyAnimation(entity, moving ? controller->walkAnimation : controller->idleAnimation, state.facingRadians);
        };
        applyMotion(m_player, predicted);
        for (const auto& snap : m_client.LatestSnapshot3D()) {
            if (snap.netId == m_client.Id()) continue;
            const auto remote = Remote(snap.netId);
            auto* transform = m_scene->world.TryGet<scene::LocalTransform>(remote);
            transform->position = snap.state.position;
            transform->dirty = true;
            applyMotion(remote, snap.state);
        }
        scene::UpdateWorldTransforms(m_scene->world);
        return scene::UpdateGameCamera(m_scene->world, online.physics, dt, 0, 0);
    }
    void ApplyAnimation(ecs::Entity entity, const asset::AssetRef& animation, float facing) {
        if (auto* animator = m_scene->world.TryGet<anim::SpriteAnimator>(entity)) {
            if (animation.guid.IsValid() && animator->animation.guid != animation.guid) {
                animator->animation = animation;
                animator->sheet = nullptr;
                animator->directClip = nullptr;
                animator->cursor = {};
                animator->started = false;
            }
            animator->facing = anim::Dir8FromVector({std::sin(facing), std::cos(facing)}, animator->facing);
        }
    }
    ecs::Entity Remote(uint32_t id) {
        if (const auto found = m_remotes.find(id); found != m_remotes.end()) return found->second;
        auto& world = m_scene->world;
        const auto remote = world.Create();
        const auto pose = *world.TryGet<scene::LocalTransform>(m_player);
        world.Add<scene::LocalTransform>(remote) = pose;
        world.Add<scene::WorldTransform>(remote);
        world.Add<scene::ObjectName>(remote).value = "Network " + std::to_string(id);
        // Adding a component can reallocate its pool; copy values before adding to that same pool.
        if (const auto* v = world.TryGet<scene::SpriteRenderer>(m_player)) {
            const auto copy = *v; world.Add<scene::SpriteRenderer>(remote) = copy;
        }
        if (const auto* v = world.TryGet<scene::BillboardRenderer>(m_player)) {
            const auto copy = *v; world.Add<scene::BillboardRenderer>(remote) = copy;
        }
        if (const auto* v = world.TryGet<scene::MeshRenderer>(m_player)) {
            const auto copy = *v; world.Add<scene::MeshRenderer>(remote) = copy;
        }
        if (const auto* v = world.TryGet<scene::FloorLevel>(m_player)) {
            const auto copy = *v; world.Add<scene::FloorLevel>(remote) = copy;
        }
        if (const auto* v = world.TryGet<anim::SpriteAnimator>(m_player)) {
            const auto copy = *v; world.Add<anim::SpriteAnimator>(remote) = copy;
        }
        m_remotes.emplace(id, remote);
        MYE_LOG_INFO("Game", "Network object added: netId={}", id);
        return remote;
    }
    void RemoveRemotes() {
        for (auto it = m_remotes.begin(); it != m_remotes.end();) {
            const auto contains = [&](const auto& snapshots) {
                return std::any_of(snapshots.begin(), snapshots.end(), [&](const auto& snap) { return snap.netId == it->first; });
            };
            const bool present = std::holds_alternative<runtime::OnlineScene2D>(*m_online)
                                     ? contains(m_client.LatestSnapshot2D()) : contains(m_client.LatestSnapshot3D());
            if (!present) {
                MYE_LOG_INFO("Game", "Network object removed: netId={}", it->first);
                m_scene->world.Destroy(it->second);
                it = m_remotes.erase(it);
            } else ++it;
        }
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
    const asset::Mesh* Mesh(asset::AssetGuid guid) {
        if (!guid.IsValid()) return nullptr;
        auto found = m_meshes.find(guid);
        if (found == m_meshes.end()) {
            const auto path = m_assetDb->PathFromGuid(guid);
            const auto* importer = m_assets->FindImporterForPath(path);
            if (!importer || importer->ProducedType() != asset::Mesh::kAssetTypeId) return nullptr;
            found = m_meshes.emplace(guid, m_assets->LoadSync<asset::Mesh>(path)).first;
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
    void BindAnimations(bool fixedTick = false) {
        anim::ForEachAnimatedRenderer(m_scene->world, [&](ecs::Entity, auto& animator, auto& sprite) {
            const auto* data = Animation(animator.animation.guid);
            if (fixedTick && data && animator.playing && animator.cursor.finished && data->nextAnimation.guid.IsValid()) {
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
            anim::SampleAnimator(animator, &sprite);
        });
    }
    void Tick(float dt) {
        // Catch-up may schedule several fixed ticks before the next render.
        if (!m_ready || m_replayFinished || (m_cli.ticks && m_tick >= m_cli.ticks)) return;
        auto controls = m_gameInput.ConsumeTick();
        if (controls.exitGame) { m_exit(0); return; }
        if (!m_cli.input.empty()) controls = m_replayTick < m_replay.size() ? m_replay[m_replayTick] : runtime::GameInput{};
        const auto previousReplayTick = m_replayTick;
        const auto tick = m_online ? TickOnline(dt, controls) : m_scene->objects->Tick(dt, controls);
        if (!tick) {
            Fail(tick.GetError());
            return;
        }
        if (!m_online) {
            if (!m_cli.input.empty() && ++m_replayTick == m_replay.size()) {
                m_replayFinished = true;
                MYE_LOG_INFO("Game", "Input replay completed: steps={}", m_replayTick);
            }
            const auto request = m_scene->objects->TakeMapRequest();
            if (!request.scenePath.empty()) {
                auto loaded = LoadScene(request.scenePath, request.spawnName);
                if (!loaded) { Fail(loaded.GetError()); return; }
            }
        }
        BindAnimations(true);
        anim::RunAnimationSystem(m_scene->world, dt);
        scene::UpdateWorldTransforms(m_scene->world);
        if (m_online && m_onlineSpawnLogged) {
            const float zoomSteps = m_cli.input.empty() || previousReplayTick != m_replayTick
                                        ? controls.cameraZoomSteps : 0;
            auto camera = scene::UpdateGameCamera2D(m_scene->world, dt, zoomSteps);
            if (!camera) { Fail(camera.GetError()); return; }
        }
        runtime::UpdateDefaultCamera2D(m_scene->world, m_camera);
        if (m_window) {
            const auto title = m_online
                                   ? m_title + " | Online " + std::to_string(m_client.Id()) + " | Players " +
                                         std::to_string(m_client.EntityCount())
                                   : m_title + " | " + std::string(m_scene->objects->Prompt()) + " | " +
                                         std::string(m_scene->objects->Message());
            if (title != m_currentTitle) { m_window->SetTitle(title); m_currentTitle = title; }
        }
        if (m_ready && ++m_tick == m_cli.ticks)
            MYE_LOG_INFO("Game", "Fixed tick limit reached: {}", m_tick);
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
        const auto view = scene::BuildGameView(m_scene->world, m_camera);
        if (!view) { m_target.EndScenePass(cmd); m_device->EndFrame(); Fail(view.GetError()); return; }
        const auto rendered = m_hybrid.Render(m_proxies, view.Value(), cmd);
        m_target.EndScenePass(cmd);
        if (!rendered) {
            m_device->EndFrame();
            Fail(Error{m_cli.project + ": " + rendered.GetError().message, rendered.GetError().code});
            return;
        }
        if (m_swapChain) m_target.Blit(cmd, m_swapChain->GetCurrentBackBuffer(), m_swapChain->GetSize(), view.Value().subpixelResidual);
        ++m_frame;
        const bool finished = m_replayFinished || (m_cli.frames && m_frame >= m_cli.frames) ||
                               (m_cli.ticks && m_tick >= m_cli.ticks);
        if (finished && ((m_online && !m_onlineSpawnLogged) || (!m_cli.input.empty() && !m_replayFinished))) {
            m_device->EndFrame();
            Fail(Error{"Game ended before online admission or input replay confirmation", 1});
            return;
        }
        if (finished) m_scene->world.Query<scene::Camera2D>().Each([&](ecs::Entity, const auto& camera) {
            if (camera.current)
                MYE_LOG_INFO("Game", "Camera2D final: zoom={}, center=({}, {})", camera.zoom,
                             camera.view.Position().x, camera.view.Position().y);
        });
        if (!m_cli.dump.empty() && (m_cli.frames || m_cli.ticks || !m_cli.input.empty() ? finished : m_frame == 3)) {
            auto captured = rhi::CaptureBackbuffer(*m_device, m_target.ColorTarget(), m_cli.dump);
            if (!captured) Fail(captured.GetError());
        }
        m_device->EndFrame();
        if (m_swapChain) m_swapChain->Present(false);
        if (finished) m_exit(m_ready ? 0 : 1);
    }
    void Fail(const Error& error) { m_ready = false; MYE_LOG_ERROR("Game", "{}", error.message); m_exit(1); }

    GameCli m_cli;
    std::function<void(int)> m_exit;
    fs::path m_root;
    InputState* m_input = nullptr;
    IWindow* m_window = nullptr;
    ScopedSubscription m_resize;
    std::unique_ptr<GameScene> m_scene;
    std::optional<net::NetSubsystem> m_network;
    std::optional<runtime::OnlineScene> m_online; // Collision data outlives the client's non-owning views.
    net::NetClient m_client;
    ecs::Entity m_player{};
    std::map<uint32_t, ecs::Entity> m_remotes;
    std::chrono::steady_clock::time_point m_networkActivity;
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
    std::map<asset::AssetGuid, asset::AssetHandle<asset::Mesh>> m_meshes;
    std::map<asset::AssetGuid, asset::AnimationAsset> m_animations;
    std::vector<runtime::GameInput> m_replay;
    std::size_t m_replayTick = 0;
    std::string m_title, m_currentTitle;
    uint64_t m_frame = 0, m_tick = 0;
    bool m_ready = false, m_replayFinished = false;
    runtime::GameInputBuffer m_gameInput;
    bool m_onlineSpawnLogged = false;
    bool m_inputFocused = true;
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
        std::puts("MyGame --project <project.myeproj> [--scene assets/scenes/name.scene] [--frames N] [--ticks N] [--dump frame.bmp] [--headless]\nOnline: --connect 127.0.0.1:port --credentials file.json [--character ID]; the scene selects 2D or 3D.\nInput replay: --input file.json; version 1, steps [{ticks:60,x:1,y:0,jump:false}].\nReplay starts after online admission and exits after all inputs are acknowledged.\n--ticks limits fixed simulation steps; when both limits are set, the first ends play.\nDefault controls: WASD/arrows or gamepad movement, E interact, Escape exit. Configure bindings in the project inputMap.");
        return 0;
    }
    auto cli = mye::ParseCli(launch.args);
    if (!cli) { std::fprintf(stderr, "%s\n", cli.GetError().message.c_str()); return 64; }
    launch.projectPath = mye::Utf8String(mye::Utf8Path(cli.Value().project).parent_path());
    launch.mainWindow.title = "MyGame";
    launch.mainWindow.clientSize = {1920, 1080};
    return mye::GuardedMain(launch);
}
