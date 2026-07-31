// MmoContentTests.cpp — 몹 카탈로그·드롭 + 사냥터 스폰/리스폰 (게임 콘텐츠)
#include "TestFramework.h"

#include "mye/mmo/Monster.h"
#include "mye/mmo/HuntingGround.h"

#include <vector>

using namespace mye;
using namespace mye::mmo;

MYE_TEST(MonsterCatalogAndDrops) {
    MonsterCatalog cat;
    RegisterStarterMonsters(cat);
    MYE_EXPECT(cat.Count() == 3);
    MYE_EXPECT(cat.Name(1) == "슬라임" && cat.Name(2) == "고블린" && cat.Name(3) == "오크");
    MYE_EXPECT(cat.XpReward(3) > cat.XpReward(1));   // 오크가 슬라임보다 경험치 많음

    // 몹 스탯: 강한 몹이 더 튼튼(HP).
    const gameplay::Stats slime = cat.MakeStats(1);
    const gameplay::Stats orc   = cat.MakeStats(3);
    MYE_EXPECT(orc.derived.maxHp > slime.derived.maxHp);

    // 드롭 결정론: 같은 시드 → 같은 드롭.
    gameplay::RngState a(12345), b(12345);
    gameplay::LootDrop d1 = cat.RollDrops(2, a);
    gameplay::LootDrop d2 = cat.RollDrops(2, b);
    MYE_EXPECT(d1.gold == d2.gold);
    MYE_EXPECT(d1.items.size() == d2.items.size());
    // 골드는 정의 범위 내.
    MYE_EXPECT(d1.gold >= 6 && d1.gold <= 18);

    // 미등록 몹 → 빈 드롭·기본 스탯(크래시 아님).
    MYE_EXPECT(cat.RollDrops(999, a).items.empty());
    MYE_EXPECT(cat.MakeStats(999).derived.maxHp > 0);
}

MYE_TEST(HuntingGroundSpawnKillRespawn) {
    MonsterCatalog cat;
    RegisterStarterMonsters(cat);

    HuntingGround field;
    field.id = 1; field.name = "초원"; field.recommendedLevel = 5;
    field.spawns = {
        { 1, 3, 5.0f },   // 슬라임 3마리, 5초 리스폰
        { 2, 2, 8.0f },   // 고블린 2마리, 8초 리스폰
    };

    SpawnManager mgr(field, cat);
    mgr.Populate();
    MYE_EXPECT(mgr.AliveCount() == 5);
    MYE_EXPECT(mgr.AliveCountOf(1) == 3 && mgr.AliveCountOf(2) == 2);

    // 슬라임 1마리 처치 → 리스폰 예약, 즉시 마릿수 감소.
    const uint64_t first = mgr.AliveInstances()[0];
    const MonsterId killedType = mgr.Get(first)->monsterId;
    MYE_EXPECT(mgr.Kill(first));
    MYE_EXPECT(mgr.AliveCount() == 4 && mgr.PendingRespawns() == 1);
    MYE_EXPECT(mgr.Get(first) == nullptr);   // 죽은 인스턴스 제거

    // 리스폰 시간 전 → 아직 안 나옴.
    mgr.Tick(3.0f);
    MYE_EXPECT(mgr.AliveCountOf(killedType) == (killedType == 1 ? 2u : 1u));

    // 리스폰 시간 경과 → 재스폰(원래 마릿수 복귀).
    mgr.Tick(3.0f);   // 총 6초 > 5초(슬라임) / 아직 8초 미만(고블린)
    MYE_EXPECT(mgr.PendingRespawns() == 0);
    MYE_EXPECT(mgr.AliveCount() == 5);
    MYE_EXPECT(mgr.AliveCountOf(1) == 3 && mgr.AliveCountOf(2) == 2);

    // 없는 인스턴스 처치 실패.
    MYE_EXPECT(!mgr.Kill(999999));
}

MYE_TEST(HuntingGroundRespawnRespectsMaxCount) {
    MonsterCatalog cat;
    RegisterStarterMonsters(cat);

    HuntingGround field;
    field.id = 2; field.name = "동굴";
    field.spawns = { { 3, 1, 4.0f } };   // 오크 1마리만

    SpawnManager mgr(field, cat);
    mgr.Populate();
    MYE_EXPECT(mgr.AliveCount() == 1);

    // 처치·리스폰 반복 후에도 maxCount(1) 초과 안 함.
    for (int cycle = 0; cycle < 3; ++cycle) {
        const uint64_t inst = mgr.AliveInstances()[0];
        MYE_EXPECT(mgr.Kill(inst));
        MYE_EXPECT(mgr.AliveCount() == 0);
        mgr.Tick(5.0f);   // 리스폰
        MYE_EXPECT(mgr.AliveCount() == 1);   // 다시 1마리(초과 없음)
    }
}
