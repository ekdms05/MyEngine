// mye/mmo/HuntingGround.cpp — 사냥터 스폰/리스폰 구현 (HuntingGround.h 참조)
#include "mye/mmo/HuntingGround.h"

#include <algorithm>

namespace mye::mmo {

int SpawnManager::MaxCountOf(MonsterId id) const {
    for (const SpawnEntry& s : m_ground.spawns) if (s.monsterId == id) return s.maxCount;
    return 0;
}

void SpawnManager::SpawnOne(MonsterId id) {
    LiveMonster m;
    m.instanceId = m_nextInstance++;
    m.monsterId = id;
    m.stats = m_cat.MakeStats(id);
    m.alive = true;
    m_live.push_back(std::move(m));
}

void SpawnManager::Populate() {
    for (const SpawnEntry& s : m_ground.spawns) {
        const int have = static_cast<int>(AliveCountOf(s.monsterId));
        for (int i = have; i < s.maxCount; ++i) SpawnOne(s.monsterId);
    }
}

bool SpawnManager::Kill(uint64_t instanceId) {
    for (auto it = m_live.begin(); it != m_live.end(); ++it) {
        if (it->instanceId == instanceId) {
            const MonsterId mid = it->monsterId;
            m_live.erase(it);
            // 리스폰 예약(사냥터에 정의된 종류만).
            for (const SpawnEntry& s : m_ground.spawns)
                if (s.monsterId == mid) { m_respawns.push_back(Respawn{ mid, s.respawnSeconds }); break; }
            return true;
        }
    }
    return false;
}

void SpawnManager::Tick(float dt) {
    if (dt < 0.0f) return;
    for (auto it = m_respawns.begin(); it != m_respawns.end();) {
        it->timeLeft -= dt;
        if (it->timeLeft <= 0.0f) {
            // maxCount 여유 있으면 재스폰.
            if (static_cast<int>(AliveCountOf(it->monsterId)) < MaxCountOf(it->monsterId))
                SpawnOne(it->monsterId);
            it = m_respawns.erase(it);
        } else {
            ++it;
        }
    }
}

size_t SpawnManager::AliveCount() const { return m_live.size(); }

size_t SpawnManager::AliveCountOf(MonsterId id) const {
    return static_cast<size_t>(std::count_if(m_live.begin(), m_live.end(),
        [id](const LiveMonster& m) { return m.monsterId == id; }));
}

std::vector<uint64_t> SpawnManager::AliveInstances() const {
    std::vector<uint64_t> out;
    out.reserve(m_live.size());
    for (const LiveMonster& m : m_live) out.push_back(m.instanceId);
    return out;
}

const LiveMonster* SpawnManager::Get(uint64_t instanceId) const {
    for (const LiveMonster& m : m_live) if (m.instanceId == instanceId) return &m;
    return nullptr;
}
LiveMonster* SpawnManager::GetMutable(uint64_t instanceId) {
    for (LiveMonster& m : m_live) if (m.instanceId == instanceId) return &m;
    return nullptr;
}

} // namespace mye::mmo
