// mye/social/Matchmaking.h — 던전 매칭(역할 구성) (docs/mmorpg/04, M12 콘텐츠)
//
// [게임 레이어] 역할(탱/힐/딜)별 대기열에 등록하고, 원하는 파티 구성(예: 탱1·힐1·딜3)이 충족되면
// 그룹을 형성한다. 대기 시간이 오래된 순으로 우선(FIFO). 순수 로직 — 결정론·단위 테스트.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::social {

using AccountId = uint64_t;

enum class Role : uint8_t { Tank, Healer, Dps, Count };

struct MatchGroup {
    std::vector<AccountId> tanks;
    std::vector<AccountId> healers;
    std::vector<AccountId> dps;
    std::vector<AccountId> All() const {
        std::vector<AccountId> a;
        a.insert(a.end(), tanks.begin(), tanks.end());
        a.insert(a.end(), healers.begin(), healers.end());
        a.insert(a.end(), dps.begin(), dps.end());
        return a;
    }
    size_t Size() const { return tanks.size() + healers.size() + dps.size(); }
};

class Matchmaking {
public:
    // 파티 구성(필요 역할 수). 기본 탱1·힐1·딜3(5인).
    Matchmaking(int tanks = 1, int healers = 1, int dps = 3)
        : m_needTank(tanks), m_needHeal(healers), m_needDps(dps) {}

    void SetComposition(int tanks, int healers, int dps) { m_needTank = tanks; m_needHeal = healers; m_needDps = dps; }

    // 대기열 등록(역할). 이미 대기 중이면 무시(false).
    bool Enqueue(AccountId acc, Role role);
    // 대기 취소.
    bool Cancel(AccountId acc);
    bool InQueue(AccountId acc) const { return m_queued.count(acc) != 0; }

    size_t QueueSize() const { return m_queued.size(); }
    size_t RoleCount(Role r) const;

    // 구성 충족되는 만큼 그룹을 형성(FIFO). 매칭된 인원은 대기열에서 제거. 형성된 그룹 반환.
    std::vector<MatchGroup> FormGroups();

private:
    std::vector<AccountId>& Q(Role r);
    const std::vector<AccountId>& Q(Role r) const;

    std::vector<AccountId> m_tankQ, m_healQ, m_dpsQ;   // FIFO 대기열(역할별)
    std::unordered_map<AccountId, Role> m_queued;      // 멤버십·역할
    int m_needTank, m_needHeal, m_needDps;
};

} // namespace mye::social
