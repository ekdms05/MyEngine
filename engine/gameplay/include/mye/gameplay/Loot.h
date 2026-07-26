// mye/gameplay/Loot.h — 결정론적 루트(드롭) 테이블 (docs/mmorpg/04, M8)
//
// 몹 처치·상자 개봉 시 드롭. 가중 무작위 선택 × rolls 회 + 골드 범위. RNG는 시드 기반(서버권위·
// 재현 가능). 순수 로직 — 네트워크/GPU 무관.
#pragma once

#include "mye/gameplay/Item.h"
#include "mye/gameplay/Combat.h"   // RngState

#include <cstdint>
#include <vector>

namespace mye::gameplay {

// 드롭 항목(가중치·개수 범위).
struct LootEntry {
    ItemId  itemId = 0;
    int32_t weight = 1;      // 가중치(상대 확률)
    int32_t minCount = 1;
    int32_t maxCount = 1;
};

// 루트 테이블. rolls 회 가중 선택 + [goldMin, goldMax] 골드.
struct LootTable {
    std::vector<LootEntry> entries;
    int32_t rolls = 1;
    int64_t goldMin = 0;
    int64_t goldMax = 0;
};

struct LootDrop {
    std::vector<ItemStack> items;   // 같은 아이템은 합산
    int64_t gold = 0;
};

// 테이블을 굴려 드롭 산출(결정론 — 같은 rng 상태 → 같은 결과).
LootDrop RollLoot(const LootTable& table, RngState& rng);

} // namespace mye::gameplay
