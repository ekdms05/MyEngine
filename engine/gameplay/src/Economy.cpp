// mye/gameplay/Economy.cpp — 상점 매매 구현 (Economy.h 참조)
#include "mye/gameplay/Economy.h"

namespace mye::gameplay {

namespace {
int64_t Price(const ItemCatalog& cat, ItemId id, int32_t count, float mul) {
    const ItemDef* d = cat.Find(id);
    const int64_t value = d ? d->value : 0;
    const double total = static_cast<double>(value) * count * static_cast<double>(mul);
    return static_cast<int64_t>(total + 0.5);
}
} // namespace

bool BuyItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count, float priceMul) {
    if (itemId == 0 || count <= 0) return false;
    const int64_t cost = Price(cat, itemId, count, priceMul);
    if (inv.gold < cost) return false;
    if (FreeSpaceFor(inv, cat, itemId) < count) return false;
    inv.gold -= cost;
    const int32_t leftover = AddItem(inv, cat, itemId, count);   // == 0(검증됨)
    (void)leftover;
    return true;
}

bool SellItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count, float sellMul) {
    if (itemId == 0 || count <= 0) return false;
    if (CountItem(inv, itemId) < count) return false;
    const int64_t gain = Price(cat, itemId, count, sellMul);
    const int32_t removed = RemoveItem(inv, itemId, count);   // == count(검증됨)
    (void)removed;
    inv.gold += gain;
    return true;
}

} // namespace mye::gameplay
