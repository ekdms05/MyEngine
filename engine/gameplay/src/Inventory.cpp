// mye/gameplay/Inventory.cpp — 인벤토리 연산 구현 (Inventory.h 참조)
#include "mye/gameplay/Inventory.h"

#include <algorithm>

namespace mye::gameplay {

int32_t CountItem(const Inventory& inv, ItemId itemId) {
    int32_t total = 0;
    for (const ItemStack& s : inv.slots)
        if (s.itemId == itemId) total += s.count;
    return total;
}

int32_t FreeSpaceFor(const Inventory& inv, const ItemCatalog& cat, ItemId itemId) {
    const int32_t stackMax = cat.StackMax(itemId);
    int32_t room = 0;
    int32_t used = 0;
    for (const ItemStack& s : inv.slots) {
        ++used;
        if (s.itemId == itemId && s.count < stackMax) room += stackMax - s.count;
    }
    const int32_t emptySlots = std::max(0, inv.capacity - used);
    return room + emptySlots * stackMax;
}

int32_t AddItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count) {
    if (itemId == 0 || count <= 0) return count;
    const int32_t stackMax = cat.StackMax(itemId);
    int32_t remaining = count;

    // 1) 기존 스택 채우기.
    for (ItemStack& s : inv.slots) {
        if (remaining <= 0) break;
        if (s.itemId == itemId && s.count < stackMax) {
            const int32_t add = std::min(remaining, stackMax - s.count);
            s.count += add;
            remaining -= add;
        }
    }
    // 2) 빈 슬롯에 새 스택.
    while (remaining > 0 && static_cast<int32_t>(inv.slots.size()) < inv.capacity) {
        const int32_t add = std::min(remaining, stackMax);
        inv.slots.push_back(ItemStack{itemId, add});
        remaining -= add;
    }
    return remaining;   // 못 넣은 잔량
}

int32_t RemoveItem(Inventory& inv, ItemId itemId, int32_t count) {
    if (itemId == 0 || count <= 0) return 0;
    int32_t remaining = count;
    for (ItemStack& s : inv.slots) {
        if (remaining <= 0) break;
        if (s.itemId == itemId) {
            const int32_t take = std::min(remaining, s.count);
            s.count -= take;
            remaining -= take;
        }
    }
    // 빈 스택 정리.
    inv.slots.erase(std::remove_if(inv.slots.begin(), inv.slots.end(),
                                   [](const ItemStack& s) { return s.Empty(); }),
                    inv.slots.end());
    return count - remaining;   // 실제 제거량
}

bool MoveItem(Inventory& src, Inventory& dst, const ItemCatalog& cat, ItemId itemId, int32_t count) {
    if (itemId == 0 || count <= 0) return false;
    // 사전 검증(원자성): 보유량·수납공간 둘 다 충분해야 실행 → 부분 실패 없음.
    if (CountItem(src, itemId) < count) return false;
    if (FreeSpaceFor(dst, cat, itemId) < count) return false;
    const int32_t removed = RemoveItem(src, itemId, count);   // == count(검증됨)
    const int32_t leftover = AddItem(dst, cat, itemId, removed); // == 0(검증됨)
    (void)leftover;
    return true;
}

} // namespace mye::gameplay
