// mye/social/Matchmaking.cpp — 던전 매칭 구현 (Matchmaking.h 참조)
#include "mye/social/Matchmaking.h"

#include <algorithm>

namespace mye::social {

std::vector<AccountId>& Matchmaking::Q(Role r) {
    switch (r) { case Role::Tank: return m_tankQ; case Role::Healer: return m_healQ; default: return m_dpsQ; }
}
const std::vector<AccountId>& Matchmaking::Q(Role r) const {
    switch (r) { case Role::Tank: return m_tankQ; case Role::Healer: return m_healQ; default: return m_dpsQ; }
}

bool Matchmaking::Enqueue(AccountId acc, Role role) {
    if (acc == 0 || role == Role::Count || m_queued.count(acc)) return false;
    Q(role).push_back(acc);
    m_queued[acc] = role;
    return true;
}

bool Matchmaking::Cancel(AccountId acc) {
    auto it = m_queued.find(acc);
    if (it == m_queued.end()) return false;
    auto& q = Q(it->second);
    q.erase(std::remove(q.begin(), q.end(), acc), q.end());
    m_queued.erase(it);
    return true;
}

size_t Matchmaking::RoleCount(Role r) const { return r == Role::Count ? 0 : Q(r).size(); }

std::vector<MatchGroup> Matchmaking::FormGroups() {
    std::vector<MatchGroup> groups;
    while (static_cast<int>(m_tankQ.size()) >= m_needTank &&
           static_cast<int>(m_healQ.size()) >= m_needHeal &&
           static_cast<int>(m_dpsQ.size())  >= m_needDps) {
        MatchGroup g;
        auto pull = [&](std::vector<AccountId>& q, int n, std::vector<AccountId>& out) {
            for (int i = 0; i < n; ++i) {
                out.push_back(q.front());
                m_queued.erase(q.front());
                q.erase(q.begin());   // FIFO(앞에서)
            }
        };
        pull(m_tankQ, m_needTank, g.tanks);
        pull(m_healQ, m_needHeal, g.healers);
        pull(m_dpsQ,  m_needDps,  g.dps);
        groups.push_back(std::move(g));
    }
    return groups;
}

} // namespace mye::social
