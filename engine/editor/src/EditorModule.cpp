// EditorModule.cpp — 에디터 실앱 부팅 + 프레임 오케스트레이션 + 씬 뷰포트 렌더 + 플레이 tick 게이팅
//                                                            (docs/07 §1·§3, 씬 뷰포트)
// 07 §1: 게임과 동일한 모듈 스택 위에 얹히는 EditorModule. 이 모듈이:
//   (1) RHI 디바이스·스왑체인·mye_imgui DebugUi(도킹 셸)를 소유·초기화하고,
//   (2) FixedUpdate에서 플레이를 진행하고 PreRender에서 [활성 World를 오프스크린 RT에 렌더] →
//       [DebugUi::BeginFrame → EditorApp::OnFrame(도킹·메뉴·패널) → DebugUi::EndFrame → Present]
//       를 오케스트레이션하며,
//   (3) IEditorViewport 를 구현해 ViewportPanel 에 오프스크린 RT 텍스처·좌표 변환을 제공한다.
//
// 플레이 tick 게이팅(07 §3): State()==Playing 일 때만 Play World 의 anim·transform 시스템을
//   tick 한다(Edit 상태면 렌더만). Paused 는 정지, StepFrame 요청 시 1프레임 진행.
//   ObjectSystem의 스크립트·물리·상호작용과 애니메이션을 같은 고정 틱에서 진행한다.
//
// 검증 CLI: --headless(창 없이), --frames N(N 프레임 후 종료), --dump path.bmp(오프스크린 RT 덤프).
//
// 셧다운 순서(데드락·수명 규약): EditorApp Shutdown → DebugUi Shutdown(윈도우/디바이스보다 먼저)
//   UI shuts down before GPU resources; each Document owns its edit World.
#include "mye/editor/EditorModule.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Viewport.h"

#include "mye/scene/RenderExtract.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"
#include "mye/ecs/World.h"

#include "mye/anim/AnimationSystem.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/Importer.h"
#include "mye/core/JsonFile.h"
#include <map>
#include <filesystem>

#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Input.h"
#include "mye/core/Log.h"
#include "mye/core/Window.h"
#include "mye/core/platform/Win32Window.h"

#include "mye/rhi/Rhi.h"

#include "mye/render/Camera2D.h"
#include "mye/render/PixelPerfectTarget.h"
#include "mye/render/HybridRenderer.h"

#include "mye/imgui/DebugUi.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace mye::editor {

namespace {
constexpr Color kViewportClear{0.09f, 0.10f, 0.13f, 1.0f};

// 창 없이 디바이스만으로 오프스크린 렌더할 때 쓰는 기본 뷰포트 크기(헤드리스 검증).
constexpr uint32_t kHeadlessW = 960;
constexpr uint32_t kHeadlessH = 540;
} // namespace

// -----------------------------------------------------------------------------
// EditorModule::Impl — 렌더 자원 소유 + IEditorViewport 구현
// -----------------------------------------------------------------------------
struct EditorModule::Impl final : public IEditorViewport {
    std::unique_ptr<EditorApp> app;
    std::string projectPath, templateDirectory;

    EngineContext*         engine = nullptr;
    InputState*            input = nullptr;

    // 렌더 자원.
    std::unique_ptr<rhi::IDevice>    device;
    std::unique_ptr<rhi::ISwapChain> swapChain;
    render::PixelPerfectTarget       rt;          // 오프스크린 뷰포트 RT(Sampled|RenderTarget)
    render::HybridRenderer           hybrid;
    mye::imgui::DebugUi              debugUi;
    scene::RenderProxyList           proxies;
    mye::ScopedSubscription          onResized;

    // 뷰포트 상태(패널이 통지, 렌더가 소비).
    // Handles are released before the manager and device.
    std::unique_ptr<asset::VirtualFileSystem> vfs;
    std::unique_ptr<asset::AssetManager> assets;
    std::unique_ptr<asset::AssetDatabase> assetDb;
    std::map<asset::AssetGuid, asset::AssetHandle<asset::Texture>> textures;
    std::map<asset::AssetGuid, asset::AnimationAsset> animations;
    std::string assetRoot;

    void ClearAssets() {
        if (engine) engine->UnregisterServiceRaw(asset::AssetDatabase::kServiceId);
        textures.clear();
        animations.clear();
        assetDb.reset();
        assets.reset();
        vfs.reset();
        assetRoot.clear();
    }
    Expected<void, Error> SyncAssets() {
        const auto root = Utf8String(Utf8Path(app->Project().RootDir()) / "assets");
        if (root == assetRoot) return {};
        ClearAssets();
        if (!app->Project().IsOpen()) return {};
        vfs = std::make_unique<asset::VirtualFileSystem>();
        vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(root), 0);
        assets = std::make_unique<asset::AssetManager>(*vfs, device.get());
        assets->RegisterImporter(std::make_unique<asset::TextureImporter>());
        assetDb = std::make_unique<asset::AssetDatabase>(*assets, nullptr);
        auto scanned = assetDb->ScanDirectory(root);
        if (!scanned) { ClearAssets(); assetRoot = root; return scanned.GetError(); }
        assetRoot = root;
        engine->RegisterServiceRaw(asset::AssetDatabase::kServiceId, assetDb.get());
        return {};
    }
    Expected<void, Error> RefreshAssetIndex() override {
        if (app->PlayMode().IsPlaying()) return Error{"Stop Play before refreshing asset files", 1};
        assetRoot.clear();
        auto synced = SyncAssets();
        if (!synced) return synced.GetError();
        return assetDb ? assetDb->ScanDirectory(assetRoot) : Expected<void, Error>(Error{"Open a project first", 1});
    }
    const asset::Texture* ResolveTexture(asset::AssetGuid guid) {
        if (!assetDb || !guid.IsValid()) return nullptr;
        auto found = textures.find(guid);
        if (found == textures.end()) {
            const auto path = assetDb->PathFromGuid(guid);
            if (path.empty()) return nullptr;
            const auto* importer = assets->FindImporterForPath(path);
            if (!importer || importer->ProducedType() != asset::Texture::kAssetTypeId) return nullptr;
            found = textures.emplace(guid, assets->LoadSync<asset::Texture>(path)).first;
        }
        return found->second.Get();
    }
    Expected<TexturePreview, Error> AssetTexture(asset::AssetGuid guid) override {
        const auto* texture = ResolveTexture(guid);
        if (!texture) return Error{"Texture could not be loaded; check its PNG and .meta", 1};
        return TexturePreview{device->GetImGuiTextureID(texture->gpuTexture), texture->width, texture->height};
    }
    const asset::AnimationAsset* ResolveAnimation(asset::AssetGuid guid) {
        if (!assetDb || !guid.IsValid()) return nullptr;
        const auto path = assetDb->PathFromGuid(guid);
        if (!path.ends_with(".anim")) return nullptr;
        const auto osPath = Utf8Path(assetRoot) / Utf8Path(path.substr(9));
        for (auto* doc : app->Project().Documents()) {
            if (doc->GetKind() != Document::Kind::Asset || Utf8Path(doc->Path()) != osPath) continue;
            return doc->Animation().Validate() ? &doc->Animation() : nullptr;
        }
        auto found = animations.find(guid);
        if (found == animations.end()) {
            auto value = ReadJsonFile(osPath);
            if (!value) { MYE_LOG_ERROR("Editor", "{}", value.GetError().message); return nullptr; }
            auto loaded = asset::AnimationAsset::FromJson(value.Value());
            if (!loaded) { MYE_LOG_ERROR("Editor", "{}", loaded.GetError().message); return nullptr; }
            found = animations.emplace(guid, std::move(loaded).Value()).first;
        }
        return &found->second;
    }
    void BindAnimations(ecs::World& world) {
        world.Query<anim::SpriteAnimator, scene::SpriteRenderer>().Each(
            [&](ecs::Entity, anim::SpriteAnimator& animator, scene::SpriteRenderer& sprite) {
                if (!animator.animation.guid.IsValid()) { animator.sheet = nullptr; animator.directClip = nullptr; return; }
                const auto* data = ResolveAnimation(animator.animation.guid);
                if (data && animator.playing && animator.cursor.finished && data->nextAnimation.guid.IsValid()) {
                    const auto* next = ResolveAnimation(data->nextAnimation.guid);
                    const auto* image = next ? ResolveTexture(next->sheet.texture.guid) : nullptr;
                    if (next && image && next->imageSize.x == static_cast<int32_t>(image->width) && next->imageSize.y == static_cast<int32_t>(image->height)) {
                        animator.animation = data->nextAnimation;
                        animator.cursor = {}; animator.started = false; data = next;
                    }
                }
                const auto* texture = data ? ResolveTexture(data->sheet.texture.guid) : nullptr;
                if (!data || !texture || data->imageSize.x != static_cast<int32_t>(texture->width) || data->imageSize.y != static_cast<int32_t>(texture->height)) {
                    animator.sheet = nullptr; animator.directClip = nullptr;
                    return;
                }
                if (animator.directClip != &data->clip) { animator.cursor = {}; animator.started = false; }
                animator.sheet = &data->sheet;
                animator.directClip = &data->clip;
                anim::UpdateAnimator(animator, 0.0f, &sprite, [](const asset::AnimEventMarker&) {});
            });
    }

    ViewportCamera vpCam{};
    uint32_t       vpWidth = kHeadlessW;
    uint32_t       vpHeight = kHeadlessH;

    // CLI.
    bool     headless = false;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    std::string dumpPath;
    uint64_t frameCount = 0;
    bool     dumped = false;
    rhi::TextureHandle dumpBackbuffer{};   // 창이 있으면 ImGui 셸까지 그려진 백버퍼를 덤프(스킨 확인용)
    std::function<void()> onExit;   // 프레임 한도 도달 시 호출(main 이 Application::RequestExit 배선).

    bool pendingInteract = false;

    // 플레이 게이팅.

    // ---- IEditorViewport ----
    void SetViewportSize(uint32_t w, uint32_t h) override {
        vpWidth = w == 0 ? 1 : w;
        vpHeight = h == 0 ? 1 : h;
    }
    void SetCamera(const ViewportCamera& c) override { vpCam = c; }
    ViewportCamera Camera() const override { return vpCam; }

    void* ColorTextureId() const override {
        if (!device || !rt.IsInitialized()) return nullptr;
        // rt.ColorTarget() 은 Sampled|RenderTarget — GetImGuiTextureID 로 SRV 래핑.
        return const_cast<rhi::IDevice*>(device.get())->GetImGuiTextureID(rt.ColorTarget());
    }
    uint32_t RenderWidth() const override { return rt.IsInitialized() ? rt.Width() : vpWidth; }
    uint32_t RenderHeight() const override { return rt.IsInitialized() ? rt.Height() : vpHeight; }

    // localPx: 뷰포트 이미지 좌상단 기준 픽셀(+Y 아래). 이미지는 RT 를 그대로 표시하므로
    //   이미지 픽셀 = RT 네이티브 픽셀 * (imageSize/RTsize) 스케일. Camera2D 는 RT 네이티브
    //   픽셀 기준으로 Screen↔World 를 정의하므로, 먼저 이미지→RT 네이티브 픽셀로 환산한다.
    Vec2 ScreenToWorld(Vec2 localPx) const override {
        const float sx = static_cast<float>(RenderWidth()) / std::max(vpWidth, 1u);
        const float sy = static_cast<float>(RenderHeight()) / std::max(vpHeight, 1u);
        return ViewportPointOnPlane(vpCam, {localPx.x * sx, localPx.y * sy}, RenderWidth(), RenderHeight());
    }
    Vec2 WorldToScreen(Vec2 world) const override { return WorldToScreen3D({world.x, world.y, 0}); }
    Vec2 WorldToScreen3D(Vec3 world) const override {
        const auto pixel = ProjectViewportPoint(vpCam, world, RenderWidth(), RenderHeight());
        return {pixel.x * std::max(vpWidth, 1u) / RenderWidth(), pixel.y * std::max(vpHeight, 1u) / RenderHeight()};
    }
};

// -----------------------------------------------------------------------------
EditorModule::EditorModule(std::string projectPath, std::string templateDirectory) : m_impl(std::make_unique<Impl>()) {
    m_impl->projectPath = std::move(projectPath);
    m_impl->templateDirectory = std::move(templateDirectory);
}
EditorModule::~EditorModule() = default;

std::span<const char* const> EditorModule::GetDependencies() const {
    static const std::array<const char*, 1> kDeps{ "SceneModule" };
    return kDeps;
}

void EditorModule::OnLoad(EngineContext& ctx) {
    ctx.RegisterServiceRaw(kServiceId, this);
}

void EditorModule::OnInitialize(EngineContext& ctx) {
    Impl& s = *m_impl;
    s.engine = &ctx;
    s.input = ctx.GetService<InputState>();

    // 프로젝트 경로는 --project(EnginePaths.projectDir). frames/dump/headless CLI 는
    //   main.cpp(MyEditorApp)가 파싱해 SetCliControl 로 주입한다(headless 는 창 유무로도 판정).
    if (s.projectPath.empty()) s.projectPath = ctx.GetPaths().projectDir;

    // 디바이스 생성(창이 없어도 오프스크린 렌더용으로 생성 — 헤드리스 덤프 지원).
    auto deviceResult = rhi::CreateDevice(rhi::Backend::DX11, {});
    if (!deviceResult) {
        MYE_LOG_ERROR("Editor", "RHI 디바이스 생성 실패: {}", deviceResult.GetError().message);
    } else {
        s.device = std::move(deviceResult).Value();
    }

    // 창이 있으면 스왑체인 + DebugUi 초기화.
    IWindow* window = ctx.GetServiceRaw(kMainWindowServiceId)
                          ? &ctx.MainWindow() : nullptr;
    s.headless = (window == nullptr);

    if (s.device && window) {
        rhi::SwapChainDesc scDesc{};
        scDesc.width = 0; scDesc.height = 0;
        // Imported pixel colors already use UNORM; sRGB output would brighten the viewport again.
        scDesc.format = rhi::Format::BGRA8Unorm;
        s.swapChain = s.device->CreateSwapChain(window->GetNativeHandle(), scDesc);
        if (!s.swapChain)
            MYE_LOG_ERROR("Editor", "스왑체인 생성 실패");

        auto& win32Window = static_cast<mye::win32::Win32Window&>(*window);
        auto uiRes = s.debugUi.Initialize(win32Window, *s.device);
        if (!uiRes)
            MYE_LOG_ERROR("Editor", "DebugUi 초기화 실패: {}", uiRes.GetError().message);
        else if (s.input)
            s.debugUi.AttachInput(s.input);

        s.onResized = mye::ScopedSubscription{
            ctx.Events(),
            ctx.Events().Subscribe<mye::WindowResizedEvent>([this](const mye::WindowResizedEvent& e) {
                if (m_impl->swapChain && e.clientSize.x > 0 && e.clientSize.y > 0)
                    m_impl->swapChain->Resize(static_cast<uint32_t>(e.clientSize.x),
                                              static_cast<uint32_t>(e.clientSize.y));
                return false;
            })};
    }

    // 오프스크린 뷰포트 RT + 하이브리드 렌더러(디바이스가 있을 때만).
    if (s.device) {
        render::PixelPerfectDesc ppd{};
        ppd.internalWidth = kHeadlessW;
        ppd.internalHeight = kHeadlessH;
        ppd.useDepth = true;
        ppd.backbufferFormat = s.swapChain ? s.swapChain->GetFormat() : rhi::Format::BGRA8Unorm;
        s.rt.Init(*s.device, ppd);

        render::HybridRendererDesc hrd{};
        hrd.colorFormat = s.rt.ColorFormat();
        hrd.depthFormat = rhi::Format::D24UnormS8Uint;
        hrd.alphaCutoff = 0.5f;
        s.hybrid.Init(*s.device, hrd);
        s.hybrid.SetTextureResolver([](void* user, asset::AssetGuid guid) {
            return static_cast<Impl*>(user)->ResolveTexture(guid);
        }, &s);
    }

    s.app = std::make_unique<EditorApp>();
}

void EditorModule::OnPostInitialize(EngineContext& ctx) {
    Impl& s = *m_impl;
    if (s.app) {
        auto r = s.app->Initialize(ctx, s.projectPath, s.templateDirectory);
        if (!r) MYE_LOG_ERROR("Editor", "EditorApp 초기화 실패: {}", r.GetError().message);

        s.app->RefreshDocumentContext();

        // 뷰포트 렌더러 배선(ViewportPanel 이 ctx.app->Viewport() 로 접근).
        s.app->SetViewport(&s);
    }

    // 시뮬레이션은 고정 틱, 렌더·ImGui는 표현 단계에서 처리한다.
    ctx.Modules().AddTick(this, UpdatePhase::PreUpdate, [this](const TimeStep&) {
        auto& state = *m_impl;
        if (state.input && state.app && state.app->PlayMode().InputEnabled() && state.app->PlayMode().State() == PlayState::Playing)
            state.pendingInteract |= state.input->WasPressed(KeyCode::E);
        else state.pendingInteract = false;
    }, 100);
    ctx.Modules().AddTick(this, UpdatePhase::FixedUpdate, [this](const TimeStep& t) {
        auto& state = *m_impl;
        if (!state.app || !state.device) return;
        state.app->RefreshDocumentContext();
        auto synced = state.SyncAssets();
        if (!synced) MYE_LOG_ERROR("Editor", "{}", synced.GetError().message);
        if (auto* world = state.app->PlayMode().ActiveWorld()) state.BindAnimations(*world);
        TickPlayWorld(t);
    }, 100);
    ctx.Modules().AddTick(this, UpdatePhase::PreRender,
                          [this](const TimeStep& t) { Frame(t); }, /*orderKey*/ 100);
}

// 플레이 tick 게이팅(07 §3): Playing 이면 Play World 의 anim·transform 을 진행.
void EditorModule::TickPlayWorld(const TimeStep& step) {
    Impl& s = *m_impl;
    if (!s.app) return;
    PlayModeController& pm = s.app->PlayMode();
    const PlayState st = pm.State();

    bool doTick = false;
    if (st == PlayState::Playing) doTick = true;
    else if (st == PlayState::Paused && pm.ConsumeStepRequest()) doTick = true;

    if (!doTick) return;
    ecs::World* w = pm.ActiveWorld();
    if (!w) return;

    const float dt = static_cast<float>(step.deltaSeconds > 0.0 ? step.deltaSeconds : (1.0 / 60.0));
    Vec2 movement;
    if (s.input && pm.InputEnabled()) {
        movement.x = static_cast<float>(s.input->IsDown(KeyCode::D) || s.input->IsDown(KeyCode::Right)) - static_cast<float>(s.input->IsDown(KeyCode::A) || s.input->IsDown(KeyCode::Left));
        movement.y = static_cast<float>(s.input->IsDown(KeyCode::W) || s.input->IsDown(KeyCode::Up)) - static_cast<float>(s.input->IsDown(KeyCode::S) || s.input->IsDown(KeyCode::Down));
    }
    auto tick = pm.Tick(dt, movement, std::exchange(s.pendingInteract, false), s.app->Project().RootDir());
    if (!tick) MYE_LOG_ERROR("Editor", "{}", tick.GetError().message);
    w = pm.ActiveWorld();
    s.app->RefreshDocumentContext();
    s.BindAnimations(*w);
    // Fixed tick: Lua/controls -> collision/events -> animation -> transforms.
    anim::RunAnimationSystem(*w, dt);
    scene::UpdateWorldTransforms(*w);
}

void EditorModule::Frame(const TimeStep&) {
    Impl& s = *m_impl;
    if (!s.app || !s.device) return;
    s.app->RefreshDocumentContext();

    auto synced = s.SyncAssets();
    if (!synced) MYE_LOG_ERROR("Editor", "{}", synced.GetError().message);
    if (auto* world = s.app->PlayMode().ActiveWorld()) s.BindAnimations(*world);

    // 2) 활성 World 추출.
    ecs::World* world = s.app->PlayMode().ActiveWorld();
    if (world) {
        scene::UpdateWorldTransforms(*world);
        scene::ExtractRenderItems(*world, s.proxies);
    } else {
        s.proxies.Clear();
    }

    // 3) 프레임 렌더.
    s.device->BeginFrame();
    rhi::ICommandContext& cmd = s.device->GetImmediateContext();

    // 3a) 오프스크린 뷰포트 RT 에 씬 렌더.
    if (s.rt.IsInitialized()) {
        const auto view = BuildViewportView(s.vpCam, s.rt.Width(), s.rt.Height());
        s.rt.BeginScenePass(cmd, kViewportClear);
        s.hybrid.Render(s.proxies, view, cmd);
        s.rt.EndScenePass(cmd);
    }

    // 3b) 백버퍼에 ImGui 셸(도킹·메뉴·패널) — 창이 있을 때만.
    rhi::TextureHandle backbuffer{};
    const bool haveWindow = s.swapChain && s.debugUi.IsInitialized();
    if (haveWindow) {
        backbuffer = s.swapChain->GetCurrentBackBuffer();
        const Vec2i winSize = s.swapChain->GetSize();

        // 백버퍼 클리어(도킹 배경).
        rhi::RenderPassColorAttachment clearColor{};
        clearColor.texture = backbuffer;
        clearColor.loadOp = rhi::LoadOp::Clear;
        clearColor.clearColor = Color{0.05f, 0.05f, 0.06f, 1.0f};
        rhi::RenderPassBeginDesc clearPass{};
        clearPass.colorAttachments = {&clearColor, 1};
        clearPass.debugName = "editor.clear";
        cmd.BeginRenderPass(clearPass);
        cmd.EndRenderPass();

        // ImGui 프레임: 도킹·메뉴·패널 빌드 → 백버퍼에 렌더.
        s.debugUi.BeginFrame();
        s.app->OnFrame();

        rhi::RenderPassColorAttachment uiColor{};
        uiColor.texture = backbuffer;
        uiColor.loadOp = rhi::LoadOp::Load;
        rhi::RenderPassBeginDesc uiPass{};
        uiPass.colorAttachments = {&uiColor, 1};
        uiPass.debugName = "editor.imgui";
        cmd.BeginRenderPass(uiPass);
        rhi::Viewport uiVp{};
        uiVp.width = static_cast<float>(winSize.x);
        uiVp.height = static_cast<float>(winSize.y);
        uiVp.maxDepth = 1.0f;
        cmd.SetViewport(uiVp);
        s.debugUi.EndFrame();
        cmd.EndRenderPass();
    }

    ++s.frameCount;

    // 4) 덤프. 창이 있으면 ImGui 셸(스킨)까지 그려진 백버퍼를, 헤드리스면 오프스크린 RT 를 캡처.
    s.dumpBackbuffer = haveWindow ? backbuffer : rhi::TextureHandle{};
    HandleDump();

    if (haveWindow) {
        s.swapChain->Present(s.app ? s.app->Vsync() : false);
    }
    s.device->EndFrame();

    // 5) 프레임 한도(자동 검증).
    if (s.frameLimit && s.frameCount >= s.maxFrames)
        RequestExit();
}

void EditorModule::HandleDump() {
    Impl& s = *m_impl;
    if (!s.dumpEnabled || s.dumped) return;
    const bool last = s.frameLimit ? (s.frameCount >= s.maxFrames) : (s.frameCount >= 3);
    if (!last) return;
    if (!s.device) return;
    // 창이 있으면 ImGui 셸(스킨/한글)까지 그려진 백버퍼를, 헤드리스면 오프스크린 RT 를 캡처.
    rhi::TextureHandle target = s.dumpBackbuffer.IsValid() ? s.dumpBackbuffer
                                                           : (s.rt.IsInitialized() ? s.rt.ColorTarget() : rhi::TextureHandle{});
    if (!target.IsValid()) return;
    auto cap = rhi::CaptureBackbuffer(*s.device, target, s.dumpPath);
    if (cap) MYE_LOG_INFO("Editor", "프레임 덤프 -> '{}' (frame {})", s.dumpPath, s.frameCount);
    else     MYE_LOG_ERROR("Editor", "덤프 실패: {}", cap.GetError().message);
    s.dumped = true;
}

void EditorModule::RequestExit() {
    // Application::RequestExit 은 core 서비스로 노출되지 않으므로, main.cpp 가 SetCliControl 로
    //   넘긴 종료 콜백(캡처한 Application*)을 호출해 메인 루프를 탈출시킨다(헤드리스·프레임 한도).
    if (m_impl->onExit) m_impl->onExit();
}

void EditorModule::OnShutdown(EngineContext& ctx) {
    Impl& s = *m_impl;
    // 셧다운 순서: EditorApp → DebugUi → 렌더 타깃/렌더러 → 스왑체인 → 디바이스.
    if (s.app) s.app->Shutdown();
    s.app.reset();

    s.onResized.Reset();
    s.debugUi.Shutdown();
    s.hybrid.Shutdown();
    s.rt.Shutdown();
    s.proxies.Clear();
    s.ClearAssets();
    s.swapChain.reset();
    s.device.reset();

    ctx.UnregisterServiceRaw(kServiceId);
}

void EditorModule::SetPerspectiveView(bool enabled) { m_impl->vpCam.perspective = enabled; }

EditorApp* EditorModule::App() { return m_impl->app.get(); }

// CLI 제어 배선(main.cpp 가 호출) — frames/dump/headless + 종료 콜백.
void EditorModule::SetCliControl(bool frameLimit, uint64_t maxFrames, bool dumpEnabled,
                                 std::string dumpPath, std::function<void()> onExit) {
    Impl& s = *m_impl;
    s.frameLimit = frameLimit;
    s.maxFrames = maxFrames;
    s.dumpEnabled = dumpEnabled;
    s.dumpPath = std::move(dumpPath);
    s.onExit = std::move(onExit);
}

void EditorModule::RequestStepFrame() { if (m_impl->app) m_impl->app->PlayMode().StepFrame(); }

} // namespace mye::editor
