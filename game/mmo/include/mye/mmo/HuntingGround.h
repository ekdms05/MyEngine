// mye/mmo/HuntingGround.h — 사냥터(존)·몹 스폰/리스폰 (게임 레이어)
//
// [게임 레이어] 파티가 가는 사냥터. 몹 종류별 최대 마릿수(maxCount)와 리스폰 시간을 정의하고,
// SpawnManager 가 초기 스폰·처치·시간 경과 리스폰을 관리한다. 순수 로직 — 결정론·단위 테스트.
#pragma once

#include "mye/mmo/Monster.h"
#include "mye/gameplay/Stats.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::mmo {

struct SpawnEntry {
    MonsterId monsterId = 0;
    int       maxCount = 1;         // 동시 최대 마릿수
    float     respawnSeconds = 5.0f;
};

struct HuntingGround {
    uint32_t                id = 0;
    std::string             name;
    int                     recommendedLevel = 1;
    std::vector<SpawnEntry> spawns;
};

// 사냥터에 살아있는 몹 인스턴스.
struct LiveMonster {
    uint64_t        instanceId = 0;
    MonsterId       monsterId = 0;
    gameplay::Stats stats;
    bool            alive = true;
};

class SpawnManager {
public:
    SpawnManager(const HuntingGround& ground, const MonsterCatalog& cat)
        : m_ground(ground), m_cat(cat) {}

    // 초기 스폰(각 종류를 maxCount 까지).
    void Populate();

    // 몹 처치 → 리스폰 예약. 성공 시 true.
    bool Kill(uint64_t instanceId);

    // 시간 경과 → 리스폰 타이머 감소, 도래 시 재스폰(maxCount 준수).
    void Tick(float dt);

    size_t AliveCount() const;
    size_t AliveCountOf(MonsterId id) const;
    size_t PendingRespawns() const { return m_respawns.size(); }
    std::vector<uint64_t> AliveInstances() const;
    const LiveMonster* Get(uint64_t instanceId) const;
    LiveMonster*       GetMutable(uint64_t instanceId);

private:
    struct Respawn { MonsterId monsterId; float timeLeft; };
    int  MaxCountOf(MonsterId id) const;
    void SpawnOne(MonsterId id);

    const HuntingGround&     m_ground;
    const MonsterCatalog&    m_cat;
    std::vector<LiveMonster> m_live;
    std::vector<Respawn>     m_respawns;
    uint64_t m_nextInstance = 1;
};

} // namespace mye::mmo
