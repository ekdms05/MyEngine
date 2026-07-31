// mye/mmo/HuntSession.cpp — 파티 사냥 세션 구현 (HuntSession.h 참조)
#include "mye/mmo/HuntSession.h"

#include "mye/mmo/Jobs.h"
#include "mye/gameplay/StatSystem.h"

namespace mye::mmo {

namespace {
// 드롭을 누적(같은 아이템 합산).
void MergeLoot(gameplay::LootDrop& into, const gameplay::LootDrop& add) {
    into.gold += add.gold;
    for (const gameplay::ItemStack& s : add.items) {
        bool merged = false;
        for (gameplay::ItemStack& t : into.items)
            if (t.itemId == s.itemId) { t.count += s.count; merged = true; break; }
        if (!merged) into.items.push_back(s);
    }
}
} // namespace

HuntSessionResult RunHuntSession(std::vector<HuntMember>& party,
                                 std::vector<gameplay::Progression>& prog,
                                 SpawnManager& spawns, const MonsterCatalog& cat,
                                 int targetKills, bool useBuffer, float buffPercent, uint64_t seed) {
    HuntSessionResult res;
    if (party.size() != prog.size()) return res;
    gameplay::RngState rng(seed);

    int guard = 0;
    while (res.monstersKilled < targetKills && guard++ < targetKills * 20) {
        // 타깃 확보(없으면 리스폰 대기).
        if (spawns.AliveCount() == 0) spawns.Tick(1000.0f);
        auto insts = spawns.AliveInstances();
        if (insts.empty()) break;
        const uint64_t inst = insts[0];
        const LiveMonster* lm = spawns.Get(inst);
        if (!lm) break;
        const MonsterId mid = lm->monsterId;
        gameplay::Stats monster = lm->stats;   // 전투용 복사

        // 처치 간 회복·부활(안전지대 휴식).
        for (HuntMember& m : party) {
            m.alive = true;
            m.stats.hp = m.stats.derived.maxHp;
            m.stats.mp = m.stats.derived.maxMp;
        }

        HuntResult hr = SimulateHunt(party, monster, useBuffer, buffPercent,
                                     seed + static_cast<uint64_t>(res.monstersKilled) * 0x9E3779B97F4A7C15ull);
        if (!hr.victory) { res.wiped = true; break; }

        spawns.Kill(inst);
        ++res.monstersKilled;

        // 경험치 전액 분배(레벨업 시 스탯 성장) + 드롭 누적.
        const int64_t xp = cat.XpReward(mid);
        res.xpPerMember += xp;
        for (size_t i = 0; i < party.size(); ++i) {
            const int32_t ups = gameplay::GainXp(prog[i], party[i].stats, xp);
            if (ups > 0) {
                party[i].stats.base.level = prog[i].level;   // 레벨 미러
                gameplay::ComputeDerived(party[i].stats, /*fillToMax=*/true);
                res.totalLevelUps += ups;
            }
        }
        MergeLoot(res.loot, cat.RollDrops(mid, rng));

        spawns.Tick(1000.0f);   // 다음 타깃 리스폰
    }
    return res;
}

} // namespace mye::mmo
