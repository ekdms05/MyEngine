// GameplayStatsTests.cpp — RPG 스탯 파생 계산 검증 (docs/mmorpg/04, M8)
//
// 순수 로직(GPU 무관). 파생 공식·수정자(flat/percent)·자원 클램프·World 일괄 재계산.
#include "TestFramework.h"

#include "mye/gameplay/Stats.h"
#include "mye/gameplay/StatSystem.h"
#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/core/Math.h"

using namespace mye;
using namespace mye::gameplay;

MYE_TEST(StatsDerivedFormula) {
    Stats s;
    s.base = BaseAttributes{/*level*/ 5, /*str*/ 20, /*agi*/ 15, /*int*/ 12, /*vit*/ 18};
    ComputeDerived(s, /*fillToMax*/ true);

    MYE_EXPECT(s.derived.maxHp == 18 * 12 + 5 * 25);   // 341
    MYE_EXPECT(s.derived.maxMp == 12 * 8 + 5 * 12);    // 156
    MYE_EXPECT(s.derived.attack == 20 * 2 + 5);        // 45
    MYE_EXPECT(s.derived.defense == 26);               // 18 + 7.5 → 반올림 26
    MYE_EXPECT(ApproxEqual(s.derived.critChance, 0.06f));
    MYE_EXPECT(ApproxEqual(s.derived.attackSpeed, 1.0f));
    MYE_EXPECT(s.hp == 341 && s.mp == 156);            // fillToMax
    MYE_EXPECT(!s.dirty);
}

MYE_TEST(StatsModifiersFlatAndPercent) {
    Stats s;
    s.base = BaseAttributes{5, 20, 15, 12, 18};
    s.modifiers.push_back(StatModifier{StatField::Attack, /*flat*/ 50.0f, /*percent*/ 0.2f, 1});
    ComputeDerived(s, true);
    // attack = (45 + 50) * 1.2 = 114
    MYE_EXPECT(s.derived.attack == 114);
}

MYE_TEST(StatsResourceClamp) {
    Stats s;
    s.base = BaseAttributes{1, 10, 10, 10, 10};
    s.hp = 9999;   // 과대 → maxHp로 클램프
    s.mp = -5;     // 음수 → 0으로 클램프
    ComputeDerived(s, /*fillToMax*/ false);
    MYE_EXPECT(s.hp == s.derived.maxHp);
    MYE_EXPECT(s.mp == 0);
}

MYE_TEST(StatsSystemRecomputesDirty) {
    ecs::World world;
    world.RegisterComponent(ecs::MakeComponentTypeDesc<Stats>("Stats"));
    ecs::Entity e = world.Create();
    auto* s = static_cast<Stats*>(world.AddDynamic(e, Stats::kComponentTypeId));
    MYE_EXPECT(s != nullptr);
    s->base = BaseAttributes{10, 30, 20, 25, 40};
    s->dirty = true;

    RunStatSystem(world);

    const auto* s2 = static_cast<const Stats*>(world.TryGetDynamic(e, Stats::kComponentTypeId));
    MYE_EXPECT(s2 != nullptr);
    MYE_EXPECT(!s2->dirty);
    MYE_EXPECT(s2->derived.maxHp == 40 * 12 + 10 * 25);   // 730
    MYE_EXPECT(s2->derived.attack == 30 * 2 + 10);        // 70
}
