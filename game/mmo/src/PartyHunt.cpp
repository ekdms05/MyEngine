// mye/mmo/PartyHunt.cpp — 파티 사냥 시뮬레이션 구현 (PartyHunt.h 참조)
#include "mye/mmo/PartyHunt.h"

#include "mye/gameplay/Combat.h"
#include "mye/gameplay/StatSystem.h"

namespace mye::mmo {

gameplay::Stats MakeMonster(int level, float toughness) {
    if (level < 1) level = 1;
    if (toughness < 0.1f) toughness = 0.1f;
    gameplay::Stats m;
    m.base.level     = level;
    m.base.strength  = static_cast<int32_t>((10 + level * 2) * toughness);
    m.base.agility   = 8 + level;
    m.base.intellect = 6;
    m.base.vitality  = static_cast<int32_t>((14 + level * 3) * toughness);   // HP·방어
    m.dirty = true;
    gameplay::ComputeDerived(m, /*fillToMax=*/true);
    return m;
}

HuntResult SimulateHunt(std::vector<HuntMember>& party, gameplay::Stats monster,
                        bool useBuffer, float buffPercent, uint64_t seed, int maxRounds) {
    HuntResult res;
    if (monster.hp <= 0) monster.hp = monster.derived.maxHp;
    res.monsterMaxHp = monster.derived.maxHp;

    // 버퍼 존재 시 비버퍼 동료 버프(파티 상호의존).
    HuntMember* buffer = nullptr;
    for (HuntMember& m : party) if (GetJob(m.cls).isSupport) { buffer = &m; break; }
    if (useBuffer && buffer) {
        for (HuntMember& m : party)
            if (!GetJob(m.cls).isSupport) ApplyPartyAttackBuff(m.stats, buffPercent);
    }

    gameplay::RngState rng(seed);

    auto aliveCount = [&]() {
        int n = 0; for (const HuntMember& m : party) if (m.alive) ++n; return n;
    };
    // 몹 어그로 대상: 검사(탱커) 우선, 없으면 첫 생존자.
    auto pickTarget = [&]() -> HuntMember* {
        for (HuntMember& m : party) if (m.alive && m.cls == JobClass::Swordsman) return &m;
        for (HuntMember& m : party) if (m.alive) return &m;
        return nullptr;
    };

    while (res.rounds < maxRounds) {
        ++res.rounds;

        // 1) 파티 공격(생존자 각자 역할대로).
        for (HuntMember& m : party) {
            if (!m.alive || monster.hp <= 0) continue;
            const JobDef& job = GetJob(m.cls);
            gameplay::AttackSpec spec;
            spec.power = job.skillPower;
            spec.type  = job.attackType;
            spec.critMultiplier = 1.5f;
            const gameplay::DamageResult dr = gameplay::ComputeDamage(m.stats, monster, spec, rng);
            gameplay::ApplyDamage(monster, dr.amount);
            m.damageDealt += dr.amount;
            res.totalDamage += dr.amount;
        }
        if (monster.hp <= 0) { res.victory = true; break; }

        // 2) 몹 반격(탱커 우선).
        if (HuntMember* tgt = pickTarget()) {
            gameplay::AttackSpec spec;
            spec.power = 1.0f;
            spec.type  = gameplay::DamageType::Physical;
            const gameplay::DamageResult dr = gameplay::ComputeDamage(monster, tgt->stats, spec, rng);
            const bool killed = gameplay::ApplyDamage(tgt->stats, dr.amount);
            if (killed) tgt->alive = false;
        }
        if (aliveCount() == 0) { res.victory = false; break; }
    }

    res.survivors = aliveCount();
    if (monster.hp <= 0) res.victory = true;
    return res;
}

} // namespace mye::mmo
