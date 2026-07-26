// mye/gameplay/Item.h — 아이템 정의·스택·카탈로그 (docs/mmorpg/04, M8)
//
// 아이템은 데이터(ItemDef). 런타임은 ItemId 로 참조하고 ItemCatalog 로 정의를 조회한다.
// 인벤토리는 ItemStack(itemId+count) 목록. 장비 아이템은 StatModifier 를 가진다.
#pragma once

#include "mye/gameplay/Stats.h"   // StatModifier

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mye::gameplay {

using ItemId = uint32_t;   // 0 = 없음/빈 슬롯

enum class ItemType : uint8_t { Misc, Equipment, Consumable, Material, Quest };

// 장비 착용 슬롯.
enum class EquipSlot : uint8_t { None, Weapon, Head, Body, Hands, Feet, Ring, Amulet, Count };

// 아이템 정의(데이터). 카탈로그가 소유.
struct ItemDef {
    ItemId      id = 0;
    std::string name;
    ItemType    type = ItemType::Misc;
    int32_t     stackMax = 1;          // 스택 상한(장비=1, 소모품=여러 개)
    EquipSlot   slot = EquipSlot::None; // Equipment 일 때 착용 슬롯
    std::vector<StatModifier> modifiers; // 장비 착용 시 Stats 에 부여
    int32_t     value = 0;             // 판매 가치(골드)
};

// 아이템 정의 레지스트리(콘텐츠 DB의 아이템 파트). id → ItemDef.
class ItemCatalog {
public:
    void Register(const ItemDef& def) { m_defs[def.id] = def; }
    const ItemDef* Find(ItemId id) const {
        auto it = m_defs.find(id);
        return it == m_defs.end() ? nullptr : &it->second;
    }
    int32_t StackMax(ItemId id) const { const ItemDef* d = Find(id); return d ? (d->stackMax > 0 ? d->stackMax : 1) : 1; }
    std::size_t Size() const { return m_defs.size(); }
private:
    std::unordered_map<ItemId, ItemDef> m_defs;
};

// 인벤토리 한 칸(스택).
struct ItemStack {
    ItemId  itemId = 0;
    int32_t count = 0;
    bool Empty() const { return itemId == 0 || count <= 0; }
};

} // namespace mye::gameplay
