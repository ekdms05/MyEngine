// SocialGraphTests.cpp — 친구/차단(FriendGraph) + 프레즌스(Presence) (M12 소셜)
#include "TestFramework.h"

#include "mye/social/FriendGraph.h"
#include "mye/social/Presence.h"

#include <algorithm>

using namespace mye;
using namespace mye::social;

namespace { bool Has(const std::vector<AccountId>& v, AccountId x) { return std::find(v.begin(), v.end(), x) != v.end(); } }

MYE_TEST(FriendRequestAcceptReject) {
    FriendGraph g;

    // 요청 → 대기.
    MYE_EXPECT(g.SendRequest(1, 2) == FriendGraph::RequestResult::Sent);
    MYE_EXPECT(g.HasPendingRequest(1, 2));
    MYE_EXPECT(g.PendingFor(2).size() == 1);
    MYE_EXPECT(!g.AreFriends(1, 2));

    // 자기 자신·중복.
    MYE_EXPECT(g.SendRequest(1, 1) == FriendGraph::RequestResult::SelfRequest);
    MYE_EXPECT(g.SendRequest(1, 2) == FriendGraph::RequestResult::AlreadyPending);

    // 수락 → 상호 친구.
    MYE_EXPECT(g.AcceptRequest(2, 1));
    MYE_EXPECT(g.AreFriends(1, 2) && g.AreFriends(2, 1));
    MYE_EXPECT(!g.HasPendingRequest(1, 2));
    MYE_EXPECT(g.FriendCount(1) == 1 && g.FriendCount(2) == 1);
    MYE_EXPECT(g.SendRequest(1, 2) == FriendGraph::RequestResult::AlreadyFriends);

    // 거절.
    MYE_EXPECT(g.SendRequest(3, 2) == FriendGraph::RequestResult::Sent);
    MYE_EXPECT(g.RejectRequest(2, 3));
    MYE_EXPECT(!g.HasPendingRequest(3, 2) && !g.AreFriends(2, 3));
    MYE_EXPECT(!g.RejectRequest(2, 3));   // 이미 없음

    // 친구 해제(상호).
    MYE_EXPECT(g.RemoveFriend(1, 2));
    MYE_EXPECT(!g.AreFriends(1, 2) && !g.AreFriends(2, 1));
}

MYE_TEST(FriendBlockControlsInteraction) {
    FriendGraph g;
    // 친구가 된 뒤 차단 → 친구 관계·상호작용 해제.
    g.SendRequest(1, 2); g.AcceptRequest(2, 1);
    MYE_EXPECT(g.AreFriends(1, 2));

    MYE_EXPECT(g.Block(2, 1));   // 2 가 1 을 차단
    MYE_EXPECT(g.IsBlocked(2, 1));
    MYE_EXPECT(!g.AreFriends(1, 2));            // 친구 자동 해제
    MYE_EXPECT(!g.CanInteract(1, 2));           // 상호작용 불가(귓속말 등)

    // 차단 중엔 친구 요청 불가.
    MYE_EXPECT(g.SendRequest(1, 2) == FriendGraph::RequestResult::Blocked);
    MYE_EXPECT(g.SendRequest(2, 1) == FriendGraph::RequestResult::Blocked);

    // 차단 중 들어온 요청은 정리됨(대기 없음).
    MYE_EXPECT(g.PendingFor(1).empty() && g.PendingFor(2).empty());

    // 해제 → 다시 상호작용·요청 가능.
    MYE_EXPECT(g.Unblock(2, 1));
    MYE_EXPECT(g.CanInteract(1, 2));
    MYE_EXPECT(g.SendRequest(1, 2) == FriendGraph::RequestResult::Sent);
}

MYE_TEST(PresenceOnlineFanout) {
    FriendGraph g;
    // 1 의 친구: 2,3,4.
    for (AccountId f : {2u, 3u, 4u}) { g.SendRequest(1, f); g.AcceptRequest(f, 1); }
    MYE_EXPECT(g.FriendCount(1) == 3);

    Presence p;
    p.SetStatus(1, PresenceStatus::Online);
    p.SetStatus(2, PresenceStatus::Online);
    p.SetStatus(3, PresenceStatus::Away);      // away 도 온라인 취급
    // 4 는 오프라인(미설정).
    MYE_EXPECT(p.IsOnline(1) && p.IsOnline(3) && !p.IsOnline(4));
    MYE_EXPECT(p.OnlineCount() == 3);

    // 1 의 상태 변경 팬아웃 = 온라인 친구(2,3), 오프라인 4 제외.
    auto fanout = p.OnlineFriends(1, g);
    MYE_EXPECT(fanout.size() == 2 && Has(fanout, 2) && Has(fanout, 3) && !Has(fanout, 4));

    // 오프라인 전환 → 맵에서 제거.
    p.SetStatus(2, PresenceStatus::Offline);
    MYE_EXPECT(!p.IsOnline(2) && p.OnlineCount() == 2);
    auto fanout2 = p.OnlineFriends(1, g);
    MYE_EXPECT(fanout2.size() == 1 && Has(fanout2, 3));
}
