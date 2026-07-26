// mye/gameplay/StatusEffect.cpp — 버프·상태이상 구현 (StatusEffect.h 참조)
#include "mye/gameplay/StatusEffect.h"
#include "mye/gameplay/Combat.h"   // ApplyDamage

#include <algorithm>
#include <cmath>

namespace mye::gameplay {

uint32_t ApplyEffect(Stats& target, StatusEffects& fx, const StatusEffectDef& def) {
    const uint32_t inst = fx.nextInstanceId++;
    for (StatModifier m : def.modifiers) {
        m.sourceId = inst;                 // 만료 시 정확 제거용
        target.modifiers.push_back(m);
    }
    if (!def.modifiers.empty()) target.dirty = true;
    fx.active.push_back(ActiveEffect{inst, def.id, def.duration, def.dps, def.flags, 0.0f});
    return inst;
}

bool TickEffects(Stats& target, StatusEffects& fx, float dt) {
    if (dt <= 0.0f) return false;
    bool killed = false;
    std::vector<uint32_t> expired;

    for (ActiveEffect& e : fx.active) {
        // DoT: 초당 dps를 누적해 정수 데미지로 적용.
        if (e.dps > 0.0f) {
            e.dotAccum += e.dps * dt;
            const int32_t whole = static_cast<int32_t>(std::floor(e.dotAccum));
            if (whole > 0) {
                e.dotAccum -= static_cast<float>(whole);
                if (ApplyDamage(target, whole)) killed = true;
            }
        }
        e.remaining -= dt;
        if (e.remaining <= 0.0f) expired.push_back(e.instanceId);
    }

    if (!expired.empty()) {
        // 만료 효과가 부여한 수정자 제거(sourceId 매칭).
        target.modifiers.erase(
            std::remove_if(target.modifiers.begin(), target.modifiers.end(),
                           [&](const StatModifier& m) {
                               return std::find(expired.begin(), expired.end(), m.sourceId) != expired.end();
                           }),
            target.modifiers.end());
        target.dirty = true;
        // 만료 효과 제거.
        fx.active.erase(
            std::remove_if(fx.active.begin(), fx.active.end(),
                           [&](const ActiveEffect& e) {
                               return std::find(expired.begin(), expired.end(), e.instanceId) != expired.end();
                           }),
            fx.active.end());
    }
    return killed;
}

bool HasFlag(const StatusEffects& fx, EffectFlag flag) {
    for (const ActiveEffect& e : fx.active)
        if (e.flags & static_cast<uint32_t>(flag)) return true;
    return false;
}

} // namespace mye::gameplay
