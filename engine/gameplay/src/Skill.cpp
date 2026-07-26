// mye/gameplay/Skill.cpp — 스킬 쿨다운·시전 구현 (Skill.h 참조)
#include "mye/gameplay/Skill.h"
#include "mye/gameplay/Stats.h"

#include <algorithm>

namespace mye::gameplay {

float CooldownRemaining(const SkillState& st, SkillId id) {
    for (const auto& [sid, rem] : st.cooldowns)
        if (sid == id) return rem;
    return 0.0f;
}

bool CanCast(const SkillState& st, const Stats& caster, const SkillDef& def) {
    return CooldownRemaining(st, def.id) <= 0.0f && caster.mp >= def.mpCost;
}

bool TryCast(SkillState& st, Stats& caster, const SkillDef& def) {
    if (!CanCast(st, caster, def)) return false;
    caster.mp -= def.mpCost;
    // 쿨다운 설정(기존 엔트리 갱신 또는 추가).
    for (auto& [sid, rem] : st.cooldowns) {
        if (sid == def.id) { rem = def.cooldown; return true; }
    }
    st.cooldowns.emplace_back(def.id, def.cooldown);
    return true;
}

void TickCooldowns(SkillState& st, float dt) {
    if (dt <= 0.0f) return;
    for (auto& [sid, rem] : st.cooldowns) {
        (void)sid;
        rem -= dt;
        if (rem < 0.0f) rem = 0.0f;
    }
    st.cooldowns.erase(std::remove_if(st.cooldowns.begin(), st.cooldowns.end(),
                                      [](const std::pair<SkillId, float>& c) { return c.second <= 0.0f; }),
                       st.cooldowns.end());
}

} // namespace mye::gameplay
