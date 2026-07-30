// mye/social/LoginQueue.cpp — 로그인 대기열 구현 (LoginQueue.h 참조)
#include "mye/social/LoginQueue.h"

#include <algorithm>

namespace mye::social {

LoginQueue::Result LoginQueue::Request(AccountId acc, int priority) {
    if (m_active.count(acc)) return Result::AlreadyActive;
    if (m_queued.count(acc)) return Result::AlreadyQueued;

    if (static_cast<int>(m_active.size()) < m_capacity) {   // capacity 0 = 슬롯 없음(전원 대기)
        m_active.insert(acc);
        return Result::Admitted;
    }
    m_queue.push_back(Entry{ acc, m_ticket++, priority });
    m_queued.insert(acc);
    return Result::Queued;
}

int LoginQueue::FrontIndex() const {
    if (m_queue.empty()) return -1;
    int best = 0;
    for (int i = 1; i < static_cast<int>(m_queue.size()); ++i) {
        const Entry& e = m_queue[i];
        const Entry& b = m_queue[best];
        // 우선순위 높은 게 앞, 같으면 ticket 작은 게 앞(FIFO).
        if (e.priority > b.priority || (e.priority == b.priority && e.ticket < b.ticket))
            best = i;
    }
    return best;
}

int LoginQueue::Position(AccountId acc) const {
    if (!m_queued.count(acc)) return 0;
    // 정렬된 순서에서의 1-based 위치.
    std::vector<const Entry*> sorted;
    sorted.reserve(m_queue.size());
    for (const Entry& e : m_queue) sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(), [](const Entry* a, const Entry* b) {
        return a->priority > b->priority || (a->priority == b->priority && a->ticket < b->ticket);
    });
    for (size_t i = 0; i < sorted.size(); ++i)
        if (sorted[i]->acc == acc) return static_cast<int>(i) + 1;
    return 0;
}

void LoginQueue::Release(AccountId acc) {
    m_active.erase(acc);
}

void LoginQueue::Cancel(AccountId acc) {
    if (!m_queued.count(acc)) return;
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
        if (it->acc == acc) { m_queue.erase(it); break; }
    }
    m_queued.erase(acc);
}

std::vector<AccountId> LoginQueue::Admit() {
    std::vector<AccountId> admitted;
    while (static_cast<int>(m_active.size()) < m_capacity && !m_queue.empty()) {
        const int fi = FrontIndex();
        if (fi < 0) break;
        const AccountId acc = m_queue[static_cast<size_t>(fi)].acc;
        m_queue.erase(m_queue.begin() + fi);
        m_queued.erase(acc);
        m_active.insert(acc);
        admitted.push_back(acc);
    }
    return admitted;
}

} // namespace mye::social
