// mye/gameplay/Economy.h — NPC 상점 매매(골드↔아이템) (docs/mmorpg/04, M8)
//
// 인벤토리의 골드와 아이템을 카탈로그 가치(ItemDef.value) 기준으로 교환한다. 원자적(골드·공간
// 사전 검증 → 전부 성공 또는 무변화). 순수 로직.
#pragma once

#include "mye/gameplay/Inventory.h"

#include <cstdint>

namespace mye::gameplay {

// 구매: 골드 지불 후 아이템 획득. 골드 부족·공간 부족 시 무변화 false.
//   priceMul: 구매 배율(1.0=정가). 총가격 = round(value * count * priceMul).
bool BuyItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count,
             float priceMul = 1.0f);

// 판매: 아이템 소모 후 골드 획득. 보유 부족 시 무변화 false.
//   sellMul: 판매 배율(보통 0.5=반값).
bool SellItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count,
              float sellMul = 0.5f);

} // namespace mye::gameplay
