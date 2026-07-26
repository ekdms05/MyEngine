// EditorModule.cpp — 에디터 실앱 부팅 + 프레임 오케스트레이션 + 씬 뷰포트 렌더 + 플레이 tick 게이팅
//                                                            (docs/07 §1·§3, 씬 뷰포트)
// 07 §1: 게임과 동일한 모듈 스택 위에 얹히는 EditorModule. 이 모듈이:
//   (1) RHI 디바이스·스왑체인·mye_imgui DebugUi(도킹 셸)를 소유·초기화하고,
//   (2) PreRender 틱에서 [플레이 tick 게이팅] → [활성 World를 오프스크린 RT에 렌더] →
//       [DebugUi::BeginFrame → EditorApp::OnFrame(도킹·메뉴·패널) → DebugUi::EndFrame → Present]
//       를 오케스트레이션하며,
//   (3) IEditorViewport 를 구현해 ViewportPanel 에 오프스크린 RT 텍스처·좌표 변환을 제공한다.
//
// 플레이 tick 게이팅(07 §3): State()==Playing 일 때만 Play World 의 anim·transform 시스템을
//   tick 한다(Edit 상태면 렌더만). Paused 는 정지, StepFrame 요청 시 1프레임 진행.
//   (스크립트·물리 런타임 tick 은 ScriptModule/PhysicsWorld 배선이 필요 — apiForNext 참조.)
//
// 검증 CLI: --headless(창 없이), --frames N(N 프레임 후 종료), --dump path.bmp(오프스크린 RT 덤프).
//
// 셧다운 순서(데드락·수명 규약): EditorApp Shutdown → DebugUi Shutdown(윈도우/디바이스보다 먼저)
//   → 렌더 타깃/렌더러 → 스왑체인 → 디바이스. World 는 SceneModule 소유(여기서 파괴 금지).
#include "mye/editor/EditorModule.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Viewport.h"

#include "mye/scene/SceneModule.h"
#include "mye/scene/RenderExtract.h"
#include "mye/scene/Transform.h"

#include "mye/anim/AnimationSystem.h"

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
    std::string projectPath;

    EngineContext*         engine = nullptr;
    scene::SceneModule*    sceneModule = nullptr;
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

    // 플레이 게이팅.
    bool stepRequested = false;

    // ---- Camera2D 산출(현재 뷰포트 크기·카메라 기준) ----
    render::Camera2D BuildCamera() const {
        render::Camera2DDesc d{};
        d.position = vpCam.center;
        d.zoom = vpCam.zoom;
        d.pixelSnap = true;
        d.viewportWidth = rt.IsInitialized() ? rt.Width() : vpWidth;
        d.viewportHeight = rt.IsInitialized() ? rt.Height() : vpHeight;
        return render::Camera2D(d);
    }

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
        render::Camera2D cam = BuildCamera();
        const float sx = static_cast<float>(RenderWidth()) / static_cast<float>(std::max<uint32_t>(vpWidth, 1));
        const float sy = static_cast<float>(RenderHeight()) / static_cast<float>(std::max<uint32_t>(vpHeight, 1));
        return cam.ScreenToWorld(Vec2{localPx.x * sx, localPx.y * sy});
    }
    Vec2 WorldToScreen(Vec2 world) const override {
        render::Camera2D cam = BuildCamera();
        const Vec2 nativePx = cam.WorldToScreen(world);
        const float sx = static_cast<float>(std::max<uint32_t>(vpWidth, 1)) / static_cast<float>(RenderWidth());
        const float sy = static_cast<float>(std::max<uint32_t>(vpHeight, 1)) / static_cast<float>(RenderHeight());
        return Vec2{nativePx.x * sx, nativePx.y * sy};
    }
};

// -----------------------------------------------------------------------------
EditorModule::EditorModule() : m_impl(std::make_unique<Impl>()) {}
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
    s.projectPath = ctx.GetPaths().projectDir;

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
        scDesc.format = rhi::Format::BGRA8UnormSrgb;
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
        ppd.backbufferFormat = s.swapChain ? s.swapChain->GetFormat() : rhi::Format::BGRA8UnormSrgb;
        s.rt.Init(*s.device, ppd);

        render::HybridRendererDesc hrd{};
        hrd.colorFormat = s.rt.ColorFormat();
        hrd.depthFormat = rhi::Format::D24UnormS8Uint;
        hrd.alphaCutoff = 0.5f;
        s.hybrid.Init(*s.device, hrd);
        // 텍스처 해석기: 에디터 에셋 파이프라인(에셋 브라우저 에이전트)과 공유 예정.
        //   M4-B 는 미배선 — 텍스처 미해석 스프라이트는 렌더 스킵(그리드·기즈모·픽킹은 정상 동작).
    }

    s.app = std::make_unique<EditorApp>();
}

void EditorModule::OnPostInitialize(EngineContext& ctx) {
    Impl& s = *m_impl;
    if (s.app) {
        auto r = s.app->Initialize(ctx, s.projectPath);
        if (!r) MYE_LOG_ERROR("Editor", "EditorApp 초기화 실패: {}", r.GetError().message);

        // 07 §3: 편집 World = SceneModule 의 active World.
        s.sceneModule = ctx.GetService<scene::SceneModule>();
        if (s.sceneModule)
            s.app->PlayMode().SetEditWorld(&s.sceneModule->World());

        // 뷰포트 렌더러 배선(ViewportPanel 이 ctx.app->Viewport() 로 접근).
        s.app->SetViewport(&s);
    }

    // PreRender 틱: 플레이 게이팅 → 오프스크린 씬 렌더 → ImGui 프레임 → Present.
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
    else if (st == PlayState::Paused && s.stepRequested) { doTick = true; s.stepRequested = false; }

    if (!doTick) return;
    ecs::World* w = pm.ActiveWorld();
    if (!w) return;

    const float dt = static_cast<float>(step.deltaSeconds > 0.0 ? step.deltaSeconds : (1.0 / 60.0));
    // anim(상태머신·클립 샘플·이벤트) → transform(월드 행렬). 스크립트·물리는 별도 배선 필요.
    anim::RunAnimationSystem(*w, dt);
    scene::UpdateWorldTransforms(*w);
}

void EditorModule::Frame(const TimeStep& step) {
    Impl& s = *m_impl;
    if (!s.app || !s.device) return;

    // 1) 플레이 tick 게이팅.
    TickPlayWorld(step);

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
        render::Camera2D cam = s.BuildCamera();
        s.rt.BeginScenePass(cmd, kViewportClear);
        s.hybrid.Render(s.proxies, cam, cmd);
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
    s.swapChain.reset();
    s.device.reset();

    ctx.UnregisterServiceRaw(kServiceId);
}

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

void EditorModule::RequestStepFrame() { m_impl->stepRequested = true; }

} // namespace mye::editor
