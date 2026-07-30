// SocialPartyTests.cpp — 파티 로스터·초대·리더 승계·추방·해산 (M12 소셜)
#include "TestFramework.h"

#include "mye/social/PartySystem.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(PartyCreateInviteAccept) {
    PartySystem ps;
    const PartyId p = ps.CreateParty(1, 3);   // 리더 1, 최대 3
    MYE_EXPECT(p != 0);
    MYE_EXPECT(ps.PartyOf(1) == p && ps.LeaderOf(p) == 1 && ps.Size(p) == 1);

    // 이미 파티 소속이면 새 파티 생성 실패.
    MYE_EXPECT(ps.CreateParty(1) == 0);

    // 초대 → 수락 → 합류.
    MYE_EXPECT(ps.Invite(p, 1, 2));
    MYE_EXPECT(ps.IsInvited(p, 2));
    MYE_EXPECT(ps.AcceptInvite(2, p) == PartySystem::JoinResult::Ok);
    MYE_EXPECT(ps.IsMember(p, 2) && ps.PartyOf(2) == p && ps.Size(p) == 2);

    // 미초대 수락 거부.
    MYE_EXPECT(ps.AcceptInvite(9, p) == PartySystem::JoinResult::NotInvited);
    // 초대자 비소속이면 초대 불가.
    MYE_EXPECT(!ps.Invite(p, 99, 5));
}

MYE_TEST(PartyFullAndAlreadyInParty) {
    PartySystem ps;
    const PartyId p = ps.CreateParty(1, 2);   // 최대 2
    ps.Invite(p, 1, 2); ps.AcceptInvite(2, p);   // 2/2
    ps.Invite(p, 1, 3);
    MYE_EXPECT(ps.AcceptInvite(3, p) == PartySystem::JoinResult::Full);

    // 다른 파티 소속인 계정 초대 불가.
    const PartyId q = ps.CreateParty(10);
    MYE_EXPECT(!ps.Invite(p, 1, 10));           // 10 은 q 소속
    MYE_EXPECT(ps.PartyOf(10) == q);
}

MYE_TEST(PartyLeaveLeaderSuccession) {
    PartySystem ps;
    const PartyId p = ps.CreateParty(1, 4);
    ps.Invite(p, 1, 2); ps.AcceptInvite(2, p);
    ps.Invite(p, 1, 3); ps.AcceptInvite(3, p);
    MYE_EXPECT(ps.Size(p) == 3 && ps.LeaderOf(p) == 1);

    // 리더 탈퇴 → 다음 멤버(2)로 승계.
    ps.Leave(1);
    MYE_EXPECT(ps.PartyOf(1) == 0 && ps.Size(p) == 2 && ps.LeaderOf(p) == 2);

    // 일반 멤버 탈퇴.
    ps.Leave(3);
    MYE_EXPECT(ps.Size(p) == 1 && ps.LeaderOf(p) == 2);

    // 마지막 멤버 탈퇴 → 해산.
    ps.Leave(2);
    MYE_EXPECT(ps.PartyCount() == 0 && ps.PartyOf(2) == 0);
}

MYE_TEST(PartyKickAndDisband) {
    PartySystem ps;
    const PartyId p = ps.CreateParty(1, 4);
    ps.Invite(p, 1, 2); ps.AcceptInvite(2, p);
    ps.Invite(p, 1, 3); ps.AcceptInvite(3, p);

    // 비리더 추방 불가.
    MYE_EXPECT(!ps.Kick(p, 2, 3));
    // 리더 추방.
    MYE_EXPECT(ps.Kick(p, 1, 3));
    MYE_EXPECT(!ps.IsMember(p, 3) && ps.PartyOf(3) == 0 && ps.Size(p) == 2);
    // 자기 자신 추방 불가.
    MYE_EXPECT(!ps.Kick(p, 1, 1));

    // 비리더 해산 불가, 리더 해산 → 전원 해제.
    MYE_EXPECT(!ps.Disband(p, 2));
    MYE_EXPECT(ps.Disband(p, 1));
    MYE_EXPECT(ps.PartyCount() == 0 && ps.PartyOf(1) == 0 && ps.PartyOf(2) == 0);
}
