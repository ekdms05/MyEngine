// mye/gameplay/Combat.h — 전투 데미지 계산 (docs/mmorpg/04, M8)
//
// 결정론적(서버권위 전제) 데미지 공식: 공격자 파생 공격력 × 스킬 배율 → 방어 감쇠 → 크리.
// RNG는 시드 기반 xorshift(서버=클라 재현). 순수 로직 — GPU/네트워크 무관.
#pragma once

#include <cstdint>

namespace mye::gameplay {

struct Stats;

// 결정론적 난수(서버권위 전투용). 같은 시드 → 같은 시퀀스.
struct RngState {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    explicit RngState(uint64_t seed = 0x9E3779B97F4A7C15ull) : state(seed ? seed : 1) {}
};
uint64_t NextU64(RngState& rng);
float    NextFloat(RngState& rng);   // [0,1)

enum class DamageType : uint8_t { Physical, Magic, True };

// 공격(스킬) 명세.
struct AttackSpec {
    float      power = 1.0f;          // 공격력 대비 배율(스킬 계수)
    DamageType type = DamageType::Physical;
    float      critMultiplier = 1.5f;
    int32_t    flatBonus = 0;         // 절대 가산
};

struct DamageResult {
    int32_t amount = 0;
    bool    crit = false;
};

// 데미지 계산(순수 — 대상 미변경). def 방어로 감쇠(True는 무시), 크리는 공격자 critChance.
DamageResult ComputeDamage(const Stats& attacker, const Stats& defender,
                           const AttackSpec& spec, RngState& rng);

// 데미지 적용 → HP 감소. HP≤0 이면 killed=true 반환.
bool ApplyDamage(Stats& target, int32_t amount);

// 회복(HP를 최대치까지). 실제 회복량 반환.
int32_t Heal(Stats& target, int32_t amount);

} // namespace mye::gameplay
