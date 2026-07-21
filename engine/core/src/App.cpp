// App.cpp — GuardedMain(부팅 시퀀스·고정 60Hz 루프·보간 alpha) + EngineContext 구현 (docs/01).
//
// 부팅 순서(GuardedMain이 보장):
//   크래시 필터(Phase 0: 로그만) → CRT 릭 체크(Debug) → 로그(콘솔+파일) → 경로 결정
//   → 설정 로드(engine.json/project.json) → OnConfigure/OnRegisterModules → 메인 윈도우 생성
//   → 모듈 초기화 → 고정 타임스텝 루프 → 정확한 역순 종료.
#include "mye/core/App.h"
#include "mye/core/Assert.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/core/platform/Win32Window.h"

#include <Windows.h>
#include <shellapi.h>            // CommandLineToArgvW (link: shell32)

#if MYE_CRT_LEAK_CHECK
#include <crtdbg.h>
#endif

#include <algorithm>
#include <unordered_map>

namespace mye {

// ---------------------------------------------------------------------------
// Widen / Narrow — win32 W-API 경계 UTF-8 <-> UTF-16 (docs/01 문자열 정책)
// ---------------------------------------------------------------------------
std::wstring Widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                          static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), len);
    return out;
}

std::string Narrow(std::wstring_view utf16) {
    if (utf16.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(),
                                          static_cast<int>(utf16.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                          out.data(), len, nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------
void Application::RequestExit(int exitCode) {
    m_exitRequested = true;
    m_exitCode = exitCode;
}

EngineContext& Application::Context() {
    MYE_ASSERT(m_context != nullptr, "Context()는 OnStart 이후에만 유효");
    return *m_context;
}

// ---------------------------------------------------------------------------
// EngineContext 구현 (엔진 내부 전용)
// ---------------------------------------------------------------------------
namespace {

class EngineContextImpl final : public EngineContext {
public:
    void* GetServiceRaw(ServiceId id) override {
        const auto it = m_services.find(id);
        return it != m_services.end() ? it->second : nullptr;
    }
    void RegisterServiceRaw(ServiceId id, void* service) override {
        MYE_ASSERT(m_services.find(id) == m_services.end(), "ServiceId 중복 등록");
        m_services[id] = service;
    }
    void UnregisterServiceRaw(ServiceId id) override { m_services.erase(id); }
    uint32_t GetEngineVersion() const override { return kEngineVersion; }
    const EnginePaths& GetPaths() const override { return m_paths; }

    EnginePaths m_paths;
    static constexpr uint32_t kEngineVersion = 0x0000'0001;   // 0.0.1

private:
    std::unordered_map<ServiceId, void*> m_services;
};

// ---------------------------------------------------------------------------
// 부팅 헬퍼 (경로·크래시 필터·CRT 릭 체크)
// ---------------------------------------------------------------------------

std::string NormalizeSeparators(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// 실행 파일이 있는 디렉터리 (UTF-8, '/' 구분자) — EnginePaths::engineDir
std::string GetExecutableDir() {
    wchar_t buffer[1024];
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, 1024);
    if (len == 0 || len >= 1024) return {};
    const std::string path = NormalizeSeparators(Narrow(std::wstring_view{buffer, len}));
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

// %LOCALAPPDATA% (UTF-8, '/' 구분자) — 없으면 빈 문자열
std::string GetLocalAppDataDir() {
    wchar_t buffer[1024];
    const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 1024);
    if (len == 0 || len >= 1024) return {};
    return NormalizeSeparators(Narrow(std::wstring_view{buffer, len}));
}

std::string LastPathComponent(std::string_view path) {
    while (!path.empty() && (path.back() == '/' || path.back() == '\\')) path.remove_suffix(1);
    const size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// 중간 경로 포함 디렉터리 생성 (이미 있으면 성공)
bool EnsureDirectory(std::string_view utf8Path) {
    if (utf8Path.empty()) return false;
    const std::wstring wide = Widen(utf8Path);
    for (size_t i = 1; i <= wide.size(); ++i) {
        if (i != wide.size() && wide[i] != L'/' && wide[i] != L'\\') continue;
        const std::wstring prefix = wide.substr(0, i);
        if (prefix.size() == 2 && prefix[1] == L':') continue;   // 드라이브 루트 ("E:")
        if (::CreateDirectoryW(prefix.c_str(), nullptr) == 0 &&
            ::GetLastError() != ERROR_ALREADY_EXISTS) {
            if (i == wide.size()) return false;   // 마지막 구성요소 실패만 치명으로 취급
        }
    }
    return true;
}

// Phase 0 크래시 필터: 로그만 남긴다. 미니덤프(MiniDumpWriteDump)는 Phase 1 (docs/01).
LONG WINAPI CrashExceptionFilter(EXCEPTION_POINTERS* info) {
    const uint32_t code =
        (info != nullptr && info->ExceptionRecord != nullptr)
            ? static_cast<uint32_t>(info->ExceptionRecord->ExceptionCode) : 0u;
    const void* address =
        (info != nullptr && info->ExceptionRecord != nullptr)
            ? info->ExceptionRecord->ExceptionAddress : nullptr;
    MYE_LOG_FATAL("Core", "unhandled SEH exception: code=0x{:08X} address={} (minidump: Phase 1)",
                  code, address);
    return EXCEPTION_EXECUTE_HANDLER;   // 로그 플러시 후 프로세스 종료 (WER 팝업 억제)
}

// Debug 빌드: CRT 디버그 힙 릭 체크 — 종료 시 자동 릭 리포트 (M0 완료 기준: 릭 0건)
void EnableCrtLeakCheck() {
#if MYE_CRT_LEAK_CHECK
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);
    // 릭 리포트를 디버거 출력 + stderr 양쪽으로 (콘솔 실행에서도 확인 가능하게)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// GuardedMain
// ---------------------------------------------------------------------------
int GuardedMain(const LaunchArgs& args) {
    ::SetUnhandledExceptionFilter(&CrashExceptionFilter);
    EnableCrtLeakCheck();

    Log::Init();

    // ---- 경로 결정 ----
    EngineContextImpl ctx;
    ctx.m_paths.engineDir  = GetExecutableDir();
    ctx.m_paths.projectDir = NormalizeSeparators(args.projectPath);
    {
        const std::string localAppData = GetLocalAppDataDir();
        std::string projectName = LastPathComponent(ctx.m_paths.projectDir);
        if (projectName.empty()) projectName = "default";
        if (!localAppData.empty()) {
            ctx.m_paths.userDir = localAppData + "/MyEngine/" + projectName;
        }
    }

    // 파일 로그 싱크 — M0 완료 기준 "로그 파일 생성". userDir 불가 시 엔진 디렉터리 폴백.
    {
        const std::string logDir = !ctx.m_paths.userDir.empty()
                                       ? ctx.m_paths.userDir + "/logs"
                                       : ctx.m_paths.engineDir + "/logs";
        if (EnsureDirectory(logDir)) {
            Log::AddSink(CreateFileSink(logDir + "/engine.log"));
        }
    }

    MYE_LOG_INFO("Core", "MyEngine boot (engineDir='{}', projectDir='{}', userDir='{}')",
                 ctx.m_paths.engineDir, ctx.m_paths.projectDir, ctx.m_paths.userDir);

    Application* app = CreateApplication(args);
    if (!app) {
        MYE_LOG_FATAL("Core", "CreateApplication returned null");
        Log::Shutdown();
        return -1;
    }

    // 코어 서비스 수명(스코프 = GuardedMain)
    EventBus       bus;
    ConfigSystem   config;
    TimeSystem     time;
    ModuleRegistry modules;

    ctx.RegisterServiceRaw(EventBus::kServiceId, &bus);
    ctx.RegisterServiceRaw(ConfigSystem::kServiceId, &config);
    ctx.RegisterServiceRaw(TimeSystem::kServiceId, &time);
    ctx.RegisterServiceRaw(ModuleRegistry::kServiceId, &modules);

    // ---- 설정 로드: Engine → Project 순 병합. 파일 없음은 성공(빈 스코프) ----
    config.RegisterSection("time", ConfigScope::Project);
    if (auto loaded = config.LoadScopeFromFile(ConfigScope::Engine,
                                               ctx.m_paths.engineDir + "/engine.json"); !loaded) {
        MYE_LOG_WARN("Core", "engine.json load failed: {}", loaded.GetError().message);
    }
    if (!ctx.m_paths.projectDir.empty()) {
        if (auto loaded = config.LoadScopeFromFile(ConfigScope::Project,
                                                   ctx.m_paths.projectDir + "/config/project.json");
            !loaded) {
            MYE_LOG_WARN("Core", "project.json load failed: {}", loaded.GetError().message);
        }
    }

    app->OnConfigure(config);
    app->OnRegisterModules(modules);

    // 고정 스텝 Hz·프레임 델타 클램프 — 설정에서 로드 (docs/00 A2: 기본 60Hz)
    time.SetFixedRate(config.GetFloat("time.fixedHz", 60.0));
    const double maxFrameDelta = config.GetFloat("time.maxFrameDeltaSeconds", 0.25);  // 죽음의 나선 방지

    // ---- 메인 윈도우 (--headless 시 생략 — 창 없는 테스트 실행 허용) ----
    const bool headless =
        std::find(args.args.begin(), args.args.end(), "--headless") != args.args.end();
    std::unique_ptr<win32::Win32Window> window;
    if (!headless) {
        auto created = win32::Win32Window::Create(args.mainWindow, bus);
        if (!created) {
            MYE_LOG_FATAL("Core", "main window creation failed: {}", created.GetError().message);
            delete app;
            Log::Shutdown();
            return -3;
        }
        window = std::move(created).Value();
        ctx.RegisterServiceRaw(kMainWindowServiceId, static_cast<IWindow*>(window.get()));
    }

    int exitCode = 0;
    if (auto initResult = modules.InitializeAll(ctx); !initResult) {
        MYE_LOG_FATAL("Core", "module init failed: {}", initResult.GetError().message);
        exitCode = -2;
    } else {
        app->m_context = &ctx;
        app->OnStart(ctx);

        // ---- 메인 루프 (봉인 구조 — docs/01 비확장 지점): 고정 스텝 + 가변 렌더 + 보간 alpha ----
        Clock clock;
        clock.Reset();
        double accumulator = 0.0;
        bool running = !app->IsExitRequested() && !(window && window->IsCloseRequested());
        while (running) {
            const double frameDelta =
                std::min(static_cast<double>(clock.TickNanoseconds()) * 1e-9, maxFrameDelta);
            accumulator += frameDelta * time.GetTimeScale();
            time.AdvanceFrame(frameDelta);

            if (window) window->PumpMessages();          // win32 메시지 → 이벤트 버스
            bus.Flush(EventChannel::PreUpdate);
            modules.Tick(UpdatePhase::PreUpdate, time.CurrentStep());

            const double fixedDelta = time.GetFixedDeltaSeconds();
            while (accumulator >= fixedDelta) {
                time.AdvanceFixedStep();
                TimeStep fixedStep = time.CurrentStep();
                fixedStep.deltaSeconds = fixedDelta;          // 계약: Fixed 페이즈 델타 = fixedDelta
                fixedStep.unscaledDeltaSeconds = fixedDelta;
                modules.Tick(UpdatePhase::FixedUpdate, fixedStep);
                accumulator -= fixedDelta;
            }
            time.SetInterpolationAlpha(accumulator / fixedDelta);   // 렌더 보간 계수 [0,1)

            modules.Tick(UpdatePhase::Update, time.CurrentStep());
            modules.Tick(UpdatePhase::PostUpdate, time.CurrentStep());
            bus.Flush(EventChannel::PostUpdate);
            modules.Tick(UpdatePhase::PreRender, time.CurrentStep());  // 02가 alpha 보간 후 렌더·Present
            modules.Tick(UpdatePhase::PostRender, time.CurrentStep());

            running = !app->IsExitRequested() && !(window && window->IsCloseRequested());
        }

        app->OnStop(ctx);
        app->m_context = nullptr;
        exitCode = app->GetExitCode();
        modules.ShutdownAll(ctx);   // 초기화의 정확한 역순
    }

    if (window) {
        ctx.UnregisterServiceRaw(kMainWindowServiceId);
        window.reset();   // 버스(구독자)보다 먼저 파괴 — 이벤트 발행처 수명 계약 준수
    }

    delete app;
    MYE_LOG_INFO("Core", "shutdown complete (exit {})", exitCode);
    Log::Shutdown();
    // MYE_CRT_LEAK_CHECK: 릭 리포트는 CRT 종료 시 자동 출력(_CRTDBG_LEAK_CHECK_DF) — 목표 0건
    return exitCode;
}

int GuardedMain(int argc, char** argv) {
    LaunchArgs launch;

    // main의 argv는 ANSI 코드페이지 — UTF-8 보장을 위해 항상 유니코드 커맨드라인에서 변환한다.
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc);
        wideArgv != nullptr) {
        launch.args.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(Narrow(wideArgv[i]));
        ::LocalFree(wideArgv);
    } else {
        launch.args.reserve(static_cast<size_t>(argc));   // 폴백 (실패 사례는 사실상 없음)
        for (int i = 0; i < argc; ++i) launch.args.emplace_back(argv[i]);
    }

    for (const auto& a : launch.args) {
        constexpr std::string_view prefix = "--project=";
        if (a.starts_with(prefix)) launch.projectPath = a.substr(prefix.size());
    }
    return GuardedMain(launch);
}

} // namespace mye
