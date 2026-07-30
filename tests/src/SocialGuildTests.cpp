// SocialGuildTests.cpp — 길드 직급·권한·승진/위임·승계 (M12 소셜)
#include "TestFramework.h"

#include "mye/social/GuildSystem.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(GuildCreateInviteRanks) {
    GuildSystem gs;
    const GuildId g = gs.CreateGuild(1, "드래곤", 10);
    MYE_EXPECT(g != 0);
    MYE_EXPECT(gs.NameOf(g) == "드래곤" && gs.LeaderOf(g) == 1);
    MYE_EXPECT(gs.RankOf(g, 1) == GuildRank::Leader && gs.Size(g) == 1);

    // 이미 소속 → 새 길드 실패.
    MYE_EXPECT(gs.CreateGuild(1, "x") == 0);

    // Member 는 초대 불가(Officer+ 만).
    gs.Invite(g, 1, 2); gs.AcceptInvite(2, g);   // 2 = Member
    MYE_EXPECT(gs.RankOf(g, 2) == GuildRank::Member);
    MYE_EXPECT(!gs.Invite(g, 2, 3));             // Member 2 초대 불가

    // 리더가 2 를 Officer 승진 → 초대 가능.
    MYE_EXPECT(gs.Promote(g, 1, 2));
    MYE_EXPECT(gs.RankOf(g, 2) == GuildRank::Officer);
    MYE_EXPECT(gs.Invite(g, 2, 3));
    MYE_EXPECT(gs.AcceptInvite(3, g) == GuildSystem::JoinResult::Ok);
    MYE_EXPECT(gs.Size(g) == 3);
}

MYE_TEST(GuildKickPermissions) {
    GuildSystem gs;
    const GuildId g = gs.CreateGuild(1, "G");
    gs.Invite(g, 1, 2); gs.AcceptInvite(2, g);   // Member
    gs.Invite(g, 1, 3); gs.AcceptInvite(3, g);   // Member
    gs.Promote(g, 1, 2);                          // 2 = Officer

    // Member 3 은 추방 권한 없음.
    MYE_EXPECT(!gs.Kick(g, 3, 2));
    // Officer 2 는 Member 3 추방 가능.
    MYE_EXPECT(gs.Kick(g, 2, 3));
    MYE_EXPECT(!gs.IsMember(g, 3) && gs.GuildOf(3) == 0);
    // Officer 는 Leader 추방 불가.
    MYE_EXPECT(!gs.Kick(g, 2, 1));
    // 자기 자신 추방 불가.
    MYE_EXPECT(!gs.Kick(g, 1, 1));
}

MYE_TEST(GuildPromoteDemoteTransfer) {
    GuildSystem gs;
    const GuildId g = gs.CreateGuild(1, "G");
    gs.Invite(g, 1, 2); gs.AcceptInvite(2, g);

    // 승진/강등은 리더만.
    gs.Promote(g, 1, 2);
    MYE_EXPECT(gs.RankOf(g, 2) == GuildRank::Officer);
    MYE_EXPECT(!gs.Demote(g, 2, 2));   // Officer 는 강등 권한 없음
    MYE_EXPECT(gs.Demote(g, 1, 2));    // 리더가 강등
    MYE_EXPECT(gs.RankOf(g, 2) == GuildRank::Member);

    // 리더 위임 → 직급 교체.
    gs.Promote(g, 1, 2);   // 2 Officer(위임 대상은 아무 멤버 가능)
    MYE_EXPECT(gs.TransferLeadership(g, 1, 2));
    MYE_EXPECT(gs.LeaderOf(g) == 2);
    MYE_EXPECT(gs.RankOf(g, 2) == GuildRank::Leader && gs.RankOf(g, 1) == GuildRank::Officer);
    // 이전 리더는 더 이상 위임 못 함.
    MYE_EXPECT(!gs.TransferLeadership(g, 1, 2));
}

MYE_TEST(GuildLeaveSuccessionAndDisband) {
    GuildSystem gs;
    const GuildId g = gs.CreateGuild(1, "G");
    gs.Invite(g, 1, 2); gs.AcceptInvite(2, g);   // Member
    gs.Invite(g, 1, 3); gs.AcceptInvite(3, g);   // Member
    gs.Promote(g, 1, 3);                          // 3 = Officer

    // 리더 탈퇴 → 최상위 직급(Officer 3)으로 승계.
    gs.Leave(1);
    MYE_EXPECT(gs.GuildOf(1) == 0 && gs.LeaderOf(g) == 3);
    MYE_EXPECT(gs.RankOf(g, 3) == GuildRank::Leader);

    // 해산은 리더만.
    MYE_EXPECT(!gs.Disband(g, 2));
    MYE_EXPECT(gs.Disband(g, 3));
    MYE_EXPECT(gs.GuildCount() == 0 && gs.GuildOf(2) == 0 && gs.GuildOf(3) == 0);
}
