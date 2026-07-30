// mye/social/GuildSystem.h — 길드(영속 그룹)·직급·권한 (docs/mmorpg/04, M12 소셜)
//
// [게임 레이어] 파티보다 크고 영속적인 그룹. 직급(Member<Officer<Leader) 위계로 초대·추방·승진·
// 위임·해산 권한을 통제한다. 한 계정 = 길드 하나. 순수 로직 — 결정론·단위 테스트.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::social {

using AccountId = uint64_t;
using GuildId   = uint64_t;

// 직급(오름차순 권한). Leader > Officer > Member.
enum class GuildRank : uint8_t { Member = 0, Officer = 1, Leader = 2 };

class GuildSystem {
public:
    enum class JoinResult { Ok, Full, AlreadyInGuild, NoGuild, NotInvited };

    // 길드 생성(생성자가 Leader). guildId 반환(0=실패: 이미 길드 소속).
    GuildId CreateGuild(AccountId leader, std::string name, int maxSize = 50);

    // 초대(Officer 이상만). 성공 시 초대 목록 추가.
    bool Invite(GuildId g, AccountId inviter, AccountId invitee);
    JoinResult AcceptInvite(AccountId invitee, GuildId g);

    // 탈퇴. Leader 탈퇴 시 최상위 직급 멤버로 승계(남으면), 비면 해산.
    void Leave(AccountId member);

    // 추방: by 직급 > target 직급이어야.
    bool Kick(GuildId g, AccountId by, AccountId target);
    // 승진/강등(Leader 만). Member↔Officer.
    bool Promote(GuildId g, AccountId by, AccountId target);
    bool Demote(GuildId g, AccountId by, AccountId target);
    // 리더 위임(Leader 만) — target 이 Leader, 기존 리더는 Officer.
    bool TransferLeadership(GuildId g, AccountId leader, AccountId target);
    // 해산(Leader 만).
    bool Disband(GuildId g, AccountId leader);

    // ---- 조회 ----
    GuildId     GuildOf(AccountId acc) const;             // 0=없음
    bool        IsMember(GuildId g, AccountId acc) const;
    GuildRank   RankOf(GuildId g, AccountId acc) const;   // 비멤버는 Member 기본(IsMember 로 구분)
    AccountId   LeaderOf(GuildId g) const;
    std::string NameOf(GuildId g) const;
    std::vector<AccountId> Members(GuildId g) const;
    size_t      Size(GuildId g) const;
    bool        IsInvited(GuildId g, AccountId acc) const;
    size_t      GuildCount() const { return m_guilds.size(); }

private:
    struct Guild {
        GuildId     id = 0;
        std::string name;
        AccountId   leader = 0;
        int         maxSize = 50;
        std::unordered_map<AccountId, GuildRank> members;
        std::unordered_set<AccountId>            invites;
    };
    Guild*       Find(GuildId id);
    const Guild* Find(GuildId id) const;

    std::unordered_map<GuildId, Guild>     m_guilds;
    std::unordered_map<AccountId, GuildId> m_memberGuild;
    GuildId m_nextId = 1;
};

} // namespace mye::social
