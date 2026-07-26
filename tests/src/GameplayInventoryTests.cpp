// GameplayInventoryTests.cpp — 인벤토리 스택·용량·원자적 이동 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Item.h"
#include "mye/gameplay/Inventory.h"

using namespace mye;
using namespace mye::gameplay;

namespace {
constexpr ItemId kPotion = 100;   // stackMax 10
constexpr ItemId kSword  = 200;   // stackMax 1

ItemCatalog MakeCatalog() {
    ItemCatalog c;
    c.Register(ItemDef{kPotion, "Potion", ItemType::Consumable, /*stackMax*/ 10, EquipSlot::None, {}, 5});
    c.Register(ItemDef{kSword,  "Sword",  ItemType::Equipment,  /*stackMax*/ 1,  EquipSlot::Weapon, {}, 100});
    return c;
}
} // namespace

MYE_TEST(InventoryStackAndCapacity) {
    ItemCatalog cat = MakeCatalog();
    Inventory inv; inv.capacity = 2;

    // 25 포션 → stackMax 10, 용량 2슬롯 = 최대 20. 5개 못 넣음.
    const int32_t leftover = AddItem(inv, cat, kPotion, 25);
    MYE_EXPECT(leftover == 5);
    MYE_EXPECT(CountItem(inv, kPotion) == 20);
    MYE_EXPECT(inv.slots.size() == 2);   // 10+10

    // 용량 가득 → 검(새 슬롯 필요)은 못 들어감.
    MYE_EXPECT(AddItem(inv, cat, kSword, 1) == 1);
}

MYE_TEST(InventoryRemove) {
    ItemCatalog cat = MakeCatalog();
    Inventory inv; inv.capacity = 10;
    AddItem(inv, cat, kPotion, 15);          // 10 + 5 (두 슬롯)
    MYE_EXPECT(RemoveItem(inv, kPotion, 12) == 12);
    MYE_EXPECT(CountItem(inv, kPotion) == 3);
    // 빈 스택 정리됨(15-12=3 → 한 슬롯).
    MYE_EXPECT(inv.slots.size() == 1);
    // 보유보다 많이 제거 요청 → 있는 만큼만.
    MYE_EXPECT(RemoveItem(inv, kPotion, 99) == 3);
    MYE_EXPECT(CountItem(inv, kPotion) == 0);
}

MYE_TEST(InventoryAtomicMoveNoDupe) {
    ItemCatalog cat = MakeCatalog();
    Inventory a; a.capacity = 10;
    Inventory b; b.capacity = 1;   // 좁은 인벤(1슬롯=10 포션)
    AddItem(a, cat, kPotion, 8);

    // 정상 이동: 5개 a→b.
    MYE_EXPECT(MoveItem(a, b, cat, kPotion, 5));
    MYE_EXPECT(CountItem(a, kPotion) == 3);
    MYE_EXPECT(CountItem(b, kPotion) == 5);
    // 총량 보존(복사·유실 없음).
    MYE_EXPECT(CountItem(a, kPotion) + CountItem(b, kPotion) == 8);

    // 보유 부족(a=3 < 10) → 실패(무변화).
    MYE_EXPECT(!MoveItem(a, b, cat, kPotion, 10));
    MYE_EXPECT(CountItem(a, kPotion) == 3 && CountItem(b, kPotion) == 5);

    // dst 용량 부족: a에 넉넉히 채운 뒤(=13), b 여유는 5뿐(1슬롯 10 - 5) → 6 이동 실패(무변화).
    AddItem(a, cat, kPotion, 10);   // a = 13
    MYE_EXPECT(CountItem(a, kPotion) == 13);
    MYE_EXPECT(!MoveItem(a, b, cat, kPotion, 6));
    MYE_EXPECT(CountItem(a, kPotion) == 13 && CountItem(b, kPotion) == 5);
}
