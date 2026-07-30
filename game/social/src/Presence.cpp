// mye/social/Presence.cpp — 프레즌스 구현 (Presence.h 참조)
#include "mye/social/Presence.h"

namespace mye::social {

void Presence::SetStatus(AccountId acc, PresenceStatus s) {
    if (s == PresenceStatus::Offline) m_status.erase(acc);
    else m_status[acc] = s;
}

PresenceStatus Presence::Get(AccountId acc) const {
    auto it = m_status.find(acc);
    return it == m_status.end() ? PresenceStatus::Offline : it->second;
}

std::vector<AccountId> Presence::OnlineFriends(AccountId acc, const FriendGraph& fg) const {
    std::vector<AccountId> out;
    for (AccountId f : fg.FriendsOf(acc))
        if (IsOnline(f)) out.push_back(f);
    return out;
}

} // namespace mye::social
