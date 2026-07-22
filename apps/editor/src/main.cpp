// MyEditor — 에디터 실행 파일 진입점 (docs/07 §1, §2)
//
// L5의 얇은 앱. 게임과 동일한 엔진 모듈 스택 위에 EditorModule을 얹는다. 프로젝트는 커맨드라인
//   `--project path/to/Game.myeproj`로 연다(런처 UI는 P2). M4-A는 골격 부팅 경로만 확립하며,
//   실제 도킹 셸·뷰포트·패널 렌더는 M4-B 구현 에이전트가 채운다.
#include "mye/core/App.h"

#include "mye/scene/SceneModule.h"
#include "mye/editor/EditorModule.h"

#include <memory>

namespace mye {

// 에디터 앱 — 정적 모듈(씬 + 에디터)만 등록한다. 렌더·에셋 등 하위 계층 모듈 배선은 M4-B에서
//   엔진 표준 부팅 경로에 맞춰 확장한다(현재는 컴파일·부팅 골격 확인이 목적).
class MyEditorApp final : public Application {
public:
    explicit MyEditorApp(const LaunchArgs& /*args*/) {}

    void OnConfigure(ConfigSystem& /*config*/) override {}

    void OnRegisterModules(ModuleRegistry& modules) override {
        modules.Register(std::make_unique<scene::SceneModule>());
        modules.Register(std::make_unique<editor::EditorModule>());
    }

    void OnStart(EngineContext& /*ctx*/) override {}
    void OnStop(EngineContext& /*ctx*/) override {}
};

Application* CreateApplication(const LaunchArgs& args) {
    return new MyEditorApp(args);
}

} // namespace mye

// 진입점 — GuardedMain(int,char**) 오버로드가 유니코드 커맨드라인 변환 + `--project=` 파싱을
//   수행한다(core App.cpp 규약). 실행 파일은 표준 main을 정의하는 엔진 규약을 따른다.
int main(int argc, char** argv) {
    return mye::GuardedMain(argc, argv);
}
