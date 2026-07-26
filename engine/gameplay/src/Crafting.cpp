// mye/gameplay/Crafting.cpp — 제작 구현 (Crafting.h 참조)
#include "mye/gameplay/Crafting.h"

namespace mye::gameplay {

bool CanCraft(const Inventory& inv, const ItemCatalog& cat, const CraftRecipe& recipe) {
    if (recipe.outputId == 0 || recipe.outputCount <= 0) return false;
    if (inv.gold < recipe.goldCost) return false;
    for (const ItemStack& in : recipe.inputs)
        if (CountItem(inv, in.itemId) < in.count) return false;
    // 결과 수납 공간(재료 소모 후 여유가 더 생기지만, 보수적으로 현재 기준 검증).
    if (FreeSpaceFor(inv, cat, recipe.outputId) < recipe.outputCount) return false;
    return true;
}

bool Craft(Inventory& inv, const ItemCatalog& cat, const CraftRecipe& recipe) {
    if (!CanCraft(inv, cat, recipe)) return false;
    for (const ItemStack& in : recipe.inputs)
        RemoveItem(inv, in.itemId, in.count);
    inv.gold -= recipe.goldCost;
    const int32_t leftover = AddItem(inv, cat, recipe.outputId, recipe.outputCount);
    (void)leftover;   // CanCraft 로 공간 보장
    return true;
}

} // namespace mye::gameplay
