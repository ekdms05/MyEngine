// TestMain.cpp — 테스트 러너 엔트리 (실패 개수 = 종료 코드)
#include "TestFramework.h"
#include "mye/core/App.h"

// Native window tests link core's platform entry points without launching an app loop.
namespace mye {
Application* CreateApplication(const LaunchArgs&) { return nullptr; }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    return mye::test::RunAll();
}
