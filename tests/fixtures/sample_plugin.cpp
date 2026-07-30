// tests/fixtures/sample_plugin.cpp — 네이티브 DLL 플러그인 테스트 픽스처
//
// PluginHost::LoadDll 검증용 샘플 DLL. C ABI 엔트리(MyeCreatePlugin/MyeDestroyPlugin)를 export 하고,
// per-frame 시스템이 DLL 내부 카운터를 증가시킨다. 테스트는 MyeSamplePluginTickCount 로 관찰한다.
#include "mye/plugin/Plugin.h"

namespace {
int g_tickCount = 0;

struct SamplePlugin : mye::plugin::IPlugin {
    mye::plugin::PluginInfo Info() const override {
        return mye::plugin::PluginInfo{ "sample.dll.plugin", 1, 0, "DLL 로딩 테스트 플러그인" };
    }
    void OnLoad(mye::plugin::PluginContext& ctx) override {
        ctx.RegisterSystem("dll_tick", [](float) { ++g_tickCount; }, 0);
    }
    void OnUnload(mye::plugin::PluginContext&) override {}
};
} // namespace

MYE_PLUGIN_EXPORT mye::plugin::IPlugin* MyeCreatePlugin() { return new SamplePlugin(); }
MYE_PLUGIN_EXPORT void MyeDestroyPlugin(mye::plugin::IPlugin* p) { delete p; }
MYE_PLUGIN_EXPORT int MyeSamplePluginTickCount() { return g_tickCount; }
