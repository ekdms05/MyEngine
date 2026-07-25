// MyEditor — 에디터 실행 파일 진입점 (docs/07 §1, §2)
//
// L5의 얇은 앱. 게임과 동일한 엔진 모듈 스택 위에 EditorModule을 얹는다. 프로젝트는 커맨드라인
//   `--project path/to/Game.myeproj`로 연다(런처 UI는 P2). EditorModule 이 RHI 디바이스·스왑체인·
//   DebugUi(도킹 셸)·씬 뷰포트 오프스크린 렌더·플레이 tick 게이팅을 소유·오케스트레이션한다.
//
// 자동 검증 CLI(EditorModule 로 전달):
//   --headless        창 없이 오프스크린 렌더만(--dump 와 함께 CI 검증).
//   --frames N        N 렌더 프레임 후 종료(0 exit).
//   --dump path.bmp   마지막 프레임 뷰포트 RT 를 BMP 로 덤프.
#include "mye/core/App.h"
#include "mye/core/Log.h"
#include "mye/core/I18n.h"

#include "mye/scene/SceneModule.h"
#include "mye/editor/EditorModule.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace mye {

// 에디터 앱 — 씬 + 에디터 모듈을 등록한다. RHI·렌더·imgui 배선은 EditorModule 내부에서 수행.
class MyEditorApp final : public Application {
public:
    explicit MyEditorApp(const LaunchArgs& args) {
        const auto& a = args.args;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_frameLimit = true;
                m_maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_dumpEnabled = true;
                m_dumpPath = a[++i];
            } else if (a[i] == "--lang" && i + 1 < a.size()) {
                const std::string& v = a[++i];
                using mye::i18n::Lang;
                if (v == "en") mye::i18n::SetLanguage(Lang::En);
                else if (v == "ja") mye::i18n::SetLanguage(Lang::Ja);
                else if (v == "zh") mye::i18n::SetLanguage(Lang::Zh);
                else mye::i18n::SetLanguage(Lang::Ko);
            }
            // --headless 는 core(GuardedMain)가 소비해 창을 생략한다.
        }
    }

    void OnConfigure(ConfigSystem& /*config*/) override {}

    void OnRegisterModules(ModuleRegistry& modules) override {
        modules.Register(std::make_unique<scene::SceneModule>());
        modules.Register(std::make_unique<editor::EditorModule>());
    }

    void OnStart(EngineContext& ctx) override {
        // EditorModule 에 CLI 제어를 주입(프레임 한도 도달 시 이 Application 을 종료).
        if (auto* em = ctx.GetService<editor::EditorModule>()) {
            em->SetCliControl(m_frameLimit, m_maxFrames, m_dumpEnabled, m_dumpPath,
                              [this]() { RequestExit(0); });
        }
        MYE_LOG_INFO("MyEditor", "start (frames={} dump='{}')",
                     m_frameLimit ? static_cast<long long>(m_maxFrames) : -1LL, m_dumpPath);
    }

    void OnStop(EngineContext& /*ctx*/) override {}

private:
    bool          m_frameLimit = false;
    std::uint64_t m_maxFrames = 0;
    bool          m_dumpEnabled = false;
    std::string   m_dumpPath;
};

Application* CreateApplication(const LaunchArgs& args) {
    return new MyEditorApp(args);
}

} // namespace mye

// 진입점 — GuardedMain(int,char**) 오버로드가 유니코드 커맨드라인 변환 + `--project=` 파싱을
//   수행한다(core App.cpp 규약). 창 기본값(제목·크기·리사이즈)은 여기서 지정한다.
int main(int /*argc*/, char** /*argv*/) {
    mye::LaunchArgs launch;
    int wideArgc = 0;
    if (LPWSTR* wideArgv = ::CommandLineToArgvW(::GetCommandLineW(), &wideArgc); wideArgv) {
        launch.args.reserve(static_cast<std::size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) launch.args.emplace_back(mye::Narrow(wideArgv[i]));
        // --project / --project=<path> 파싱(GuardedMain(LaunchArgs) 는 projectPath 만 소비).
        for (int i = 1; i < wideArgc; ++i) {
            const std::string& s = launch.args[static_cast<std::size_t>(i)];
            constexpr std::string_view kPrefix = "--project=";
            if (s.rfind(std::string(kPrefix), 0) == 0) launch.projectPath = s.substr(kPrefix.size());
            else if (s == "--project" && i + 1 < wideArgc)
                launch.projectPath = launch.args[static_cast<std::size_t>(i + 1)];
        }
        ::LocalFree(wideArgv);
    }
    launch.mainWindow.title = "MyEngine — MyEditor (M4)";
    launch.mainWindow.clientSize = {1920, 1080};
    launch.mainWindow.resizable = true;
    return mye::GuardedMain(launch);
}
