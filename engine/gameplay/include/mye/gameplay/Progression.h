// mye/gameplay/Progression.h — 경험치·레벨업·스탯 성장 (docs/mmorpg/04, M8)
//
// XP 획득 → 레벨업(초과분 이월) → 기본 스탯 성장 → Stats.dirty 로 파생 재계산 유도. 순수 로직.
#pragma once

#include "mye/ecs/ComponentType.h"

#include <cstdint>

namespace mye::gameplay {

struct Stats;

struct Progression {
    MYE_COMPONENT(Progression);
    int64_t xp = 0;          // 현재 레벨에서 누적된 경험치
    int32_t level = 1;
    int32_t maxLevel = 99;
};

// level → level+1 에 필요한 경험치(성장 곡선, 밸런스 지점).
int64_t XpForLevel(int32_t level);

// XP 획득 → 레벨업 처리. 레벨업마다 stats.base 성장 + stats.dirty=true. 오른 레벨 수 반환.
int32_t GainXp(Progression& prog, Stats& stats, int64_t amount);

} // namespace mye::gameplay
