// mye/social/FriendGraph.cpp — 친구·요청·차단 구현 (FriendGraph.h 참조)
#include "mye/social/FriendGraph.h"

namespace mye::social {

namespace {
bool SetHas(const std::unordered_map<AccountId, std::unordered_set<AccountId>>& m,
            AccountId key, AccountId val) {
    auto it = m.find(key);
    return it != m.end() && it->second.count(val) != 0;
}
}

FriendGraph::RequestResult FriendGraph::SendRequest(AccountId from, AccountId to) {
    if (from == to) return RequestResult::SelfRequest;
    if (AreFriends(from, to)) return RequestResult::AlreadyFriends;
    // 어느 쪽이든 차단이면 요청 불가.
    if (IsBlocked(to, from) || IsBlocked(from, to)) return RequestResult::Blocked;
    if (SetHas(m_pending, to, from)) return RequestResult::AlreadyPending;
    m_pending[to].insert(from);
    return RequestResult::Sent;
}

bool FriendGraph::AcceptRequest(AccountId to, AccountId from) {
    if (!SetHas(m_pending, to, from)) return false;
    m_pending[to].erase(from);
    m_friends[to].insert(from);
    m_friends[from].insert(to);
    return true;
}

bool FriendGraph::RejectRequest(AccountId to, AccountId from) {
    if (!SetHas(m_pending, to, from)) return false;
    m_pending[to].erase(from);
    return true;
}

bool FriendGraph::HasPendingRequest(AccountId from, AccountId to) const {
    return SetHas(m_pending, to, from);
}

std::vector<AccountId> FriendGraph::PendingFor(AccountId to) const {
    std::vector<AccountId> out;
    auto it = m_pending.find(to);
    if (it != m_pending.end()) for (AccountId f : it->second) out.push_back(f);
    return out;
}

bool FriendGraph::AreFriends(AccountId a, AccountId b) const {
    return SetHas(m_friends, a, b);
}

bool FriendGraph::RemoveFriend(AccountId a, AccountId b) {
    bool removed = false;
    if (auto it = m_friends.find(a); it != m_friends.end()) removed |= it->second.erase(b) > 0;
    if (auto it = m_friends.find(b); it != m_friends.end()) it->second.erase(a);
    return removed;
}

std::vector<AccountId> FriendGraph::FriendsOf(AccountId acc) const {
    std::vector<AccountId> out;
    auto it = m_friends.find(acc);
    if (it != m_friends.end()) for (AccountId f : it->second) out.push_back(f);
    return out;
}

size_t FriendGraph::FriendCount(AccountId acc) const {
    auto it = m_friends.find(acc);
    return it == m_friends.end() ? 0 : it->second.size();
}

bool FriendGraph::Block(AccountId who, AccountId target) {
    if (who == target) return false;
    m_blocks[who].insert(target);
    // 친구 관계·양방향 대기 요청 정리.
    RemoveFriend(who, target);
    if (auto it = m_pending.find(who); it != m_pending.end()) it->second.erase(target);
    if (auto it = m_pending.find(target); it != m_pending.end()) it->second.erase(who);
    return true;
}

bool FriendGraph::Unblock(AccountId who, AccountId target) {
    auto it = m_blocks.find(who);
    return it != m_blocks.end() && it->second.erase(target) > 0;
}

bool FriendGraph::IsBlocked(AccountId who, AccountId target) const {
    return SetHas(m_blocks, who, target);
}

bool FriendGraph::CanInteract(AccountId a, AccountId b) const {
    return !IsBlocked(a, b) && !IsBlocked(b, a);
}

} // namespace mye::social
