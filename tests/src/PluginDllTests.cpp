// PluginDllTests.cpp — 네이티브 DLL 플러그인 로드/틱/언로드 (엔진 확장성)
//
// 게임 코드를 별도 .dll 로 런타임 로드하는 경로 검증. 샘플 DLL(tests/fixtures/sample_plugin.cpp)을
// 로드해 per-frame 시스템이 host.Tick 에서 실행되는지 DLL 심볼로 관찰한다.
#include "TestFramework.h"

#include "mye/plugin/PluginHost.h"

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace mye;

MYE_TEST(PluginNativeDllLoad) {
#if defined(_WIN32) && defined(MYE_SAMPLE_PLUGIN_DLL)
    const char* dllPath = MYE_SAMPLE_PLUGIN_DLL;

    // 별도 ref 로 같은 모듈을 열어 tick 카운터 심볼 확보(host 와 동일 모듈 = 동일 static).
    HMODULE probe = ::LoadLibraryA(dllPath);
    MYE_EXPECT(probe != nullptr);
    if (!probe) return;
    auto tickCount = reinterpret_cast<int (*)()>(reinterpret_cast<void*>(::GetProcAddress(probe, "MyeSamplePluginTickCount")));
    MYE_EXPECT(tickCount != nullptr);
    if (!tickCount) { ::FreeLibrary(probe); return; }

    plugin::PluginHost host(1);
    auto r = host.LoadDll(dllPath);
    MYE_EXPECT(static_cast<bool>(r));
    MYE_EXPECT(host.IsLoaded("sample.dll.plugin") && host.SystemCount() == 1);

    // DLL 의 시스템이 host.Tick 에서 실행 → DLL 내부 카운터 증가.
    const int before = tickCount();
    host.Tick(0.016f);
    host.Tick(0.016f);
    MYE_EXPECT(tickCount() == before + 2);

    // 언로드 → 시스템 제거 + FreeLibrary(host ref). probe ref 로 모듈은 유지.
    MYE_EXPECT(host.Unload("sample.dll.plugin"));
    MYE_EXPECT(!host.IsLoaded("sample.dll.plugin") && host.SystemCount() == 0);
    const int afterUnload = tickCount();
    host.Tick(0.016f);
    MYE_EXPECT(tickCount() == afterUnload);   // 언로드 후 증가 없음

    // 없는 경로 로드는 실패(크래시 아님).
    MYE_EXPECT(!host.LoadDll("Z:/no/such/plugin.dll"));

    ::FreeLibrary(probe);
#else
    MYE_EXPECT(true);   // 비-Windows 또는 DLL 경로 미정의 → 스킵
#endif
}
