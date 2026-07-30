// PluginGameIntegrationTests.cpp — 확장성 삼위일체 엔드투엔드 (엔진 확장성 검증)
//
// 게임(MMORPG) 로직이 엔진 코어 무수정으로 붙는 레퍼런스 패턴을 증명한다:
//   플러그인이 (1) 데이터드리븐 컴포넌트 스키마를 등록하고, (2) 엔티티에 부착하고,
//   (3) 그 컴포넌트를 매 틱 처리하는 시스템을 등록한다. PluginHost 가 로드·틱·언로드한다.
#include "TestFramework.h"

#include "mye/plugin/PluginHost.h"
#include "mye/ddc/DynamicComponentStore.h"

#include <memory>

using namespace mye;

namespace {

// "게임" 플러그인 — 엔진에 포함되지 않는 확장. 게임 서비스(스키마·스토어)를 주입받아
//   HP 재생 규칙을 데이터+시스템으로 구현한다.
struct RegenPlugin : plugin::IPlugin {
    ddc::SchemaRegistry*        registry = nullptr;
    ddc::DynamicComponentStore* store = nullptr;
    RegenPlugin(ddc::SchemaRegistry* r, ddc::DynamicComponentStore* s) : registry(r), store(s) {}

    plugin::PluginInfo Info() const override {
        return plugin::PluginInfo{ "sample.regen", 1, 0, "HP 재생(데이터+시스템)" };
    }

    void OnLoad(plugin::PluginContext& ctx) override {
        // (1) 데이터로 컴포넌트 정의.
        ddc::ComponentSchema health{"Health"};
        health.AddField({"cur", ddc::FieldType::I32, 1})
              .AddField({"max", ddc::FieldType::I32, 10})
              .AddField({"regen", ddc::FieldType::I32, 2});
        (void)registry->Register(health);

        // (2) 엔티티에 부착(초기 HP 낮게).
        store->Add(1, "Health", *registry)->SetInt("cur", 1);
        ddc::DynamicComponent* c2 = store->Add(2, "Health", *registry);
        c2->SetInt("cur", 8); c2->SetInt("max", 8);   // 이미 만렙 HP

        // (3) 매 틱 HP 재생 시스템 등록(store 캡처).
        ddc::DynamicComponentStore* s = store;
        ctx.RegisterSystem("hp_regen", [s](float /*dt*/) {
            s->ForEach("Health", [](ddc::EntityId, ddc::DynamicComponent& h) {
                const int64_t cur = h.GetInt("cur");
                const int64_t mx  = h.GetInt("max");
                const int64_t rg  = h.GetInt("regen");
                int64_t next = cur + rg;
                if (next > mx) next = mx;   // 만렙 클램프
                h.SetInt("cur", next);
            });
        });
    }

    void OnUnload(plugin::PluginContext&) override {}
};

} // namespace

MYE_TEST(PluginGameRegenSystemEndToEnd) {
    ddc::SchemaRegistry registry;
    ddc::DynamicComponentStore store;
    plugin::PluginHost host(1);

    // 게임 플러그인 로드 → 스키마 등록 + 엔티티 부착 + 시스템 등록.
    MYE_EXPECT(static_cast<bool>(host.Load(std::make_unique<RegenPlugin>(&registry, &store))));
    MYE_EXPECT(registry.Find("Health") != nullptr);
    MYE_EXPECT(store.ComponentCount() == 2);
    MYE_EXPECT(host.SystemCount() == 1);

    // 초기 상태.
    MYE_EXPECT(store.Get(1, "Health")->GetInt("cur") == 1);
    MYE_EXPECT(store.Get(2, "Health")->GetInt("cur") == 8);

    // 한 틱: 엔티티1 1→3(+2), 엔티티2 8→8(만렙 클램프).
    host.Tick(0.016f);
    MYE_EXPECT(store.Get(1, "Health")->GetInt("cur") == 3);
    MYE_EXPECT(store.Get(2, "Health")->GetInt("cur") == 8);

    // 여러 틱: 엔티티1 3→5→7→9→10(max 클램프).
    host.Tick(0.016f);   // 5
    host.Tick(0.016f);   // 7
    host.Tick(0.016f);   // 9
    host.Tick(0.016f);   // 10 (클램프)
    host.Tick(0.016f);   // 10 (유지)
    MYE_EXPECT(store.Get(1, "Health")->GetInt("cur") == 10);

    // 언로드 → 시스템 제거(더 이상 재생 안 됨). 데이터는 스토어에 남음(게임 서비스 소유).
    store.Get(1, "Health")->SetInt("cur", 4);
    MYE_EXPECT(host.Unload("sample.regen"));
    MYE_EXPECT(host.SystemCount() == 0);
    host.Tick(0.016f);
    MYE_EXPECT(store.Get(1, "Health")->GetInt("cur") == 4);   // 언로드 후 변화 없음
}
