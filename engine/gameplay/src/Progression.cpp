// mye/gameplay/Progression.cpp — 경험치·레벨업 구현 (Progression.h 참조)
#include "mye/gameplay/Progression.h"
#include "mye/gameplay/Stats.h"

namespace mye::gameplay {

int64_t XpForLevel(int32_t level) {
    if (level < 1) level = 1;
    // 2차 곡선: 레벨이 오를수록 필요 경험치 증가. level 1→2 = 100, 2→3 = 250, 3→4 = 450 ...
    return 50LL * static_cast<int64_t>(level) * static_cast<int64_t>(level)
         + 50LL * static_cast<int64_t>(level);
}

int32_t GainXp(Progression& prog, Stats& stats, int64_t amount) {
    if (amount <= 0) return 0;
    prog.xp += amount;
    int32_t gained = 0;
    while (prog.level < prog.maxLevel) {
        const int64_t need = XpForLevel(prog.level);
        if (prog.xp < need) break;
        prog.xp -= need;
        ++prog.level;
        ++gained;
        // 기본 스탯 성장(밸런스 지점).
        stats.base.level      = prog.level;
        stats.base.strength  += 2;
        stats.base.agility   += 1;
        stats.base.intellect += 1;
        stats.base.vitality  += 2;
        stats.dirty = true;
    }
    if (prog.level >= prog.maxLevel) prog.xp = 0;   // 만렙은 경험치 누적 안 함
    return gained;
}

} // namespace mye::gameplay
