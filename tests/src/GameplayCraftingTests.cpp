// GameplayCraftingTests.cpp — 제작 레시피(재료 소모·결과 생성·원자성) 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Item.h"
#include "mye/gameplay/Inventory.h"
#include "mye/gameplay/Crafting.h"

using namespace mye;
using namespace mye::gameplay;

namespace {
constexpr ItemId kOre = 10, kWood = 11, kSword = 20;
ItemCatalog MakeCatalog() {
    ItemCatalog c;
    c.Register(ItemDef{kOre,   "Ore",   ItemType::Material, 99, EquipSlot::None, {}, 2});
    c.Register(ItemDef{kWood,  "Wood",  ItemType::Material, 99, EquipSlot::None, {}, 1});
    c.Register(ItemDef{kSword, "Sword", ItemType::Equipment, 1, EquipSlot::Weapon, {}, 100});
    return c;
}
} // namespace

MYE_TEST(CraftingConsumesInputsProducesOutput) {
    ItemCatalog cat = MakeCatalog();
    CraftRecipe recipe;
    recipe.id = 1;
    recipe.inputs = {ItemStack{kOre, 3}, ItemStack{kWood, 2}};
    recipe.outputId = kSword; recipe.outputCount = 1; recipe.goldCost = 10;

    Inventory inv; inv.capacity = 10; inv.gold = 50;
    AddItem(inv, cat, kOre, 5);
    AddItem(inv, cat, kWood, 2);

    MYE_EXPECT(CanCraft(inv, cat, recipe));
    MYE_EXPECT(Craft(inv, cat, recipe));
    MYE_EXPECT(CountItem(inv, kOre) == 2);    // 5-3
    MYE_EXPECT(CountItem(inv, kWood) == 0);   // 2-2
    MYE_EXPECT(CountItem(inv, kSword) == 1);
    MYE_EXPECT(inv.gold == 40);               // 50-10
}

MYE_TEST(CraftingRejectsInsufficientAndAtomic) {
    ItemCatalog cat = MakeCatalog();
    CraftRecipe recipe;
    recipe.id = 1;
    recipe.inputs = {ItemStack{kOre, 3}, ItemStack{kWood, 2}};
    recipe.outputId = kSword; recipe.outputCount = 1; recipe.goldCost = 10;

    Inventory inv; inv.capacity = 10; inv.gold = 50;
    AddItem(inv, cat, kOre, 3);   // wood 없음 → 재료 부족

    MYE_EXPECT(!CanCraft(inv, cat, recipe));
    MYE_EXPECT(!Craft(inv, cat, recipe));
    // 무변화(원자성): 재료·골드 그대로.
    MYE_EXPECT(CountItem(inv, kOre) == 3);
    MYE_EXPECT(inv.gold == 50);
    MYE_EXPECT(CountItem(inv, kSword) == 0);

    // 골드 부족 케이스.
    AddItem(inv, cat, kWood, 2);
    inv.gold = 5;   // goldCost 10 > 5
    MYE_EXPECT(!Craft(inv, cat, recipe));
    MYE_EXPECT(CountItem(inv, kOre) == 3 && CountItem(inv, kWood) == 2);
}
