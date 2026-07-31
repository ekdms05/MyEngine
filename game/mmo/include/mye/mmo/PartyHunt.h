// mye/mmo/PartyHunt.h — 파티 사냥 전투 시뮬레이션 (게임 레이어)
//
// [게임 레이어] 검사·마법사·버퍼로 이뤄진 파티가 몹을 사냥한다. 버퍼가 동료를 버프하면 딜이 올라
// 더 빨리 잡는다(파티 상호의존). 검사는 몹의 어그로를 받아 앞에서 버틴다(마법사·버퍼 보호).
// 결정론(시드 RNG) 순수 로직 — 서버권위·단위 테스트.
#pragma once

#include "mye/mmo/Jobs.h"
#include "mye/gameplay/Stats.h"

#include <cstdint>
#include <vector>

namespace mye::mmo {

struct HuntMember {
    JobClass        cls = JobClass::Swordsman;
    gameplay::Stats stats;
    int64_t         damageDealt = 0;   // 이번 사냥 기여 데미지(성취감 지표)
    bool            alive = true;
};

struct HuntResult {
    bool    victory = false;
    int     rounds = 0;
    int64_t totalDamage = 0;
    int32_t monsterMaxHp = 0;
    int     survivors = 0;
};

// 파티(멤버 목록)가 monster 를 사냥. useBuffer 면 버퍼가 시작 시 비버퍼 동료에게 공격 버프.
//   party 각 멤버의 damageDealt/alive 를 갱신하고 결과 반환. monster 는 복사본으로 소비.
HuntResult SimulateHunt(std::vector<HuntMember>& party, gameplay::Stats monster,
                        bool useBuffer, float buffPercent, uint64_t seed, int maxRounds = 300);

// 사냥용 몹 Stats 생성(레벨·강함 배수). 파생·HP 채움.
gameplay::Stats MakeMonster(int level, float toughness = 1.0f);

} // namespace mye::mmo
