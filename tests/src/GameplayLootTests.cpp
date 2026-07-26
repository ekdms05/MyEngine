// GameplayLootTests.cpp — 결정론적 루트 테이블 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Loot.h"

using namespace mye;
using namespace mye::gameplay;

MYE_TEST(LootDeterministic) {
    LootTable t;
    t.entries.push_back(LootEntry{100, /*weight*/ 3, 1, 3});
    t.entries.push_back(LootEntry{200, /*weight*/ 1, 1, 1});
    t.rolls = 5;
    t.goldMin = 10; t.goldMax = 50;

    RngState a(999), b(999);
    LootDrop da = RollLoot(t, a);
    LootDrop db = RollLoot(t, b);
    // 같은 시드 → 완전히 동일한 드롭.
    MYE_EXPECT(da.gold == db.gold);
    MYE_EXPECT(da.items.size() == db.items.size());
    for (size_t i = 0; i < da.items.size(); ++i) {
        MYE_EXPECT(da.items[i].itemId == db.items[i].itemId);
        MYE_EXPECT(da.items[i].count == db.items[i].count);
    }
    MYE_EXPECT(da.gold >= 10 && da.gold <= 50);
}

MYE_TEST(LootSingleEntryAlwaysDrops) {
    LootTable t;
    t.entries.push_back(LootEntry{100, 1, /*min*/ 2, /*max*/ 4});
    t.rolls = 1;
    RngState rng(1);
    LootDrop d = RollLoot(t, rng);
    MYE_EXPECT(d.items.size() == 1);
    MYE_EXPECT(d.items[0].itemId == 100);
    MYE_EXPECT(d.items[0].count >= 2 && d.items[0].count <= 4);
}

MYE_TEST(LootRollsMergeSameItem) {
    LootTable t;
    t.entries.push_back(LootEntry{100, 1, 1, 1});   // 유일 엔트리 → 매 roll 100 획득
    t.rolls = 4;
    RngState rng(5);
    LootDrop d = RollLoot(t, rng);
    // 4회 모두 100 → 한 스택으로 합산, count==4.
    MYE_EXPECT(d.items.size() == 1);
    MYE_EXPECT(d.items[0].itemId == 100 && d.items[0].count == 4);
}

MYE_TEST(LootFixedGold) {
    LootTable t;
    t.goldMin = 77; t.goldMax = 77;   // 고정
    RngState rng(1);
    LootDrop d = RollLoot(t, rng);
    MYE_EXPECT(d.gold == 77);
    MYE_EXPECT(d.items.empty());       // 아이템 엔트리 없음
}
