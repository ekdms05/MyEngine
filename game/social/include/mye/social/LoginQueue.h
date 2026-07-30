// mye/social/LoginQueue.h — 로그인 대기열(순번·용량·우선순위·admit) (docs/mmorpg/09, M12 소셜)
//
// [게임 레이어 — 엔진 아님] 서버 동접 상한(capacity) 초과 시 접속을 대기열에 넣고, 슬롯이 나면
// 우선순위(높을수록 앞)→FIFO 순으로 admit 한다. 순수 로직 — 결정론·단위 테스트.
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::social {

using AccountId = uint64_t;

class LoginQueue {
public:
    explicit LoginQueue(int capacity = 1000) : m_capacity(capacity < 0 ? 0 : capacity) {}

    enum class Result { Admitted, Queued, AlreadyActive, AlreadyQueued };

    void SetCapacity(int c) { m_capacity = c < 0 ? 0 : c; }
    int  Capacity() const { return m_capacity; }
    int  ActiveCount() const { return static_cast<int>(m_active.size()); }
    size_t QueueLength() const { return m_queue.size(); }

    // 접속 요청. 활성 여유 있으면 즉시 Admitted, 아니면 Queued(대기열 추가). 중복은 상태 반환.
    Result Request(AccountId acc, int priority = 0);

    // 대기열 순번(1-based, 우선순위→FIFO 정렬 기준). 대기 중 아니면 0.
    int Position(AccountId acc) const;

    // 활성 로그아웃(슬롯 반납). 이후 Admit 으로 대기열에서 채운다.
    void Release(AccountId acc);
    // 대기열 이탈(취소).
    void Cancel(AccountId acc);

    // 여유 슬롯을 대기열 앞(우선순위→FIFO)에서 채운다. admit 된 계정 목록 반환.
    std::vector<AccountId> Admit();

    bool IsActive(AccountId acc) const { return m_active.count(acc) != 0; }
    bool IsQueued(AccountId acc) const { return m_queued.count(acc) != 0; }

private:
    struct Entry { AccountId acc; uint64_t ticket; int priority; };
    // 우선순위 desc, ticket asc(FIFO) 로 가장 앞 항목 인덱스. 큐가 비면 -1.
    int FrontIndex() const;

    int m_capacity;
    std::unordered_set<AccountId> m_active;
    std::vector<Entry>            m_queue;
    std::unordered_set<AccountId> m_queued;   // 큐 멤버십 빠른 조회
    uint64_t m_ticket = 1;
};

} // namespace mye::social
