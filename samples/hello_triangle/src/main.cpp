// hello_triangle — M0 완료 기준 데모 (docs/00 §7 M0)
//
// 목표: 실행 → 클리어 컬러 위에 정점 컬러 보간 삼각형. 창 리사이즈·최소화·복원 크래시 없음.
//       ESC 종료 시 릭 리포트 0건, 로그 파일 생성.
//
// 구조: TriangleRendererModule(IModule)이 OnInitialize에서 디바이스·스왑체인·셰이더·PSO를 만들고
//       PreRender 틱에서 BeginRenderPass(클리어) → Draw(3) → Present 한다.
//
// 자동 검증 CLI (GuardedMain 루프는 봉인 — 종료는 Application::RequestExit로만):
//   --frames N        N 렌더 프레임 후 exit 0
//   --dump path.bmp   마지막 백버퍼를 BMP로 저장 (rhi::CaptureBackbuffer)
//   --resize-test     실행 중 코드로 창 2회 리사이즈
#include "mye/core/App.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/rhi/Rhi.h"
#include "mye/rhi/ShaderCompiler.h"

#include <Windows.h>
#include <shellapi.h>   // CommandLineToArgvW

#include <cstdint>
#include <cstdlib>      // strtoull
#include <string>
#include <string_view>
#include <vector>

namespace {

// 위치(NDC) + 정점 컬러. VS 패스스루, PS는 보간된 정점 컬러 출력 → 삼각형 색 보간 증거.
constexpr const char* kTriangleHlsl = R"hlsl(
struct VsIn  { float3 pos : POSITION; float4 color : COLOR0; };
struct VsOut { float4 pos : SV_Position; float4 color : COLOR0; };
VsOut vs_main(VsIn i) { VsOut o; o.pos = float4(i.pos, 1.0); o.color = i.color; return o; }
float4 ps_main(VsOut i) : SV_Target { return i.color; }
)hlsl";

struct Vertex {
    float pos[3];
    float color[4];
};

// 상단=빨강, 좌하=초록, 우하=파랑 → 래스터라이저 정점 컬러 보간
constexpr Vertex kVertices[3] = {
    {{ 0.0f,  0.6f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{-0.6f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{ 0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
};

constexpr mye::Color kClearColor{0.10f, 0.10f, 0.12f, 1.0f};

// 앱과 렌더 모듈이 공유하는 M0 실행 제어 (CLI 검증 파라미터 + 종료 신호)
struct RunControl {
    mye::Application* app = nullptr;
    bool     frameLimit = false;
    uint64_t maxFrames = 0;
    bool     dumpEnabled = false;
    std::string dumpPath;
    bool     resizeTest = false;
};

class TriangleRendererModule final : public mye::IModule {
public:
    explicit TriangleRendererModule(RunControl* control) : m_control(control) {}

    const char* GetName() const override { return "TriangleRenderer"; }

    void OnInitialize(mye::EngineContext& ctx) override {
        namespace rhi = mye::rhi;
        m_ctx = &ctx;
        m_bus = &ctx.Events();

        mye::IWindow& window = ctx.MainWindow();

        // 1) 디바이스
        auto deviceResult = rhi::CreateDevice(rhi::Backend::DX11, {});
        if (!deviceResult) {
            MYE_LOG_FATAL("Sample", "CreateDevice failed: {}", deviceResult.GetError().message);
            m_control->app->RequestExit(-10);
            return;
        }
        m_device = std::move(deviceResult).Value();

        // 2) 스왑체인 (창 클라이언트 크기)
        rhi::SwapChainDesc scDesc{};
        scDesc.width  = 0;   // 0 = 창 크기 사용
        scDesc.height = 0;
        scDesc.format = rhi::Format::BGRA8UnormSrgb;
        m_swapChain = m_device->CreateSwapChain(window.GetNativeHandle(), scDesc);
        if (!m_swapChain) {
            MYE_LOG_FATAL("Sample", "CreateSwapChain failed");
            m_control->app->RequestExit(-11);
            return;
        }

        // 3) 셰이더 (FXC 런타임 컴파일)
        rhi::ShaderCompileDesc vsDesc{};
        vsDesc.source = kTriangleHlsl;
        vsDesc.entryPoint = "vs_main";
        vsDesc.stage = rhi::ShaderStage::Vertex;
        vsDesc.debugName = "triangle.vs";
        auto vsBytes = rhi::CompileShaderFxc(vsDesc);
        if (!vsBytes) {
            MYE_LOG_FATAL("Sample", "VS compile failed: {}", vsBytes.GetError().message);
            m_control->app->RequestExit(-12);
            return;
        }

        rhi::ShaderCompileDesc psDesc{};
        psDesc.source = kTriangleHlsl;
        psDesc.entryPoint = "ps_main";
        psDesc.stage = rhi::ShaderStage::Pixel;
        psDesc.debugName = "triangle.ps";
        auto psBytes = rhi::CompileShaderFxc(psDesc);
        if (!psBytes) {
            MYE_LOG_FATAL("Sample", "PS compile failed: {}", psBytes.GetError().message);
            m_control->app->RequestExit(-13);
            return;
        }

        m_vs = m_device->CreateShader(rhi::ShaderStage::Vertex, vsBytes.Value().data);
        m_ps = m_device->CreateShader(rhi::ShaderStage::Pixel, psBytes.Value().data);

        // 4) 버텍스 버퍼 (정적)
        rhi::BufferDesc vbDesc{};
        vbDesc.size = sizeof(kVertices);
        vbDesc.usage = rhi::BufferUsage::Vertex;
        vbDesc.access = rhi::CpuAccess::None;
        vbDesc.debugName = "triangle.vb";
        m_vertexBuffer = m_device->CreateBuffer(vbDesc, kVertices);

        // 4b) 파이프라인
        const rhi::VertexAttribute attrs[2] = {
            {"POSITION", 0, rhi::Format::RGB32Float,  0,                    0},
            {"COLOR",    0, rhi::Format::RGBA32Float, sizeof(float) * 3,    0},
        };
        rhi::GraphicsPipelineDesc pDesc{};
        pDesc.vs = m_vs;
        pDesc.ps = m_ps;
        pDesc.vertexLayout.attributes = attrs;
        pDesc.vertexLayout.stride = sizeof(Vertex);
        pDesc.topology = rhi::PrimitiveTopology::TriangleList;
        pDesc.raster.cull = rhi::CullMode::None;
        pDesc.rtFormats.colorCount = 1;
        pDesc.rtFormats.colorFormats[0] = m_swapChain->GetFormat();
        pDesc.debugName = "triangle.pso";
        m_pipeline = m_device->CreateGraphicsPipeline(pDesc);

        if (!m_pipeline.IsValid() || !m_vertexBuffer.IsValid()) {
            MYE_LOG_FATAL("Sample", "pipeline/vertex buffer creation failed");
            m_control->app->RequestExit(-14);
            return;
        }

        // 5) 리사이즈 → 스왑체인 재구성 (PumpMessages 시점 즉시 발행 → 렌더 패스 밖에서 처리)
        m_onResized = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<mye::WindowResizedEvent>([this](const mye::WindowResizedEvent& e) {
                if (m_swapChain && e.clientSize.x > 0 && e.clientSize.y > 0) {
                    m_swapChain->Resize(static_cast<uint32_t>(e.clientSize.x),
                                        static_cast<uint32_t>(e.clientSize.y));
                    MYE_LOG_INFO("Sample", "swapchain resized to {}x{}",
                                 e.clientSize.x, e.clientSize.y);
                }
                return false;   // 미소비 — 다른 구독자에게도 전달
            })};

        // ESC 종료 (M0 부트스트랩 키 이벤트)
        m_onKey = mye::ScopedSubscription{
            *m_bus,
            m_bus->Subscribe<mye::KeyEvent>([this](const mye::KeyEvent& e) {
                if (e.pressed && e.virtualKey == VK_ESCAPE) {
                    MYE_LOG_INFO("Sample", "ESC pressed — requesting exit");
                    m_control->app->RequestExit(0);
                }
                return false;
            })};

        MYE_LOG_INFO("Sample", "TriangleRendererModule initialized (device + swapchain + PSO ready)");
    }

    void OnPostInitialize(mye::EngineContext& ctx) override {
        // PreRender 페이즈에 렌더 틱 등록 (docs/01: 02가 alpha 보간 후 렌더·Present)
        ctx.Modules().AddTick(this, mye::UpdatePhase::PreRender,
                              [this](const mye::TimeStep& step) { Render(step); }, /*orderKey*/ 0);
    }

    void OnShutdown(mye::EngineContext& /*ctx*/) override {
        // Destroy 역순 해제 (지연 파괴 큐 검증 포인트)
        m_onKey.Reset();
        m_onResized.Reset();
        if (m_device) {
            if (m_pipeline.IsValid())     m_device->Destroy(m_pipeline);
            if (m_vertexBuffer.IsValid()) m_device->Destroy(m_vertexBuffer);
            if (m_ps.IsValid())           m_device->Destroy(m_ps);
            if (m_vs.IsValid())           m_device->Destroy(m_vs);
        }
        m_swapChain.reset();   // 스왑체인은 반드시 디바이스보다 먼저 파괴
        m_device.reset();
        MYE_LOG_INFO("Sample", "TriangleRendererModule shutdown ({} frames rendered)", m_frameCount);
    }

private:
    void Render(const mye::TimeStep& step) {
        namespace rhi = mye::rhi;
        if (!m_device || !m_swapChain) return;

        m_device->BeginFrame();

        rhi::TextureHandle backbuffer = m_swapChain->GetCurrentBackBuffer();
        const mye::Vec2i size = m_swapChain->GetSize();

        rhi::ICommandContext& cmd = m_device->GetImmediateContext();

        rhi::RenderPassColorAttachment color{};
        color.texture = backbuffer;
        color.loadOp = rhi::LoadOp::Clear;
        color.clearColor = kClearColor;

        rhi::RenderPassBeginDesc pass{};
        pass.colorAttachments = {&color, 1};
        pass.debugName = "triangle.pass";
        cmd.BeginRenderPass(pass);

        rhi::Viewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = static_cast<float>(size.x);
        vp.height = static_cast<float>(size.y);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        cmd.SetViewport(vp);

        cmd.SetPipeline(m_pipeline);
        cmd.SetVertexBuffer(0, m_vertexBuffer, 0);
        cmd.Draw(3, 0);

        cmd.EndRenderPass();

        // --dump: Present 전(백버퍼에 그린 뒤)에 마지막 프레임 캡처
        ++m_frameCount;
        const bool lastFrame = m_control->frameLimit && (m_frameCount >= m_control->maxFrames);
        if (m_control->dumpEnabled && lastFrame && !m_dumped) {
            auto captured = rhi::CaptureBackbuffer(*m_device, backbuffer, m_control->dumpPath);
            if (captured) {
                MYE_LOG_INFO("Sample", "backbuffer dumped to '{}'", m_control->dumpPath);
            } else {
                MYE_LOG_ERROR("Sample", "dump failed: {}", captured.GetError().message);
            }
            m_dumped = true;
        }

        m_swapChain->Present(/*vsync*/ false);
        m_device->EndFrame();

        // --resize-test: 실행 중 코드로 창 2회 리사이즈
        if (m_control->resizeTest) {
            if (m_frameCount == 30) DoResize(800, 480);
            if (m_frameCount == 60) DoResize(1024, 600);
        }

        // 초당 로그 (고정스텝 틱 수·fps)
        m_secondsAccum += step.unscaledDeltaSeconds;
        if (m_secondsAccum >= 1.0) {
            const uint64_t fixedNow = step.fixedStepIndex;
            const uint64_t framesNow = m_frameCount;
            const uint64_t fixedDelta = fixedNow - m_lastFixedIndex;
            const uint64_t renderDelta = framesNow - m_lastFrameCount;
            MYE_LOG_INFO("Sample", "1s tick: fps={} fixedSteps={} (fixedIdx={} frameIdx={})",
                         renderDelta, fixedDelta, fixedNow, step.frameIndex);
            m_secondsAccum -= 1.0;
            m_lastFixedIndex = fixedNow;
            m_lastFrameCount = framesNow;
        }

        // --frames N: N 프레임 후 종료
        if (lastFrame) {
            MYE_LOG_INFO("Sample", "frame limit {} reached — requesting exit", m_control->maxFrames);
            m_control->app->RequestExit(0);
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
        MYE_LOG_INFO("Sample", "resize-test: requested client {}x{}", w, h);
    }

    RunControl* m_control = nullptr;
    mye::EngineContext* m_ctx = nullptr;
    mye::EventBus* m_bus = nullptr;

    std::unique_ptr<mye::rhi::IDevice>    m_device;
    std::unique_ptr<mye::rhi::ISwapChain> m_swapChain;
    mye::rhi::ShaderHandle   m_vs{};
    mye::rhi::ShaderHandle   m_ps{};
    mye::rhi::PipelineHandle m_pipeline{};
    mye::rhi::BufferHandle   m_vertexBuffer{};
    mye::ScopedSubscription  m_onResized;
    mye::ScopedSubscription  m_onKey;

    uint64_t m_frameCount = 0;
    bool     m_dumped = false;
    double   m_secondsAccum = 0.0;
    uint64_t m_lastFixedIndex = 0;
    uint64_t m_lastFrameCount = 0;
};

class HelloTriangleApp final : public mye::Application {
public:
    explicit HelloTriangleApp(const mye::LaunchArgs& args) {
        m_control.app = this;
        // CLI 파싱: --frames N / --dump path.bmp / --resize-test
        const auto& a = args.args;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_control.frameLimit = true;
                m_control.maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_control.dumpEnabled = true;
                m_control.dumpPath = a[++i];
            } else if (a[i] == "--resize-test") {
                m_control.resizeTest = true;
            }
        }
    }

    void OnConfigure(mye::ConfigSystem& /*config*/) override {}

    void OnRegisterModules(mye::ModuleRegistry& modules) override {
        modules.Register(std::make_unique<TriangleRendererModule>(&m_control));
    }

    void OnStart(mye::EngineContext& /*ctx*/) override {
        MYE_LOG_INFO("Sample", "hello_triangle start (frames={} dump={} resizeTest={})",
                     m_control.frameLimit ? static_cast<long long>(m_control.maxFrames) : -1LL,
                     m_control.dumpEnabled ? m_control.dumpPath : std::string("<none>"),
                     m_control.resizeTest);
    }

private:
    RunControl m_control;
};

} // namespace

namespace mye {
Application* CreateApplication(const LaunchArgs& args) {
    return new HelloTriangleApp(args);
}
} // namespace mye

int main(int /*argc*/, char** /*argv*/) {
    // 커맨드라인 → LaunchArgs (UTF-8 변환은 GuardedMain의 int/char** 오버로드와 동일 경로).
    // 여기서 직접 만드는 이유: M0 데모 창 기본값(960x540 리사이즈 가능)을 args.mainWindow에 심기 위함.
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

    launch.mainWindow.title = "MyEngine — hello_triangle (M0)";
    launch.mainWindow.clientSize = {960, 540};
    launch.mainWindow.resizable = true;

    return mye::GuardedMain(launch);
}
