// mye/core/App.h — 엔트리포인트·Application·GuardedMain (docs/01)
//
// 실행 파일(게임/에디터)은 CreateApplication() 하나만 구현한다.
// GuardedMain이 크래시 필터 설치 → 로그 → 설정 로드 → 모듈 초기화 → 고정 60Hz 루프 → 역순 종료를 보장.
//
// 메인 루프(봉인 — 비확장 지점):
//   frameDelta = clock.Tick(); frameDelta = min(frameDelta, maxFrameDelta);
//   accumulator += frameDelta * timeScale;
//   PumpMessages(); bus.Flush(PreUpdate);
//   while (accumulator >= fixedDelta) { Tick(FixedUpdate, fixed); accumulator -= fixedDelta; }
//   alpha = accumulator / fixedDelta;
//   Tick(PreUpdate..PostUpdate); bus.Flush(PostUpdate); Tick(PreRender); Tick(PostRender);
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Config.h"
#include "mye/core/Module.h"
#include "mye/core/Window.h"

#include <string>
#include <vector>

namespace mye {

struct LaunchArgs {
    std::vector<std::string> args;   // UTF-8 변환된 커맨드라인 (argv[0] 포함)
    std::string projectPath;         // --project=... (게임/에디터 공통, 없으면 빈 문자열)
    WindowDesc  mainWindow;          // 앱이 OnConfigure 전에 창 기본값을 바꿀 수 있음
};

class Application {
public:
    virtual ~Application() = default;

    virtual void OnConfigure(ConfigSystem& config) = 0;          // 설정 오버라이드 기회 (모듈 초기화 전)
    virtual void OnRegisterModules(ModuleRegistry& modules) = 0; // 정적 모듈 등록 (+ 미래: 플러그인 로더)
    virtual void OnStart(EngineContext& /*ctx*/) {}              // 모듈 초기화 완료 후, 루프 진입 직전
    virtual void OnStop(EngineContext& /*ctx*/) {}               // 루프 탈출 직후, 모듈 종료 전

    void RequestExit(int exitCode = 0);
    bool IsExitRequested() const { return m_exitRequested; }
    int  GetExitCode() const { return m_exitCode; }

protected:
    EngineContext& Context();        // OnStart 이후 유효 (그 전 호출은 어서트)

private:
    friend int GuardedMain(const LaunchArgs&);
    EngineContext* m_context = nullptr;
    bool m_exitRequested = false;
    int  m_exitCode = 0;
};

// 실행 파일이 구현하는 유일한 진입 함수. 소유권은 GuardedMain이 가져간다(종료 시 delete).
Application* CreateApplication(const LaunchArgs& args);

// 엔진이 소유: 크래시 필터·CRT 릭 체크(Debug) 설치 → 로그/설정 → 모듈 초기화 → 루프 → 역순 종료
int GuardedMain(const LaunchArgs& args);

// 편의 오버로드: 커맨드라인(UTF-16 → UTF-8 변환 포함)을 LaunchArgs로 만들어 위를 호출.
// 콘솔 서브시스템 exe의 main()이 이것만 호출하면 된다.
int GuardedMain(int argc, char** argv);

} // namespace mye
