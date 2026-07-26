// mye/gameplay/Inventory.h — 인벤토리 컴포넌트 + 원자적 아이템 연산 (docs/mmorpg/04, M8)
//
// 인벤토리는 ItemStack 슬롯 목록(capacity 상한)과 골드. 연산은 스택 규칙(stackMax)을 지키고,
// MoveItem 은 원자적(전부 성공 또는 전부 롤백) — 아이템 복사/유실 방지(dupe-safety) 전제.
#pragma once

#include "mye/gameplay/Item.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::gameplay {

// 인벤토리 컴포넌트. slots 는 비어있지 않은 스택만 보관, 최대 capacity 개.
struct Inventory {
    MYE_COMPONENT(Inventory);
    std::vector<ItemStack> slots;
    int32_t capacity = 20;
    int64_t gold = 0;
};

// itemId 개수 합.
int32_t CountItem(const Inventory& inv, ItemId itemId);

// 아이템 추가(스택 규칙·용량). 넣지 못한 잔량 반환(0=전부 수납).
int32_t AddItem(Inventory& inv, const ItemCatalog& cat, ItemId itemId, int32_t count);

// 아이템 제거. 실제 제거한 수 반환(보유량보다 적으면 그만큼만).
int32_t RemoveItem(Inventory& inv, ItemId itemId, int32_t count);

// 원자적 이동: src → dst 로 count 개. 실패 시(부족·용량초과) 아무 변화 없이 false.
//   플레이어 거래·창고 이동의 dupe-safe 경로. cat 은 dst 스택 규칙 판정용.
bool MoveItem(Inventory& src, Inventory& dst, const ItemCatalog& cat, ItemId itemId, int32_t count);

// 남은 수납 가능량(스택 여유 + 빈 슬롯 × stackMax).
int32_t FreeSpaceFor(const Inventory& inv, const ItemCatalog& cat, ItemId itemId);

} // namespace mye::gameplay
