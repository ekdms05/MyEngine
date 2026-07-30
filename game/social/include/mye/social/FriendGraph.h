// mye/social/FriendGraph.h — 친구·요청·차단 그래프 (docs/mmorpg/09, M12 소셜)
//
// [게임 레이어] 친구 관계(상호)·친구 요청(대기)·차단. 차단은 친구/요청/귓속말을 막는다(상호작용 통제).
// 순수 로직 — 결정론·단위 테스트. 프레즌스 팬아웃·채팅 라우팅이 이 그래프를 참조.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::social {

using AccountId = uint64_t;

class FriendGraph {
public:
    enum class RequestResult { Sent, AlreadyFriends, AlreadyPending, Blocked, SelfRequest };

    // ---- 친구 요청 ----
    RequestResult SendRequest(AccountId from, AccountId to);
    bool AcceptRequest(AccountId to, AccountId from);   // to 가 from 의 요청 수락 → 친구
    bool RejectRequest(AccountId to, AccountId from);
    bool HasPendingRequest(AccountId from, AccountId to) const;   // from→to 대기 중?
    std::vector<AccountId> PendingFor(AccountId to) const;        // to 에게 온 요청 목록

    // ---- 친구 ----
    bool AreFriends(AccountId a, AccountId b) const;
    bool RemoveFriend(AccountId a, AccountId b);         // 상호 해제
    std::vector<AccountId> FriendsOf(AccountId acc) const;
    size_t FriendCount(AccountId acc) const;

    // ---- 차단 ----
    bool Block(AccountId who, AccountId target);         // 친구/대기 요청도 정리
    bool Unblock(AccountId who, AccountId target);
    bool IsBlocked(AccountId who, AccountId target) const;   // who 가 target 을 차단?
    bool CanInteract(AccountId a, AccountId b) const;        // 양방향 차단 없음

private:
    std::unordered_map<AccountId, std::unordered_set<AccountId>> m_friends;   // 상호
    std::unordered_map<AccountId, std::unordered_set<AccountId>> m_pending;   // to → {from...}
    std::unordered_map<AccountId, std::unordered_set<AccountId>> m_blocks;    // who → {target...}
};

} // namespace mye::social
