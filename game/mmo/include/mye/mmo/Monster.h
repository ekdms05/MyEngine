// mye/mmo/Monster.h — 몹 정의·카탈로그·드롭 (게임 레이어)
//
// [게임 레이어] 사냥할 몹의 종류. 각 몹은 레벨·강함(HP/공격)·경험치 보상·루트 테이블을 갖는다.
// engine/gameplay(Stats·Loot·RngState)를 재사용. 순수 로직 — 결정론·단위 테스트.
#pragma once

#include "mye/mmo/PartyHunt.h"       // MakeMonster
#include "mye/gameplay/Loot.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mye::mmo {

using MonsterId = uint32_t;

struct MonsterDef {
    MonsterId          id = 0;
    std::string        name;
    int                level = 1;
    float              toughness = 1.0f;   // HP/공격 배수(MakeMonster)
    int64_t            xpReward = 0;        // 처치 시 파티 분배 경험치
    gameplay::LootTable loot;               // 드롭(가중·골드)
};

class MonsterCatalog {
public:
    void Register(const MonsterDef& def) { m_defs[def.id] = def; }
    const MonsterDef* Find(MonsterId id) const {
        auto it = m_defs.find(id);
        return it == m_defs.end() ? nullptr : &it->second;
    }
    size_t Count() const { return m_defs.size(); }

    // 몹 전투 Stats 생성(레벨·강함). 미등록이면 기본 몹.
    gameplay::Stats MakeStats(MonsterId id) const {
        const MonsterDef* d = Find(id);
        return d ? MakeMonster(d->level, d->toughness) : MakeMonster(1, 1.0f);
    }
    // 드롭 굴림(결정론). 미등록이면 빈 드롭.
    gameplay::LootDrop RollDrops(MonsterId id, gameplay::RngState& rng) const {
        const MonsterDef* d = Find(id);
        return d ? gameplay::RollLoot(d->loot, rng) : gameplay::LootDrop{};
    }
    int64_t XpReward(MonsterId id) const { const MonsterDef* d = Find(id); return d ? d->xpReward : 0; }
    std::string_view Name(MonsterId id) const { const MonsterDef* d = Find(id); return d ? std::string_view(d->name) : std::string_view{}; }

private:
    std::unordered_map<MonsterId, MonsterDef> m_defs;
};

// 초보 사냥터용 기본 몹 3종을 카탈로그에 등록(슬라임·고블린·오크). 콘텐츠 시드.
void RegisterStarterMonsters(MonsterCatalog& cat);

} // namespace mye::mmo
