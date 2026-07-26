// GameplayCombatTests.cpp — 전투 데미지 계산 검증 (docs/mmorpg/04, M8)
//
// 결정론적(서버권위) 데미지 — 방어 감쇠·크리·True·최소데미지·적용·회복·RNG 재현성.
#include "TestFramework.h"

#include "mye/gameplay/Stats.h"
#include "mye/gameplay/Combat.h"

using namespace mye;
using namespace mye::gameplay;

namespace {
Stats MakeUnit(int32_t attack, int32_t defense, float crit, int32_t maxHp) {
    Stats s;
    s.derived.attack = attack;
    s.derived.defense = defense;
    s.derived.critChance = crit;
    s.derived.maxHp = maxHp;
    s.hp = maxHp;
    return s;
}
} // namespace

MYE_TEST(CombatPhysicalMitigation) {
    Stats atk = MakeUnit(/*atk*/ 100, 0, /*crit*/ 0.0f, 100);
    Stats def = MakeUnit(0, /*def*/ 100, 0.0f, 500);
    RngState rng(1);
    // base=100, 감쇠 100/(100+100)=0.5 → 50, 크리 없음.
    DamageResult r = ComputeDamage(atk, def, AttackSpec{1.0f, DamageType::Physical, 1.5f, 0}, rng);
    MYE_EXPECT(r.amount == 50);
    MYE_EXPECT(!r.crit);
}

MYE_TEST(CombatCritAndTrue) {
    Stats atk = MakeUnit(100, 0, /*crit*/ 1.0f, 100);   // 항상 크리
    Stats def = MakeUnit(0, 100, 0.0f, 500);
    RngState rng(7);
    DamageResult crit = ComputeDamage(atk, def, AttackSpec{1.0f, DamageType::Physical, 1.5f, 0}, rng);
    MYE_EXPECT(crit.crit);
    MYE_EXPECT(crit.amount == 75);   // 50 * 1.5

    // True 데미지는 방어 무시: base=100, 크리 1.5 → 150.
    RngState rng2(7);
    DamageResult tr = ComputeDamage(atk, def, AttackSpec{1.0f, DamageType::True, 1.5f, 0}, rng2);
    MYE_EXPECT(tr.amount == 150);
}

MYE_TEST(CombatMinimumOne) {
    Stats atk = MakeUnit(1, 0, 0.0f, 100);
    Stats def = MakeUnit(0, 9999, 0.0f, 500);
    RngState rng(1);
    DamageResult r = ComputeDamage(atk, def, AttackSpec{0.01f, DamageType::Physical, 1.5f, 0}, rng);
    MYE_EXPECT(r.amount == 1);   // 최소 1
}

MYE_TEST(CombatApplyDamageAndHeal) {
    Stats t = MakeUnit(0, 0, 0.0f, 100);
    t.hp = 50;
    MYE_EXPECT(!ApplyDamage(t, 30));   // 50→20, 생존
    MYE_EXPECT(t.hp == 20);
    MYE_EXPECT(ApplyDamage(t, 100));   // 20→0, 사망
    MYE_EXPECT(t.hp == 0);

    t.hp = 20;
    MYE_EXPECT(Heal(t, 50) == 50);     // 20→70
    MYE_EXPECT(t.hp == 70);
    MYE_EXPECT(Heal(t, 100) == 30);    // 70→100(캡)
    MYE_EXPECT(t.hp == 100);
}

MYE_TEST(CombatRngDeterministic) {
    RngState a(12345), b(12345);
    for (int i = 0; i < 8; ++i) MYE_EXPECT(NextU64(a) == NextU64(b));
    // 다른 시드 → 다른 값(거의 확실).
    RngState c(1), d(2);
    MYE_EXPECT(NextU64(c) != NextU64(d));
}
