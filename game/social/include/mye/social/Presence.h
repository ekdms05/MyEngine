// mye/social/Presence.h — 접속 상태(프레즌스) + 친구 팬아웃 (docs/mmorpg/09, M12 소셜)
//
// [게임 레이어] 계정별 접속 상태. 상태 변경 시 온라인 친구에게 알림(팬아웃 대상 = 온라인 친구).
// 순수 로직. FriendGraph 를 참조해 대상만 계산.
#pragma once

#include "mye/social/FriendGraph.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::social {

enum class PresenceStatus : uint8_t { Offline, Online, Away, Busy };

class Presence {
public:
    // 상태 설정. Offline 이면 맵에서 제거(오프라인은 기본값).
    void SetStatus(AccountId acc, PresenceStatus s);

    PresenceStatus Get(AccountId acc) const;
    bool   IsOnline(AccountId acc) const { return Get(acc) != PresenceStatus::Offline; }
    size_t OnlineCount() const { return m_status.size(); }

    // acc 의 상태 변경을 알릴 대상 = 온라인인 친구들(팬아웃).
    std::vector<AccountId> OnlineFriends(AccountId acc, const FriendGraph& fg) const;

private:
    std::unordered_map<AccountId, PresenceStatus> m_status;   // 비어있으면 Offline
};

} // namespace mye::social
