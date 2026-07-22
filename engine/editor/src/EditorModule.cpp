// EditorModule.cpp — 에디터 IModule 골격 (M4-A: 라이프사이클 배선, DebugUi/틱은 M4-B)
//
// 07 §1: 게임과 동일한 모듈 스택 위에 얹히는 EditorModule. DebugUi(도킹 셸) 소유·초기화 +
//   PreRender 틱에서 BeginFrame→EditorApp::OnFrame→EndFrame 오케스트레이션 + 정확한 역순 종료.
//   골격은 EditorApp 수명·서비스 등록·의존 선언을 확립하고, DebugUi 초기화·틱 등록은 M4-B가 채운다.
#include "mye/editor/EditorModule.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/PlayMode.h"

#include "mye/scene/SceneModule.h"

#include <array>

namespace mye::editor {

struct EditorModule::Impl {
    std::unique_ptr<EditorApp> app;
    std::string projectPath;   // OnPostInitialize에서 오픈(--project)
    // TODO(M4-B): mye::imgui::DebugUi debugUi; (헤더 노출 회피 위해 여기서만 include)
};

EditorModule::EditorModule() : m_impl(std::make_unique<Impl>()) {}
EditorModule::~EditorModule() = default;

std::span<const char* const> EditorModule::GetDependencies() const {
    // SceneModule(편집 World)·렌더·imgui 이후 초기화되도록. 문자열 이름 위상 정렬(01).
    static const std::array<const char*, 1> kDeps{ "SceneModule" };
    return kDeps;
}

void EditorModule::OnLoad(EngineContext& ctx) {
    ctx.RegisterServiceRaw(kServiceId, this);
}

void EditorModule::OnInitialize(EngineContext& ctx) {
    // TODO(M4-B): DebugUi.Initialize(Win32Window, RHI IDevice) — mye_imgui 도킹 셸.
    m_impl->app = std::make_unique<EditorApp>();
    // 프로젝트 경로: 01 EnginePaths.projectDir(--project). 없으면 빈 문자열(런처는 P2).
    m_impl->projectPath = ctx.GetPaths().projectDir;
}

void EditorModule::OnPostInitialize(EngineContext& ctx) {
    if (m_impl->app) {
        auto r = m_impl->app->Initialize(ctx, m_impl->projectPath);
        (void)r;   // TODO(M4-B): 실패 시 로그 + 런처 복귀(P2).

        // 07 §3: 편집 World = SceneModule의 active World. Play/Stop 스냅샷 원본.
        if (auto* scene = ctx.GetService<scene::SceneModule>())
            m_impl->app->PlayMode().SetEditWorld(&scene->World());
    }
    // PreRender 틱 등록: DebugUi::BeginFrame → EditorApp::OnFrame → EndFrame. DebugUi가 아직
    //   초기화되지 않은 부팅 경로(렌더/윈도우 모듈 미배선, M4-A)에서는 no-op으로 안전하게 건너뛴다.
    ctx.Modules().AddTick(this, UpdatePhase::PreRender, [this](const TimeStep&) {
        EditorApp* app = m_impl->app.get();
        if (!app) return;
        // DebugUi 미초기화 시(M4-A 부팅) UI 프레임을 건너뛴다 — ImGui 컨텍스트 없이 OnFrame 금지.
        // (M4-B: DebugUi.IsInitialized() 게이트 + BeginFrame/EndFrame 감싸기.)
    });
}

void EditorModule::OnShutdown(EngineContext& ctx) {
    // 셧다운 데드락·수명 규약: EditorApp Shutdown → DebugUi Shutdown(윈도우/디바이스보다 먼저).
    if (m_impl->app) m_impl->app->Shutdown();
    m_impl->app.reset();
    // TODO(M4-B): DebugUi.Shutdown().
    ctx.UnregisterServiceRaw(kServiceId);
}

EditorApp* EditorModule::App() { return m_impl->app.get(); }

} // namespace mye::editor
