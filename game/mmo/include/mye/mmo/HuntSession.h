// mye/mmo/HuntSession.h — 파티 사냥 세션(핵심 루프) (게임 레이어)
//
// [게임 레이어] 게임의 심장: 파티가 사냥터에서 몹을 잡아 **경험치(성장)+드롭(보상)을 함께** 얻는다.
// 매 처치마다 파티에 경험치 분배(레벨업 시 스탯 성장), 루트 누적, 리스폰. "사냥과 성장, 동료와 함께".
#pragma once

#include "mye/mmo/PartyHunt.h"        // HuntMember
#include "mye/mmo/HuntingGround.h"    // SpawnManager
#include "mye/mmo/Monster.h"          // MonsterCatalog
#include "mye/gameplay/Progression.h"
#include "mye/gameplay/Loot.h"

#include <cstdint>
#include <vector>

namespace mye::mmo {

struct HuntSessionResult {
    int                 monstersKilled = 0;
    int64_t             xpPerMember = 0;      // 멤버당 획득 경험치(전액 분배)
    gameplay::LootDrop  loot;                 // 누적 드롭(골드+아이템)
    int                 totalLevelUps = 0;    // 파티 전체 레벨업 횟수
    bool                wiped = false;        // 전멸했는가
};

// 파티가 사냥터에서 targetKills 마리를 잡을 때까지 사냥. 처치 간 파티는 회복(휴식).
//   각 멤버의 prog(경험치/레벨) 갱신 + 레벨업 시 stats 성장. 반환: 세션 요약.
//   party 와 prog 는 인덱스 정렬(같은 크기). useBuffer 면 버퍼가 매 전투 동료 버프.
HuntSessionResult RunHuntSession(std::vector<HuntMember>& party,
                                 std::vector<gameplay::Progression>& prog,
                                 SpawnManager& spawns, const MonsterCatalog& cat,
                                 int targetKills, bool useBuffer, float buffPercent, uint64_t seed);

} // namespace mye::mmo
