// GameplayStatusTests.cpp — 버프·상태이상(수정자·DoT·플래그·만료) 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Stats.h"
#include "mye/gameplay/StatSystem.h"
#include "mye/gameplay/StatusEffect.h"

using namespace mye;
using namespace mye::gameplay;

MYE_TEST(StatusBuffAppliesAndExpires) {
    Stats s; s.base = BaseAttributes{5, 20, 15, 12, 18};
    ComputeDerived(s, true);
    const int32_t baseAttack = s.derived.attack;   // 45

    StatusEffects fx;
    StatusEffectDef rage;
    rage.id = 1; rage.duration = 3.0f;
    rage.modifiers.push_back(StatModifier{StatField::Attack, /*flat*/ 20.0f, 0.0f, 0});
    ApplyEffect(s, fx, rage);
    MYE_EXPECT(s.dirty);
    ComputeDerived(s);
    MYE_EXPECT(s.derived.attack == baseAttack + 20);
    MYE_EXPECT(fx.active.size() == 1);

    // 2초 → 아직 유지.
    TickEffects(s, fx, 2.0f);
    MYE_EXPECT(fx.active.size() == 1);
    ComputeDerived(s);
    MYE_EXPECT(s.derived.attack == baseAttack + 20);

    // 추가 2초 → 만료, 수정자 제거.
    TickEffects(s, fx, 2.0f);
    MYE_EXPECT(fx.active.empty());
    MYE_EXPECT(s.modifiers.empty());
    ComputeDerived(s);
    MYE_EXPECT(s.derived.attack == baseAttack);
}

MYE_TEST(StatusDamageOverTime) {
    Stats s; s.derived.maxHp = 100; s.hp = 100;
    StatusEffects fx;
    StatusEffectDef poison;
    poison.id = 2; poison.duration = 5.0f; poison.dps = 10.0f;
    ApplyEffect(s, fx, poison);

    // 1초 → 10 데미지.
    TickEffects(s, fx, 1.0f);
    MYE_EXPECT(s.hp == 90);
    // 2.5초 → 25 데미지 누적(분수 누적).
    TickEffects(s, fx, 2.5f);
    MYE_EXPECT(s.hp == 65);
}

MYE_TEST(StatusFlagsStun) {
    Stats s; s.derived.maxHp = 100; s.hp = 100;
    StatusEffects fx;
    MYE_EXPECT(!HasFlag(fx, EffectFlag_Stun));

    StatusEffectDef stun;
    stun.id = 3; stun.duration = 1.0f; stun.flags = EffectFlag_Stun;
    ApplyEffect(s, fx, stun);
    MYE_EXPECT(HasFlag(fx, EffectFlag_Stun));
    MYE_EXPECT(!HasFlag(fx, EffectFlag_Silence));

    TickEffects(s, fx, 1.5f);   // 만료
    MYE_EXPECT(!HasFlag(fx, EffectFlag_Stun));
}
