// mye/gameplay/Crafting.h — 제작 레시피 (docs/mmorpg/04, M8)
//
// 레시피: 재료(ItemStack 목록) → 결과. 인벤토리에서 재료를 소모하고 결과를 생성한다. 원자적
// (재료·공간 사전 검증 → 전부 성공 또는 무변화). 순수 로직.
#pragma once

#include "mye/gameplay/Inventory.h"

#include <vector>

namespace mye::gameplay {

struct CraftRecipe {
    uint32_t               id = 0;
    std::vector<ItemStack> inputs;    // 필요한 재료
    ItemId                 outputId = 0;
    int32_t                outputCount = 1;
    int64_t                goldCost = 0;
};

// 제작 가능 여부(재료 보유 + 골드 + 결과 수납 공간).
bool CanCraft(const Inventory& inv, const ItemCatalog& cat, const CraftRecipe& recipe);

// 제작 실행: 가능하면 재료·골드 소모 + 결과 생성 후 true. 불가하면 무변화 false.
bool Craft(Inventory& inv, const ItemCatalog& cat, const CraftRecipe& recipe);

} // namespace mye::gameplay
