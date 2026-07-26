// mye/gameplay/Skill.h — 스킬 정의·쿨다운·시전 (docs/mmorpg/04, M8)
//
// 스킬은 데이터(SkillDef: 쿨다운·MP비용·계수). 엔티티는 SkillState 로 스킬별 남은 쿨다운을 갖는다.
// 시전은 쿨다운 0 + MP 충분 시에만 성공(MP 차감 + 쿨다운 시작). 순수 로직.
#pragma once

#include "mye/gameplay/Combat.h"   // DamageType
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace mye::gameplay {

struct Stats;
using SkillId = uint32_t;

struct SkillDef {
    SkillId    id = 0;
    float      cooldown = 1.0f;   // 초
    int32_t    mpCost = 0;
    float      power = 1.0f;      // AttackSpec.power 로 사용
    DamageType type = DamageType::Physical;
};

// 엔티티별 스킬 상태(스킬 id → 남은 쿨다운 초).
struct SkillState {
    MYE_COMPONENT(SkillState);
    std::vector<std::pair<SkillId, float>> cooldowns;
};

float CooldownRemaining(const SkillState& st, SkillId id);
bool  CanCast(const SkillState& st, const Stats& caster, const SkillDef& def);

// 시전 시도: 가능하면 MP 차감 + 쿨다운 시작하고 true. 불가하면 무변화 false.
bool TryCast(SkillState& st, Stats& caster, const SkillDef& def);

// 매 프레임 쿨다운 감소(0 이하 제거).
void TickCooldowns(SkillState& st, float dtSeconds);

} // namespace mye::gameplay
