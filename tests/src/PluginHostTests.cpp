// PluginHostTests.cpp — 플러그인 로드/언로드·틱·버전 게이팅·타입 해제 (엔진 확장성)
#include "TestFramework.h"

#include "mye/plugin/PluginHost.h"
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"

#include <memory>
#include <string>

using namespace mye;
using namespace mye::plugin;

// ---- 플러그인이 등록하는 게임 타입(엔진에 없는 확장 타입) ----
namespace plugintest {
struct Buff { std::int32_t power = 0; std::int32_t durationTicks = 0; };
}
MYE_REFLECT(plugintest::Buff);
template <> void mye::refl::Reflect(TypeBuilder<plugintest::Buff>& b) {
    b.Version(1)
     .Field("power", &plugintest::Buff::power)
     .Field("durationTicks", &plugintest::Buff::durationTicks);
}

namespace {
// 리플렉션 타입 + per-frame 시스템을 등록하는 샘플 플러그인. tickCount 로 시스템 동작 관찰.
struct SamplePlugin : IPlugin {
    int* tickCounter = nullptr;
    uint32_t minVer = 0;
    explicit SamplePlugin(int* counter, uint32_t minEngineVer = 0) : tickCounter(counter), minVer(minEngineVer) {}

    PluginInfo Info() const override {
        return PluginInfo{ "sample.buffs", 1, minVer, "샘플 버프 플러그인" };
    }
    void OnLoad(PluginContext& ctx) override {
        ctx.RegisterType<plugintest::Buff>();
        ctx.RegisterSystem("buff_tick", [this](float){ if (tickCounter) ++(*tickCounter); }, 0);
    }
    void OnUnload(PluginContext&) override {}
};
}

MYE_TEST(PluginLoadRegistersTypeAndSystem) {
    // 엔진버전 10 호스트.
    PluginHost host(10);
    int ticks = 0;

    MYE_EXPECT(static_cast<bool>(host.Load(std::make_unique<SamplePlugin>(&ticks))));
    MYE_EXPECT(host.Count() == 1);
    MYE_EXPECT(host.IsLoaded("sample.buffs"));
    MYE_EXPECT(host.SystemCount() == 1);

    // 플러그인이 등록한 타입이 레지스트리에 존재.
    MYE_EXPECT(refl::TypeRegistry::Get().Find("plugintest::Buff") != nullptr);

    // 틱이 플러그인 시스템을 구동.
    host.Tick(0.016f);
    host.Tick(0.016f);
    MYE_EXPECT(ticks == 2);

    // 중복 이름 로드 거부.
    MYE_EXPECT(!host.Load(std::make_unique<SamplePlugin>(&ticks)));

    // 언로드 → 시스템 제거(더 이상 틱 안 됨).
    MYE_EXPECT(host.Unload("sample.buffs"));
    MYE_EXPECT(host.Count() == 0 && host.SystemCount() == 0);
    host.Tick(0.016f);
    MYE_EXPECT(ticks == 2);   // 언로드 후 증가 없음
    MYE_EXPECT(!host.Unload("sample.buffs"));   // 이미 없음
}

MYE_TEST(PluginVersionGating) {
    PluginHost host(5);   // 엔진버전 5
    int ticks = 0;

    // 필요 엔진버전 9 > 5 → 로드 거부.
    MYE_EXPECT(!host.Load(std::make_unique<SamplePlugin>(&ticks, 9)));
    MYE_EXPECT(host.Count() == 0);

    // 필요 엔진버전 5 <= 5 → 허용.
    MYE_EXPECT(static_cast<bool>(host.Load(std::make_unique<SamplePlugin>(&ticks, 5))));
    MYE_EXPECT(host.Count() == 1);
}

MYE_TEST(PluginHostUnloadsAllOnDestroy) {
    int ticks = 0;
    {
        PluginHost host(1);
        MYE_EXPECT(static_cast<bool>(host.Load(std::make_unique<SamplePlugin>(&ticks))));
        MYE_EXPECT(host.Count() == 1);
        // 소멸자가 남은 플러그인 언로드(누수·dangling 방지).
    }
    // 소멸 후 재로드 가능(타입 재등록 멱등).
    PluginHost host2(1);
    MYE_EXPECT(static_cast<bool>(host2.Load(std::make_unique<SamplePlugin>(&ticks))));
    MYE_EXPECT(host2.IsLoaded("sample.buffs"));
}
