// GameplayProgressionTests.cpp — 경험치/레벨업 + 스킬 쿨다운 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Stats.h"
#include "mye/gameplay/StatSystem.h"
#include "mye/gameplay/Progression.h"
#include "mye/gameplay/Skill.h"
#include "mye/core/Math.h"

using namespace mye;
using namespace mye::gameplay;

MYE_TEST(ProgressionLevelUpGrowsStats) {
    Progression p; p.level = 1;
    Stats s; s.base = BaseAttributes{1, 10, 10, 10, 10};
    ComputeDerived(s, true);
    const int32_t hp1 = s.derived.maxHp;

    // level1→2 필요 = XpForLevel(1) = 50+50 = 100. 100 주면 정확히 1레벨.
    MYE_EXPECT(XpForLevel(1) == 100);
    MYE_EXPECT(GainXp(p, s, 100) == 1);
    MYE_EXPECT(p.level == 2);
    MYE_EXPECT(p.xp == 0);
    MYE_EXPECT(s.base.level == 2);
    MYE_EXPECT(s.base.strength == 12 && s.base.vitality == 12);   // 성장
    MYE_EXPECT(s.dirty);
    ComputeDerived(s);
    MYE_EXPECT(s.derived.maxHp > hp1);   // 레벨·체력 증가로 HP 상승

    // 한 번에 여러 레벨: 필요 = XpForLevel(2)=300 + XpForLevel(3)=600 = 900 → +2레벨(2→4).
    MYE_EXPECT(XpForLevel(2) == 300 && XpForLevel(3) == 600);
    p.xp = 0;
    MYE_EXPECT(GainXp(p, s, 900) == 2);
    MYE_EXPECT(p.level == 4);
    MYE_EXPECT(p.xp == 0);
}

MYE_TEST(ProgressionMaxLevelCap) {
    Progression p; p.level = 98; p.maxLevel = 99;
    Stats s; s.base.level = 98;
    // 대량 XP → 99에서 캡, 초과 XP는 버림.
    const int32_t gained = GainXp(p, s, 1'000'000);
    MYE_EXPECT(p.level == 99);
    MYE_EXPECT(gained == 1);
    MYE_EXPECT(p.xp == 0);
}

MYE_TEST(SkillCooldownAndMpGate) {
    SkillState st;
    Stats caster; caster.mp = 30; caster.derived.maxMp = 100;
    const SkillDef fireball{/*id*/ 1, /*cd*/ 2.0f, /*mp*/ 20, /*power*/ 3.0f, DamageType::Magic};

    MYE_EXPECT(CanCast(st, caster, fireball));
    MYE_EXPECT(TryCast(st, caster, fireball));
    MYE_EXPECT(caster.mp == 10);                       // MP 차감
    MYE_EXPECT(ApproxEqual(CooldownRemaining(st, 1), 2.0f));

    // 쿨다운 중 → 재시전 불가.
    MYE_EXPECT(!CanCast(st, caster, fireball));
    MYE_EXPECT(!TryCast(st, caster, fireball));
    MYE_EXPECT(caster.mp == 10);                       // 변화 없음

    // 쿨다운 경과 → 다시 가능(단 MP 부족: 10 < 20).
    TickCooldowns(st, 2.5f);
    MYE_EXPECT(ApproxEqual(CooldownRemaining(st, 1), 0.0f));
    MYE_EXPECT(!CanCast(st, caster, fireball));        // MP 부족

    // MP 회복 후 시전 가능.
    caster.mp = 50;
    MYE_EXPECT(CanCast(st, caster, fireball));
    MYE_EXPECT(TryCast(st, caster, fireball));
    MYE_EXPECT(caster.mp == 30);
}
