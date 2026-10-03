// MyEditor — 에디터 실행 파일 진입점 (docs/07 §1, §2)
//
// L5의 얇은 앱. 게임과 동일한 엔진 모듈 스택 위에 EditorModule을 얹는다. 프로젝트는 커맨드라인
//   메뉴 또는 `--project path/to/Game.myeproj`로 연다. EditorModule 이 RHI 디바이스·스왑체인·
//   DebugUi(도킹 셸)·씬 뷰포트 오프스크린 렌더·플레이 tick 게이팅을 소유·오케스트레이션한다.
//
// 자동 검증 CLI(EditorModule 로 전달):
//   --headless        창 없이 오프스크린 렌더만(--dump 와 함께 CI 검증).
//   --frames N        N 렌더 프레임 후 종료(0 exit).
//   --dump path.bmp   마지막 프레임 뷰포트 RT 를 BMP 로 덤프.
#include "mye/core/App.h"
#include "mye/core/Log.h"
#include "mye/core/I18n.h"
#include "mye/core/JsonFile.h"

#include "mye/scene/SceneModule.h"
#include "mye/editor/EditorModule.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/Selection.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/ProjectAssetOperations.h"
#include "mye/editor/Viewport.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"

#include <Windows.h>
#include <shellapi.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye {
namespace {
std::string ProjectArgument(const std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i].starts_with("--project=")) return args[i].substr(10);
        if (args[i] == "--project" && i + 1 < args.size()) return args[i + 1];
    }
    return {};
}
}

// 에디터 앱 — 씬 + 에디터 모듈을 등록한다. RHI·렌더·imgui 배선은 EditorModule 내부에서 수행.
class MyEditorApp final : public Application {
public:
    explicit MyEditorApp(const LaunchArgs& args) : m_projectPath(ProjectArgument(args.args)) {
        std::array<wchar_t, 32768> executable{};
        const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (count > 0 && count < executable.size())
            m_executableDirectory = Utf8String(std::filesystem::path(executable.data()).parent_path());
        const auto& a = args.args;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--frames" && i + 1 < a.size()) {
                m_frameLimit = true;
                m_maxFrames = std::strtoull(a[++i].c_str(), nullptr, 10);
            } else if (a[i] == "--dump" && i + 1 < a.size()) {
                m_dumpEnabled = true;
                m_dumpPath = a[++i];
            } else if (a[i] == "--animation" && i + 1 < a.size()) {
                m_animationPath = a[++i];
            } else if (a[i] == "--ui" && i + 1 < a.size()) {
                m_uiPath = a[++i];
            } else if (a[i] == "--animation-state" && i + 1 < a.size()) {
                m_animationStatePath = a[++i];
            } else if (a[i] == "--import-asset" && i + 1 < a.size()) {
                m_importSource = a[++i];
            } else if (a[i] == "--asset-destination" && i + 1 < a.size()) {
                m_importDestination = a[++i];
            } else if (a[i] == "--add-element-dialog") {
                m_addElementDialog = true;
            } else if (a[i] == "--input-settings-dialog") {
                m_inputSettingsDialog = true;
            } else if (a[i] == "--workspace" && i + 1 < a.size()) {
                m_workspace = a[++i];
            } else if (a[i] == "--view3d") {
                m_view3d = true;
            } else if (a[i] == "--select" && i + 1 < a.size()) {
                m_selectName = a[++i];
            } else if (a[i] == "--play") {
                m_startPlaying = true;
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
        const auto starter = Utf8Path(m_executableDirectory) / "templates" / "meadow_village";
        std::error_code ec;
        modules.Register(std::make_unique<editor::EditorModule>(m_projectPath,
            std::filesystem::is_directory(starter, ec) ? Utf8String(starter) : ""));
    }

    void OnStart(EngineContext& ctx) override {
        // EditorModule 에 CLI 제어를 주입(프레임 한도 도달 시 이 Application 을 종료).
        if (auto* em = ctx.GetService<editor::EditorModule>()) {
            const bool automatedStartup = !ctx.GetServiceRaw(kMainWindowServiceId) ||
                m_frameLimit || m_dumpEnabled || m_startPlaying || m_view3d || m_addElementDialog || m_inputSettingsDialog ||
                !m_importSource.empty() || !m_importDestination.empty() || !m_animationPath.empty() || !m_uiPath.empty() || !m_animationStatePath.empty() ||
                !m_workspace.empty() || !m_selectName.empty();
            // File associations and interactive --project opens recover in the launcher.
            // Scripted operations must not report success after opening the wrong project.
            if (automatedStartup && !m_projectPath.empty() && (!em->App() || !em->App()->Project().IsOpen())) {
                RequestExit(1); return;
            }
            if (!m_importSource.empty() || !m_importDestination.empty()) {
                if (!em->App() || !em->App()->Project().IsOpen() || m_importSource.empty() || m_importDestination.empty()) {
                    MYE_LOG_ERROR("MyEditor", "Asset import requires --project, --import-asset and --asset-destination");
                    RequestExit(1); return;
                }
                auto imported = editor::ImportProjectAsset(em->App()->Project().RootDir(), m_importSource, m_importDestination);
                if (!imported) { MYE_LOG_ERROR("MyEditor", "{}", imported.GetError().message); RequestExit(1); return; }
                auto refreshed = em->App()->Viewport()->RefreshAssetIndex();
                if (!refreshed) { MYE_LOG_ERROR("MyEditor", "{}", refreshed.GetError().message); RequestExit(1); return; }
                MYE_LOG_INFO("MyEditor", "Imported asset: {}", m_importDestination);
            }
            if (em->App() && m_addElementDialog) em->App()->RequestAddElement();
            if (m_view3d) em->SetPerspectiveView(true);
            if (em->App() && !m_animationPath.empty()) {
                const auto opened = em->App()->OpenAnimation(m_animationPath);
                if (!opened) { MYE_LOG_ERROR("MyEditor", "{}", opened.GetError().message); RequestExit(1); }
            }
            if (em->App() && !m_workspace.empty()) {
                using Workspace = editor::EditorApp::Workspace;
                if (m_workspace == "2d") em->App()->SelectWorkspace(Workspace::Scene2D);
                else if (m_workspace == "3d") em->App()->SelectWorkspace(Workspace::Scene3D);
                else if (m_workspace == "lua") em->App()->SelectWorkspace(Workspace::Lua);
                else { MYE_LOG_ERROR("MyEditor", "Unknown workspace: {}", m_workspace); RequestExit(1); }
            }
            if (em->App() && !m_uiPath.empty()) {
                const auto opened = em->App()->OpenUi(m_uiPath);
                if (!opened) { MYE_LOG_ERROR("MyEditor", "{}", opened.GetError().message); RequestExit(1); return; }
            }
            if (em->App() && !m_animationStatePath.empty()) {
                const auto opened = em->App()->OpenAnimationState(m_animationStatePath);
                if (!opened) { MYE_LOG_ERROR("MyEditor", "{}", opened.GetError().message); RequestExit(1); return; }
            }
            if (em->App() && m_startPlaying) {
                const auto started = em->App()->PlayMode().Play();
                if (!started) { MYE_LOG_ERROR("MyEditor", "{}", started.GetError().message); RequestExit(1); }
            }
            if (em->App()) em->App()->RefreshDocumentContext();
            if (m_inputSettingsDialog) {
                auto opened = em->App() ? em->App()->RequestInputSettings()
                    : Expected<void, Error>{Error{"Editor initialization failed", 1}};
                if (!opened) { MYE_LOG_ERROR("MyEditor", "{}", opened.GetError().message); RequestExit(1); return; }
            }
            if (em->App() && !m_selectName.empty()) {
                if (auto* world = em->App()->PlayMode().ActiveWorld())
                    world->Query<scene::ObjectName>().Each([&](ecs::Entity entity, const scene::ObjectName& name) {
                        if (name.value == m_selectName) em->App()->Selection().Select(editor::SelectableRef::OfEntity(entity));
                    });
            }
            em->SetCliControl(m_frameLimit, m_maxFrames, m_dumpEnabled, m_dumpPath,
                              [this](int code) { RequestExit(code); });
        }
        MYE_LOG_INFO("MyEditor", "start (frames={} dump='{}')",
                     m_frameLimit ? static_cast<long long>(m_maxFrames) : -1LL, m_dumpPath);
    }

    void OnStop(EngineContext& /*ctx*/) override {}

private:
    std::string   m_projectPath, m_executableDirectory, m_animationPath, m_uiPath, m_animationStatePath, m_selectName, m_workspace;
    std::string   m_importSource, m_importDestination;
    bool          m_addElementDialog = false;
    bool          m_inputSettingsDialog = false;
    bool m_view3d = false;
    bool          m_startPlaying = false;
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
        ::LocalFree(wideArgv);
    }
    launch.projectPath = mye::ProjectArgument(launch.args);
    const auto project = mye::Utf8Path(launch.projectPath);
    if (project.extension() == ".myeproj") {
        // Core settings consume a directory; the editor still opens the selected manifest.
        launch.projectPath = project.has_parent_path() ? mye::Utf8String(project.parent_path()) : ".";
    }
    launch.mainWindow.title = "MyEngine — MyEditor";
    launch.mainWindow.clientSize = launch.projectPath.empty() ? mye::Vec2i{640, 420} : mye::Vec2i{1600, 900};
    launch.mainWindow.resizable = true;
    return mye::GuardedMain(launch);
}
