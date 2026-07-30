// DdcStoreTests.cpp — 엔티티↔동적 컴포넌트 스토어·질의·월드 세이브 (엔진 확장성)
#include "TestFramework.h"

#include "mye/ddc/DynamicComponentStore.h"
#include "mye/core/Json.h"

#include <string>
#include <vector>

using namespace mye;
using namespace mye::ddc;

namespace {
SchemaRegistry MakeRegistry() {
    SchemaRegistry reg;
    ComponentSchema health{"Health"};
    health.AddField({"cur", FieldType::I32, 10}).AddField({"max", FieldType::I32, 10});
    ComponentSchema pos{"Position"};
    pos.AddField({"x", FieldType::F32}).AddField({"y", FieldType::F32});
    (void)reg.Register(health);
    (void)reg.Register(pos);
    return reg;
}
}

MYE_TEST(DdcStoreAttachQueryRemove) {
    SchemaRegistry reg = MakeRegistry();
    DynamicComponentStore store;

    // 엔티티에 컴포넌트 부착.
    DynamicComponent* h1 = store.Add(1, "Health", reg);
    MYE_EXPECT(h1 != nullptr && h1->GetInt("max") == 10);
    h1->SetInt("cur", 7);
    MYE_EXPECT(store.Add(2, "Health", reg) != nullptr);
    MYE_EXPECT(store.Add(1, "Position", reg) != nullptr);

    // 미등록 스키마 → nullptr.
    MYE_EXPECT(store.Add(1, "Nope", reg) == nullptr);

    // 조회.
    MYE_EXPECT(store.Has(1, "Health") && store.Has(1, "Position") && !store.Has(2, "Position"));
    MYE_EXPECT(store.Get(1, "Health")->GetInt("cur") == 7);
    MYE_EXPECT(store.Get(9, "Health") == nullptr);
    MYE_EXPECT(store.EntityCount() == 2 && store.ComponentCount() == 3);

    // 중복 Add → 기존 반환(값 유지).
    DynamicComponent* h1b = store.Add(1, "Health", reg);
    MYE_EXPECT(h1b == h1 && h1b->GetInt("cur") == 7);

    // 질의: Health 를 가진 엔티티 순회.
    std::vector<EntityId> withHealth;
    store.ForEach("Health", [&](EntityId e, DynamicComponent&) { withHealth.push_back(e); });
    MYE_EXPECT(withHealth.size() == 2);

    // 컴포넌트 1개 제거.
    MYE_EXPECT(store.Remove(1, "Position"));
    MYE_EXPECT(!store.Has(1, "Position") && store.Has(1, "Health"));
    MYE_EXPECT(!store.Remove(1, "Position"));   // 이미 없음

    // 엔티티 전체 제거.
    store.RemoveEntity(1);
    MYE_EXPECT(!store.Has(1, "Health"));
    MYE_EXPECT(store.EntityCount() == 1);
}

MYE_TEST(DdcStoreWorldSaveRoundtrip) {
    SchemaRegistry reg = MakeRegistry();

    json::Value saved;
    {
        DynamicComponentStore store;
        DynamicComponent* h = store.Add(100, "Health", reg);
        h->SetInt("cur", 3); h->SetInt("max", 20);
        DynamicComponent* p = store.Add(100, "Position", reg);
        p->SetFloat("x", 12.5); p->SetFloat("y", -4.0);
        store.Add(200, "Health", reg)->SetInt("cur", 5);
        saved = store.ToJson();
    }

    // 텍스트 왕복(Stringify → Parse).
    std::string text = json::Stringify(saved);
    auto parsed = json::Parse(text);
    MYE_EXPECT(static_cast<bool>(parsed));

    // 로드해 복원.
    DynamicComponentStore loaded;
    auto n = loaded.FromJson(parsed.Value(), reg);
    MYE_EXPECT(static_cast<bool>(n) && n.Value() == 3);
    MYE_EXPECT(loaded.ComponentCount() == 3);

    const DynamicComponent* h = loaded.Get(100, "Health");
    MYE_EXPECT(h != nullptr && h->GetInt("cur") == 3 && h->GetInt("max") == 20);
    const DynamicComponent* p = loaded.Get(100, "Position");
    MYE_EXPECT(p != nullptr && p->GetFloat("x") == 12.5 && p->GetFloat("y") == -4.0);
    MYE_EXPECT(loaded.Get(200, "Health")->GetInt("cur") == 5);

    // 스키마 미등록이면 해당 항목 건너뜀(부분 로드 안전).
    SchemaRegistry partial;
    ComponentSchema onlyPos{"Position"};
    onlyPos.AddField({"x", FieldType::F32}).AddField({"y", FieldType::F32});
    (void)partial.Register(onlyPos);
    DynamicComponentStore loaded2;
    auto n2 = loaded2.FromJson(parsed.Value(), partial);
    MYE_EXPECT(static_cast<bool>(n2) && n2.Value() == 1);   // Position 만 로드(Health 미등록)
    MYE_EXPECT(loaded2.Get(100, "Position") != nullptr && loaded2.Get(100, "Health") == nullptr);
}
