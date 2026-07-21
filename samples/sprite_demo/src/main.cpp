// sprite_demo — M1 통합 데모·검증 (docs/00 §7 M1 완료 기준)
//
// 목표: 정수배 스케일 창에서 도트 캐릭터(hero.png)가 WASD로 부드럽게(60Hz 고정 시뮬 +
//       렌더 보간) 이동, 지터·텍셀 번짐 없음, ImGui 오버레이에 FPS·드로우콜, 카메라 추적 시
//       픽셀 그리드 유지.
//
// 관통 계층: GuardedMain · Application · ModuleRegistry · Config · RHI(DX11) · InputState ·
//            AssetManager(assets:// PNG) · mye_render(Camera2D/SpriteBatch/PixelPerfectTarget) ·
//            mye_imgui(DebugUi).
//
// 렌더 파이프라인(PreRender, alpha 보간 후):
//   device->BeginFrame();
//   cam.SetPosition(스냅된 카메라 추적 위치);
//   rt.BeginScenePass(cmd, clear); batch.Begin(cam.VP()); Submit(배경 타일)×N + Submit(hero);
//   batch.End(cmd); rt.EndScenePass(cmd); rt.Blit(cmd, backbuffer, windowSize, cam.SubpixelResidual());
//   debugUi.BeginFrame()..EndFrame() (blit된 백버퍼 위 오버레이); Present; EndFrame();
//
// 자동 검증 CLI:
//   --frames N            N 렌더 프레임 후 exit 0
//   --dump path.bmp       마지막 프레임 백버퍼를 BMP 저장
//   --move-test           입력 없이 코드로 캐릭터를 +X로 이동. 프레임 A(초기)·B(이동 후)를 dumpA/dumpB 저장.
//   --dumpA path / --dumpB path   move-test 두 스냅샷 경로(기본 dumpA.bmp/dumpB.bmp)
//   --resize-test         실행 중 창 2회 리사이즈
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

#include "mye/render/Camera2D.h"
#include "mye/render/SpriteBatch.h"
#include "mye/render/SpriteRenderer.h"
#include "mye/render/PixelPerfectTarget.h"

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
#include <vector>

namespace {

namespace rhi = mye::rhi;
namespace asset = mye::asset;
namespace render = mye::render;

using mye::Vec2;
using mye::Vec2i;
using mye::Color;

// 이동 속도(unit/s). PPU 48 → 4 unit/s = 192 px/s.
constexpr float kMoveSpeed = 4.0f;

// 배경 격자 타일: 체커보드 색 두 종.
constexpr Color kClearColor{0.06f, 0.07f, 0.10f, 1.0f};
constexpr Color kTileA{0.16f, 0.18f, 0.24f, 1.0f};
constexpr Color kTileB{0.22f, 0.25f, 0.33f, 1.0f};

struct RunControl {
    mye::Application* app = nullptr;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    std::string dumpPath;
    bool     moveTest = false;
    std::string dumpAPath = "dumpA.bmp";
    std::string dumpBPath = "dumpB.bmp";
    bool     resizeTest = false;
};

// 1x1 흰색 텍스처(배경 타일용 — 틴트로 색 지정). 셰이더가 텍스처×틴트라 흰 텍셀이 필요.
rhi::TextureHandle CreateWhiteTexture(rhi::IDevice& device) {
    const uint32_t white = 0xFFFFFFFFu;   // RGBA8 premultiplied white
    rhi::TextureDesc d{};
    d.dim = rhi::TextureDim::Tex2D;
    d.format = rhi::Format::RGBA8Unorm;
    d.width = 1;
    d.height = 1;
    d.usage = rhi::TextureUsage::Sampled;
    d.debugName = "demo.white";
    rhi::TextureInitData init{};
    init.data = &white;
    init.rowPitch = 4;
    return device.CreateTexture(d, &init);
}

class SpriteDemoModule final : public mye::IModule {
public:
    explicit SpriteDemoModule(RunControl* control) : m_control(control) {}

    const char* GetName() const override { return "SpriteDemo"; }

    void OnInitialize(mye::EngineContext& ctx) override {
        m_ctx = &ctx;
        m_bus = &ctx.Events();
        m_input = ctx.GetService<mye::InputState>();

        mye::IWindow& window = ctx.MainWindow();

        // 1) 디바이스
        auto deviceResult = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!deviceResult) {
            MYE_LOG_FATAL("SpriteDemo", "CreateDevice failed: {}", deviceResult.GetError().message);
            m_control->app->RequestExit(-10);
            return;
        }
        m_device = std::move(deviceResult).Value();

        // 2) 스왑체인 (창 클라이언트 크기, 최종 sRGB 백버퍼)
        rhi::SwapChainDesc scDesc{};
        scDesc.width = 0;
        scDesc.height = 0;
        scDesc.format = rhi::Format::BGRA8UnormSrgb;
        m_swapChain = m_device->CreateSwapChain(window.GetNativeHandle(), scDesc);
        if (!m_swapChain) {
            MYE_LOG_FATAL("SpriteDemo", "CreateSwapChain failed");
            m_control->app->RequestExit(-11);
            return;
        }

        // 3) VFS 마운트 + AssetManager (assets:// → samples/sprite_demo/assets)
        //    작업 디렉터리 기준 상대 경로들을 순차 시도(빌드 실행 위치 다양성 대응).
        m_vfs = std::make_unique<asset::VirtualFileSystem>();
        std::string assetsDir = ResolveAssetsDir();
        m_vfs->Mount("assets", std::make_unique<asset::LooseFileSystem>(assetsDir), 0);
        m_assets = std::make_unique<asset::AssetManager>(*m_vfs, m_device.get());
        m_assets->RegisterImporter(std::make_unique<asset::TextureImporter>());

        // 4) hero.png 로드
        m_hero = m_assets->LoadSync<asset::Texture>("assets://hero.png");
        if (m_hero.State() != asset::AssetState::Loaded || !m_hero.Get()) {
            MYE_LOG_FATAL("SpriteDemo", "hero.png load failed (dir='{}', state={})",
                          assetsDir, static_cast<int>(m_hero.State()));
            m_control->app->RequestExit(-12);
            return;
        }
        {
            asset::Texture* t = m_hero.Get();
            MYE_LOG_INFO("SpriteDemo", "hero.png loaded {}x{}", t->width, t->height);
        }

        // 5) 배경 타일용 흰 텍스처
        m_white = CreateWhiteTexture(*m_device);

        // 6) 픽셀퍼펙트 RT + 스프라이트 배처
        render::PixelPerfectDesc ppd{};
        ppd.backbufferFormat = m_swapChain->GetFormat();
        m_rt.Init(*m_device, ppd);
        m_batch.Init(*m_device, m_rt.ColorFormat(), m_rt.DepthEnabled());
        if (!m_rt.IsInitialized() || !m_batch.IsInitialized()) {
            MYE_LOG_FATAL("SpriteDemo", "render init failed (rt={} batch={})",
                          m_rt.IsInitialized(), m_batch.IsInitialized());
            m_control->app->RequestExit(-13);
            return;
        }

        // 7) 카메라 (픽셀 스냅, 내부 960x540 뷰포트 기본)
        render::Camera2DDesc camDesc{};
        camDesc.position = {0.0f, 0.0f};
        camDesc.pixelSnap = true;
        m_camera = render::Camera2D(camDesc);

        // 8) ImGui 오버레이. MainWindow는 IWindow&지만 실제 Win32Window — static_cast(규약).
        auto& win32Window = static_cast<mye::win32::Win32Window&>(window);
        auto uiRes = m_debugUi.Initialize(win32Window, *m_device);
        if (!uiRes) {
            MYE_LOG_ERROR("SpriteDemo", "DebugUi init failed: {}", uiRes.GetError().message);
            // 오버레이 없이도 데모는 진행(픽셀 검증은 씬 렌더로 가능).
        } else {
            // 입력 선점 배선: ImGui 캡처 시 게임 입력(WASD 등) 억제 — Raw Input 경로 포함.
            m_debugUi.AttachInput(m_input);
        }

        // 리사이즈 → 스왑체인만 재구성(내부 RT는 고정 960x540, Blit이 정수배 재산출).
        m_onResized = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<mye::WindowResizedEvent>([this](const mye::WindowResizedEvent& e) {
                if (m_swapChain && e.clientSize.x > 0 && e.clientSize.y > 0) {
                    m_swapChain->Resize(static_cast<uint32_t>(e.clientSize.x),
                                        static_cast<uint32_t>(e.clientSize.y));
                    MYE_LOG_INFO("SpriteDemo", "swapchain resized to {}x{}",
                                 e.clientSize.x, e.clientSize.y);
                }
                return false;
            })};

        MYE_LOG_INFO("SpriteDemo", "SpriteDemoModule initialized (assetsDir='{}')", assetsDir);
    }

    void OnPostInitialize(mye::EngineContext& ctx) override {
        // 고정 스텝: 60Hz에서 속도 적분(결정성 이동).
        ctx.Modules().AddTick(this, mye::UpdatePhase::FixedUpdate,
                              [this](const mye::TimeStep& step) { FixedUpdate(step); }, 0);
        // Update(프레임당 1회): 엣지성 입력(ESC 종료) 소비.
        ctx.Modules().AddTick(this, mye::UpdatePhase::Update,
                              [this](const mye::TimeStep& step) { Update(step); }, 0);
        // 렌더: alpha 보간 후 그림.
        ctx.Modules().AddTick(this, mye::UpdatePhase::PreRender,
                              [this](const mye::TimeStep& step) { Render(step); }, 0);
    }

    void OnShutdown(mye::EngineContext& /*ctx*/) override {
        m_onResized.Reset();
        m_debugUi.Shutdown();
        m_batch.Shutdown();
        m_rt.Shutdown();
        if (m_device && m_white.IsValid()) m_device->Destroy(m_white);
        m_hero = {};
        m_assets.reset();
        m_vfs.reset();
        m_swapChain.reset();
        m_device.reset();
        MYE_LOG_INFO("SpriteDemo", "SpriteDemoModule shutdown ({} frames)", m_frameCount);
    }

private:
    std::string ResolveAssetsDir() const {
        namespace fs = std::filesystem;
        auto has = [](const fs::path& dir) {
            std::error_code ec;
            return fs::exists(dir / "hero.png", ec);
        };
        // 1) exe 위치에서 상위로 걸어 올라가며 samples/sprite_demo/assets 탐색.
        wchar_t exePathW[MAX_PATH] = {};
        if (::GetModuleFileNameW(nullptr, exePathW, MAX_PATH) > 0) {
            fs::path dir = fs::path(exePathW).parent_path();
            for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
                const fs::path cand = dir / "samples" / "sprite_demo" / "assets";
                if (has(cand)) return cand.string();
                if (has(dir / "assets")) return (dir / "assets").string();
                dir = dir.parent_path();
            }
        }
        // 2) 작업 디렉터리 상대 후보(폴백).
        const char* candidates[] = {
            "samples/sprite_demo/assets",
            "../samples/sprite_demo/assets",
            "../../samples/sprite_demo/assets",
            "../../../samples/sprite_demo/assets",
            "assets",
        };
        for (const char* c : candidates) {
            if (has(fs::path(c))) return c;
        }
        return "samples/sprite_demo/assets";
    }

    // 고정 60Hz: 입력(또는 move-test)으로 속도 적분. 이전 위치 저장(렌더 보간용).
    void FixedUpdate(const mye::TimeStep& step) {
        m_prevPos = m_curPos;
        const float dt = static_cast<float>(step.deltaSeconds);

        Vec2 dir{0.0f, 0.0f};
        if (m_control->moveTest) {
            // 입력 없이 결정적으로 +X 이동(카메라 추적·이동 증명).
            dir = {1.0f, 0.0f};
        } else if (m_input) {
            if (m_input->IsDown(mye::KeyCode::W)) dir.y += 1.0f;  // 월드 +Y 위
            if (m_input->IsDown(mye::KeyCode::S)) dir.y -= 1.0f;
            if (m_input->IsDown(mye::KeyCode::A)) dir.x -= 1.0f;
            if (m_input->IsDown(mye::KeyCode::D)) dir.x += 1.0f;
        }
        // 대각선 정규화
        if (dir.x != 0.0f || dir.y != 0.0f) {
            const float len = dir.Length();
            dir = dir / len;
            m_curPos += dir * (kMoveSpeed * dt);
        }
    }

    // 엣지성 입력(WasPressed/WasReleased)은 프레임당 정확히 1회 관측이 보장되는 Update 페이즈에서
    // 소비한다. FixedUpdate는 한 렌더 프레임에 0/1/2회 이상 실행될 수 있어(고정스텝 누산기) 엣지가
    // 유실·중복될 수 있으므로 ESC 종료 판정을 여기로 둔다. 지속 입력(IsDown 이동)은 FixedUpdate 유지.
    void Update(const mye::TimeStep& /*step*/) {
        if (m_input && m_input->WasPressed(mye::KeyCode::Escape)) {
            MYE_LOG_INFO("SpriteDemo", "ESC pressed — requesting exit");
            m_control->app->RequestExit(0);
        }
    }

    void SubmitBackgroundTiles() {
        // 카메라 주변 격자 체커보드(픽셀 그리드·이동 확인용). 1 unit = 1 타일.
        // 화면 절반(unit): 960/48/2=10 가로, 540/48/2=5.625 세로 → 여유로 ±12, ±8.
        const Vec2 camPos = m_camera.Position();
        const int cx = static_cast<int>(std::floor(camPos.x));
        const int cy = static_cast<int>(std::floor(camPos.y));
        for (int ty = cy - 8; ty <= cy + 8; ++ty) {
            for (int tx = cx - 12; tx <= cx + 12; ++tx) {
                render::SpriteDraw s{};
                s.texture = m_white;
                s.position = {static_cast<float>(tx), static_cast<float>(ty)};
                s.size = {1.0f, 1.0f};
                s.pivot = {0.0f, 0.0f};   // 좌하단 앵커 → 타일 정렬
                const bool even = ((tx + ty) & 1) == 0;
                s.tint = even ? kTileA : kTileB;
                s.sortY = -1000.0f;       // 배경은 항상 뒤
                m_batch.Submit(s);
            }
        }
    }

    void Render(const mye::TimeStep& step) {
        if (!m_device || !m_swapChain || !m_rt.IsInitialized()) return;

        // 렌더 보간: 고정 스텝 간 alpha로 이전/현재 위치 lerp.
        const float alpha = static_cast<float>(m_ctx->Time().GetInterpolationAlpha());
        const Vec2 renderPos = Vec2::Lerp(m_prevPos, m_curPos, alpha);

        m_device->BeginFrame();

        // 카메라가 캐릭터 추적(픽셀 스냅은 카메라가 내부에서 수행).
        m_camera.SetPosition(renderPos);

        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        // --- 씬: 내부 RT에 배경 타일 + hero ---
        m_rt.BeginScenePass(cmd, kClearColor);
        m_batch.Begin(m_camera.ViewProjection());

        SubmitBackgroundTiles();

        // hero: 발밑 피벗(0.5,1). 크기·UV 정규화는 통합 헬퍼(SpriteRenderer)가 수행 —
        // 텍스처픽셀/PPU·UV 정규화 규약을 데모가 재구현하지 않는다.
        asset::Texture* tex = m_hero.Get();
        if (tex) {
            render::SpriteDraw s = render::MakeSprite(tex, renderPos, {0.5f, 1.0f});
            if (s.texture.IsValid()) m_batch.Submit(s);
        }

        m_batch.End(cmd);
        m_rt.EndScenePass(cmd);

        // --- 업스케일 블릿(정수배 레터박스 + 서브픽셀 잔차 UV 오프셋) ---
        const Vec2i winSize = m_swapChain->GetSize();
        rhi::TextureHandle backbuffer = m_swapChain->GetCurrentBackBuffer();
        m_rt.Blit(cmd, backbuffer, winSize, m_camera.SubpixelResidual());

        // 통계
        const uint32_t drawCalls = m_batch.DrawCallCount();
        const uint32_t sprites = m_batch.QuadCount();
        const render::UpscaleLayout layout = render::PixelPerfectTarget::ComputeLayout(
            {static_cast<int32_t>(m_rt.Width()), static_cast<int32_t>(m_rt.Height())}, winSize);

        // --- ImGui 오버레이(blit된 백버퍼 위) ---
        // Blit이 자체 RenderPass를 EndRenderPass로 닫아 백버퍼 RTV가 언바인드된다.
        // ImGui RenderDrawData는 현재 바인딩된 RTV에 그리므로, 백버퍼를 LoadOp::Load로 다시
        // 열어(블릿 결과 보존) 전체 창 뷰포트를 설정한 뒤 EndFrame()으로 오버레이를 얹는다.
        // 불변식: BeginFrame(ImGui::NewFrame)~EndFrame(ImGui::Render) 사이에 조기 return/예외가
        // 없어야 ImGui 프레임 짝이 유지된다. 아래 로직은 위젯 호출·렌더패스 설정뿐이라 안전하다.
        if (m_debugUi.IsInitialized()) {
            m_debugUi.BeginFrame();
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("Debug");
            ImGui::Text("FPS: %.1f", m_fps);
            ImGui::Text("Fixed steps/s: %llu", static_cast<unsigned long long>(m_fixedPerSec));
            ImGui::Text("Draw calls: %u", drawCalls);
            ImGui::Text("Sprites: %u", sprites);
            ImGui::Text("Hero world: (%.3f, %.3f)", renderPos.x, renderPos.y);
            ImGui::Text("Integer scale: %ux", layout.scale);
            ImGui::Text("Window: %dx%d", winSize.x, winSize.y);
            ImGui::End();

            rhi::RenderPassColorAttachment uiColor{};
            uiColor.texture = backbuffer;
            uiColor.loadOp = rhi::LoadOp::Load;      // 블릿 결과 위에 얹음
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
            m_debugUi.EndFrame();                    // 백버퍼 RTV 바인딩 상태에서 ImGui 드로우
            cmd.EndRenderPass();
        }

        // --- 검증용 덤프(Present 전, 백버퍼에 그린 뒤) ---
        ++m_frameCount;
        HandleDumps(backbuffer);

        m_swapChain->Present(/*vsync*/ false);
        m_device->EndFrame();

        // --resize-test
        if (m_control->resizeTest) {
            if (m_frameCount == 30) DoResize(1920, 1080);
            if (m_frameCount == 60) DoResize(1024, 600);
        }

        // 초당 로그(fps·고정스텝 수).
        m_secondsAccum += step.unscaledDeltaSeconds;
        if (m_secondsAccum >= 1.0) {
            const uint64_t fixedNow = step.fixedStepIndex;
            const uint64_t renderDelta = m_frameCount - m_lastFrameCount;
            const uint64_t fixedDelta = fixedNow - m_lastFixedIndex;
            m_fps = static_cast<float>(renderDelta);
            m_fixedPerSec = fixedDelta;
            MYE_LOG_INFO("SpriteDemo", "1s tick: fps={} fixedSteps={} pos=({},{}) scale={}",
                         renderDelta, fixedDelta, m_curPos.x, m_curPos.y, layout.scale);
            m_secondsAccum -= 1.0;
            m_lastFixedIndex = fixedNow;
            m_lastFrameCount = m_frameCount;
        }

        // --frames N
        const bool lastFrame = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
        if (lastFrame) {
            MYE_LOG_INFO("SpriteDemo", "frame limit {} reached — requesting exit",
                         m_control->maxFrames);
            m_control->app->RequestExit(0);
        }
    }

    void HandleDumps(rhi::TextureHandle backbuffer) {
        // move-test: 프레임 2에서 A(초기), 프레임 maxFrames(또는 100)에서 B(이동 후) 저장.
        if (m_control->moveTest) {
            const uint64_t bFrame = m_control->frameLimit ? m_control->maxFrames : 100;
            if (m_frameCount == 2 && !m_dumpedA) {
                DumpTo(backbuffer, m_control->dumpAPath, "A");
                m_dumpedA = true;
            }
            if (m_frameCount >= bFrame && !m_dumpedB) {
                DumpTo(backbuffer, m_control->dumpBPath, "B");
                m_dumpedB = true;
            }
        }
        // --dump: 마지막 프레임.
        if (m_control->dumpEnabled && !m_dumped) {
            const bool lastFrame = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
            if (lastFrame) {
                DumpTo(backbuffer, m_control->dumpPath, "final");
                m_dumped = true;
            }
        }
    }

    void DumpTo(rhi::TextureHandle backbuffer, const std::string& path, const char* label) {
        auto cap = rhi::CaptureBackbuffer(*m_device, backbuffer, path);
        if (cap) {
            MYE_LOG_INFO("SpriteDemo", "dump {} -> '{}' (frame {})", label, path, m_frameCount);
        } else {
            MYE_LOG_ERROR("SpriteDemo", "dump {} failed: {}", label, cap.GetError().message);
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
        MYE_LOG_INFO("SpriteDemo", "resize-test: requested client {}x{}", w, h);
    }

    RunControl* m_control = nullptr;
    mye::EngineContext* m_ctx = nullptr;
    mye::EventBus* m_bus = nullptr;
    mye::InputState* m_input = nullptr;

    std::unique_ptr<rhi::IDevice>    m_device;
    std::unique_ptr<rhi::ISwapChain> m_swapChain;
    std::unique_ptr<asset::VirtualFileSystem> m_vfs;
    std::unique_ptr<asset::AssetManager>      m_assets;
    asset::AssetHandle<asset::Texture>        m_hero;
    rhi::TextureHandle m_white{};

    render::PixelPerfectTarget m_rt;
    render::SpriteBatch        m_batch;
    render::Camera2D           m_camera;
    mye::imgui::DebugUi        m_debugUi;

    mye::ScopedSubscription m_onResized;

    Vec2 m_curPos{0.0f, 0.0f};
    Vec2 m_prevPos{0.0f, 0.0f};

    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
    bool     m_dumpedA = false;
    bool     m_dumpedB = false;
    double   m_secondsAccum = 0.0;
    uint64_t m_lastFixedIndex = 0;
    uint64_t m_lastFrameCount = 0;
    float    m_fps = 0.0f;
    uint64_t m_fixedPerSec = 0;
};

class SpriteDemoApp final : public mye::Application {
public:
    explicit SpriteDemoApp(const mye::LaunchArgs& args) {
        m_control.app = this;
        const auto& a = args.args;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_control.frameLimit = true;
                m_control.maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_control.dumpEnabled = true;
                m_control.dumpPath = a[++i];
            } else if (a[i] == "--move-test") {
                m_control.moveTest = true;
            } else if (a[i] == "--dumpA" && i + 1 < a.size()) {
                m_control.dumpAPath = a[++i];
            } else if (a[i] == "--dumpB" && i + 1 < a.size()) {
                m_control.dumpBPath = a[++i];
            } else if (a[i] == "--resize-test") {
                m_control.resizeTest = true;
            }
        }
    }

    void OnConfigure(mye::ConfigSystem& /*config*/) override {}

    void OnRegisterModules(mye::ModuleRegistry& modules) override {
        modules.Register(std::make_unique<SpriteDemoModule>(&m_control));
    }

    void OnStart(mye::EngineContext& /*ctx*/) override {
        MYE_LOG_INFO("SpriteDemo", "sprite_demo start (frames={} moveTest={} resizeTest={})",
                     m_control.frameLimit ? static_cast<long long>(m_control.maxFrames) : -1LL,
                     m_control.moveTest, m_control.resizeTest);
    }

private:
    RunControl m_control;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) {
    return new SpriteDemoApp(args);
}
} // namespace mye

int main(int /*argc*/, char** /*argv*/) {
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    }
    for (const auto& a : launch.args) {
        constexpr std::string_view prefix = "--project=";
        if (a.starts_with(prefix)) launch.projectPath = a.substr(prefix.size());
    }

    launch.mainWindow.title = "MyEngine — sprite_demo (M1)";
    launch.mainWindow.clientSize = {960, 540};
    launch.mainWindow.resizable = true;

    return mye::GuardedMain(launch);
}
