// mye/gameplay/Loot.cpp — 루트 테이블 굴리기 구현 (Loot.h 참조)
#include "mye/gameplay/Loot.h"

namespace mye::gameplay {

namespace {
void MergeDrop(std::vector<ItemStack>& items, ItemId id, int32_t count) {
    if (id == 0 || count <= 0) return;
    for (ItemStack& s : items)
        if (s.itemId == id) { s.count += count; return; }
    items.push_back(ItemStack{id, count});
}
} // namespace

LootDrop RollLoot(const LootTable& table, RngState& rng) {
    LootDrop drop;

    int64_t totalWeight = 0;
    for (const LootEntry& e : table.entries)
        if (e.weight > 0) totalWeight += e.weight;

    for (int32_t r = 0; r < table.rolls && totalWeight > 0; ++r) {
        int64_t pick = static_cast<int64_t>(NextU64(rng) % static_cast<uint64_t>(totalWeight));
        const LootEntry* chosen = nullptr;
        for (const LootEntry& e : table.entries) {
            if (e.weight <= 0) continue;
            pick -= e.weight;
            if (pick < 0) { chosen = &e; break; }
        }
        if (!chosen) continue;
        int32_t count = chosen->minCount;
        if (chosen->maxCount > chosen->minCount) {
            const uint64_t span = static_cast<uint64_t>(chosen->maxCount - chosen->minCount + 1);
            count += static_cast<int32_t>(NextU64(rng) % span);
        }
        MergeDrop(drop.items, chosen->itemId, count);
    }

    if (table.goldMax >= table.goldMin) {
        drop.gold = table.goldMin;
        if (table.goldMax > table.goldMin) {
            const uint64_t span = static_cast<uint64_t>(table.goldMax - table.goldMin + 1);
            drop.gold += static_cast<int64_t>(NextU64(rng) % span);
        }
    }
    return drop;
}

} // namespace mye::gameplay
