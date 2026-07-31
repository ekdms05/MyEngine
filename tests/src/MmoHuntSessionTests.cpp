// MmoHuntSessionTests.cpp — 핵심 루프: 파티가 사냥터에서 성장+보상 (게임 캡스톤)
#include "TestFramework.h"

#include "mye/mmo/HuntSession.h"
#include "mye/mmo/Jobs.h"

#include <vector>

using namespace mye;
using namespace mye::mmo;

namespace {
std::vector<HuntMember> MakeParty(int level) {
    std::vector<HuntMember> p;
    p.push_back(HuntMember{ JobClass::Swordsman, MakeJobStats(JobClass::Swordsman, level), 0, true });
    p.push_back(HuntMember{ JobClass::Mage,      MakeJobStats(JobClass::Mage, level),      0, true });
    p.push_back(HuntMember{ JobClass::Buffer,    MakeJobStats(JobClass::Buffer, level),    0, true });
    return p;
}
std::vector<gameplay::Progression> MakeProg(int level) {
    std::vector<gameplay::Progression> pr(3);
    for (auto& p : pr) { p.level = level; p.xp = 0; }
    return pr;
}
SpawnManager MakeField(const MonsterCatalog& cat, HuntingGround& field) {
    field.id = 1; field.name = "초원"; field.spawns = { { 1, 2, 5.0f } };   // 슬라임 2마리
    SpawnManager mgr(field, cat);
    mgr.Populate();
    return mgr;
}
}

MYE_TEST(HuntSessionCoreLoop) {
    MonsterCatalog cat; RegisterStarterMonsters(cat);
    HuntingGround field; SpawnManager mgr = MakeField(cat, field);

    auto party = MakeParty(8);
    auto prog  = MakeProg(8);

    // 파티가 슬라임 8마리 사냥(버퍼 버프 사용).
    HuntSessionResult r = RunHuntSession(party, prog, mgr, cat, 8, /*useBuffer=*/true, 0.30f, 42);

    // 사냥 성공: 목표만큼 처치, 전멸 없음.
    MYE_EXPECT(r.monstersKilled == 8 && !r.wiped);

    // 성장: 멤버당 경험치 획득(슬라임 xp 20 × 8 = 160), 각 멤버 prog 에 누적.
    //   레벨 8 은 XpForLevel(8)=3600 이므로 160 으론 레벨업 안 됨 → prog.xp 로 누적.
    MYE_EXPECT(r.xpPerMember == 160);
    for (const auto& p : prog) { MYE_EXPECT(p.level == 8 && p.xp == 160); }

    // 보상: 드롭(골드 or 아이템) 획득 — 함께 사냥한 성과.
    MYE_EXPECT(r.loot.gold > 0 || !r.loot.items.empty());
}

MYE_TEST(HuntSessionLevelUpGrowsParty) {
    MonsterCatalog cat; RegisterStarterMonsters(cat);
    HuntingGround field; SpawnManager mgr = MakeField(cat, field);

    // 저레벨 파티(레벨 5) → 사냥으로 레벨업·스탯 성장 관찰.
    auto party = MakeParty(5);
    auto prog  = MakeProg(5);
    const int32_t swordAtkBefore = party[0].stats.derived.attack;

    // 넉넉히 사냥(레벨5 XpForLevel(5)=1500, 슬라임 20 → 75마리 필요 → 목표 넉넉히).
    HuntSessionResult r = RunHuntSession(party, prog, mgr, cat, 90, /*useBuffer=*/true, 0.30f, 7);
    MYE_EXPECT(!r.wiped && r.monstersKilled == 90);

    // 레벨업 발생 → 검사 스탯 성장(공격력 증가).
    MYE_EXPECT(r.totalLevelUps > 0);
    MYE_EXPECT(prog[0].level > 5);
    MYE_EXPECT(party[0].stats.derived.attack > swordAtkBefore);   // 성장 체감

    // 누적 보상.
    MYE_EXPECT(r.loot.gold > 0);
}
