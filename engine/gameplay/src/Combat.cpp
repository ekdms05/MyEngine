// mye/gameplay/Combat.cpp — 전투 데미지 계산 구현 (Combat.h 참조)
#include "mye/gameplay/Combat.h"
#include "mye/gameplay/Stats.h"

#include <algorithm>
#include <cmath>

namespace mye::gameplay {

uint64_t NextU64(RngState& rng) {
    // xorshift64* — 결정론적·플랫폼 독립(정수 연산만).
    uint64_t x = rng.state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng.state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

float NextFloat(RngState& rng) {
    // 상위 24비트 → [0,1). 부동 결정론(같은 비트 → 같은 값).
    const uint32_t bits = static_cast<uint32_t>(NextU64(rng) >> 40);   // 24비트
    return static_cast<float>(bits) * (1.0f / 16777216.0f);
}

DamageResult ComputeDamage(const Stats& attacker, const Stats& defender,
                           const AttackSpec& spec, RngState& rng) {
    DamageResult r;

    float base = static_cast<float>(attacker.derived.attack) * spec.power
               + static_cast<float>(spec.flatBonus);

    // 방어 감쇠 — 100/(100+defense) 곡선(True 데미지는 무시).
    if (spec.type != DamageType::True) {
        const float def = static_cast<float>(defender.derived.defense);
        base *= 100.0f / (100.0f + std::max(0.0f, def));
    }

    // 크리(공격자 critChance). critChance≥1 항상, ≤0 없음 → 테스트 결정성.
    const float roll = NextFloat(rng);
    if (roll < attacker.derived.critChance) {
        base *= spec.critMultiplier;
        r.crit = true;
    }

    r.amount = std::max(1, static_cast<int32_t>(base + 0.5f));   // 최소 1
    return r;
}

bool ApplyDamage(Stats& target, int32_t amount) {
    if (amount < 0) amount = 0;
    target.hp -= amount;
    if (target.hp < 0) target.hp = 0;
    return target.hp <= 0;
}

int32_t Heal(Stats& target, int32_t amount) {
    if (amount < 0) amount = 0;
    const int32_t before = target.hp;
    target.hp = std::min(target.hp + amount, target.derived.maxHp);
    return target.hp - before;
}

} // namespace mye::gameplay
