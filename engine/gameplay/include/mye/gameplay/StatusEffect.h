// mye/gameplay/StatusEffect.h — 버프·디버프·상태이상 (docs/mmorpg/04, M8)
//
// 시간제 효과: 지속 동안 Stats 수정자(공/방 등)를 부여하고, DoT(초당 데미지)와 상태 플래그
// (스턴·침묵·속박)를 갖는다. 만료 시 부여한 수정자를 sourceId 로 정확히 제거한다. 순수 로직.
#pragma once

#include "mye/gameplay/Stats.h"   // StatModifier
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::gameplay {

enum EffectFlag : uint32_t {
    EffectFlag_None    = 0,
    EffectFlag_Stun    = 1u << 0,
    EffectFlag_Silence = 1u << 1,
    EffectFlag_Root    = 1u << 2,
};

// 효과 정의(데이터).
struct StatusEffectDef {
    uint32_t id = 0;
    float    duration = 1.0f;
    float    dps = 0.0f;            // 초당 데미지(0=없음)
    uint32_t flags = EffectFlag_None;
    std::vector<StatModifier> modifiers;   // 지속 중 부여할 스탯 수정자
};

// 적용된 효과 인스턴스.
struct ActiveEffect {
    uint32_t instanceId = 0;   // Stats.modifiers.sourceId 와 매칭(정확 제거)
    uint32_t defId = 0;
    float    remaining = 0.0f;
    float    dps = 0.0f;
    uint32_t flags = 0;
    float    dotAccum = 0.0f;   // 누적 DoT 분수 데미지
};

struct StatusEffects {
    MYE_COMPONENT(StatusEffects);
    std::vector<ActiveEffect> active;
    uint32_t nextInstanceId = 1;
};

// 효과 적용 → 수정자 부여(sourceId=인스턴스) + Stats.dirty. 인스턴스 id 반환.
uint32_t ApplyEffect(Stats& target, StatusEffects& fx, const StatusEffectDef& def);

// 매 프레임 갱신: 지속 감소 + DoT 적용 + 만료 효과 수정자 제거(Stats.dirty). DoT로 죽으면 true.
bool TickEffects(Stats& target, StatusEffects& fx, float dtSeconds);

// 상태 플래그 질의(스턴 등).
bool HasFlag(const StatusEffects& fx, EffectFlag flag);

} // namespace mye::gameplay
