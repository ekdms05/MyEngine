// MmoJobHuntTests.cpp — 3직업 역할 + 파티 사냥 시너지 (게임 수직슬라이스)
#include "TestFramework.h"

#include "mye/mmo/Jobs.h"
#include "mye/mmo/PartyHunt.h"

#include <vector>

using namespace mye;
using namespace mye::mmo;

MYE_TEST(JobRolesAreDistinct) {
    const gameplay::Stats sword = MakeJobStats(JobClass::Swordsman, 10);
    const gameplay::Stats mage  = MakeJobStats(JobClass::Mage, 10);
    const gameplay::Stats buff  = MakeJobStats(JobClass::Buffer, 10);

    // 검사: 최고 HP·방어(탱커).
    MYE_EXPECT(sword.derived.maxHp > mage.derived.maxHp);
    MYE_EXPECT(sword.derived.maxHp > buff.derived.maxHp);
    MYE_EXPECT(sword.derived.defense > mage.derived.defense);

    // 마법사: 최고 MP·높은 공격력(INT 기반 마법 딜러) + 물몸.
    MYE_EXPECT(mage.derived.maxMp > sword.derived.maxMp);
    MYE_EXPECT(mage.derived.attack > buff.derived.attack);          // 버퍼보다 강한 딜
    MYE_EXPECT(mage.derived.maxHp < sword.derived.maxHp);           // 물몸

    // 검사 vs 마법사: 딜은 둘 다 높되(역할 다름), 마법사는 마법·검사는 물리.
    MYE_EXPECT(sword.derived.attack > 0 && mage.derived.attack > 0);
    MYE_EXPECT(GetJob(JobClass::Swordsman).attackType == gameplay::DamageType::Physical);
    MYE_EXPECT(GetJob(JobClass::Mage).attackType == gameplay::DamageType::Magic);

    // 직업명(한국어).
    MYE_EXPECT(JobName(JobClass::Swordsman) == "검사");
    MYE_EXPECT(JobName(JobClass::Mage) == "마법사");
    MYE_EXPECT(JobName(JobClass::Buffer) == "버퍼");

    // 레벨 성장: 높은 레벨이 더 강함.
    const gameplay::Stats sword20 = MakeJobStats(JobClass::Swordsman, 20);
    MYE_EXPECT(sword20.derived.attack > sword.derived.attack && sword20.derived.maxHp > sword.derived.maxHp);
}

MYE_TEST(PartyBuffBoostsAttack) {
    // 버퍼의 +30% 공격 버프가 검사·마법사 모두에게 이득(마법사도 INT→attack 이므로 % 적용됨).
    gameplay::Stats sword = MakeJobStats(JobClass::Swordsman, 10);
    gameplay::Stats mage  = MakeJobStats(JobClass::Mage, 10);
    const int32_t swordBase = sword.derived.attack;
    const int32_t mageBase  = mage.derived.attack;

    ApplyPartyAttackBuff(sword, 0.30f);
    ApplyPartyAttackBuff(mage, 0.30f);
    MYE_EXPECT(sword.derived.attack > swordBase);
    MYE_EXPECT(mage.derived.attack > mageBase);       // 마법사도 버프 수혜
    MYE_EXPECT(mage.derived.attack >= static_cast<int32_t>(mageBase * 1.29f));

    // 버프 해제 → 원복.
    RemovePartyAttackBuff(sword);
    MYE_EXPECT(sword.derived.attack == swordBase);
}

MYE_TEST(PartyHuntSynergyWithBuffer) {
    // 검사·마법사·버퍼 파티가 몹을 사냥. 버퍼 버프가 있으면 더 빨리 잡는다(파티 상호의존).
    auto makeParty = []() {
        std::vector<HuntMember> p;
        p.push_back(HuntMember{ JobClass::Swordsman, MakeJobStats(JobClass::Swordsman, 12), 0, true });
        p.push_back(HuntMember{ JobClass::Mage,      MakeJobStats(JobClass::Mage, 12),      0, true });
        p.push_back(HuntMember{ JobClass::Buffer,    MakeJobStats(JobClass::Buffer, 12),    0, true });
        return p;
    };

    const uint64_t seed = 20260731;

    // 버퍼 버프 없이.
    auto partyNoBuff = makeParty();
    HuntResult noBuff = SimulateHunt(partyNoBuff, MakeMonster(12, 1.4f), /*useBuffer=*/false, 0.30f, seed);

    // 버퍼 버프 있이(같은 몹·시드).
    auto partyBuff = makeParty();
    HuntResult withBuff = SimulateHunt(partyBuff, MakeMonster(12, 1.4f), /*useBuffer=*/true, 0.30f, seed);

    // 둘 다 승리하되, 버프가 있으면 더 적은 라운드에 잡는다.
    MYE_EXPECT(noBuff.victory && withBuff.victory);
    MYE_EXPECT(withBuff.rounds < noBuff.rounds);   // 버퍼 시너지 = 더 빠른 사냥

    // 각자 역할에서 기여(성취감): 검사·마법사 모두 데미지를 냈다.
    int64_t swordDmg = 0, mageDmg = 0;
    for (const HuntMember& m : partyBuff) {
        if (m.cls == JobClass::Swordsman) swordDmg = m.damageDealt;
        if (m.cls == JobClass::Mage)      mageDmg = m.damageDealt;
    }
    MYE_EXPECT(swordDmg > 0 && mageDmg > 0);
    // 검사(탱커)가 어그로를 받아 앞에서 버틴다 — 최소한 살아서 딜을 이어감(파티 생존).
    MYE_EXPECT(withBuff.survivors >= 1);
}
