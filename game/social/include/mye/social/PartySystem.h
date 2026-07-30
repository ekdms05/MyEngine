// mye/social/PartySystem.h — 파티(임시 그룹) 로스터·초대·리더 (docs/mmorpg/04, M12 소셜)
//
// [게임 레이어] 파티 생성·초대·수락·탈퇴·추방·해산. 리더 위임(리더 탈퇴 시 승계). 한 계정은 파티 하나.
// 순수 로직 — 결정론·단위 테스트. 채팅(Party 채널)·던전 매칭이 참조.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::social {

using AccountId = uint64_t;
using PartyId   = uint64_t;

class PartySystem {
public:
    enum class JoinResult { Ok, Full, AlreadyInParty, NoParty, NotInvited };

    // 파티 생성(생성자가 리더·첫 멤버). partyId 반환(0=실패: 이미 파티 소속).
    PartyId CreateParty(AccountId leader, int maxSize = 6);

    // 초대(초대자는 파티 소속이어야). 성공 시 초대 목록에 추가.
    bool Invite(PartyId party, AccountId inviter, AccountId invitee);

    // 초대 수락 → 멤버 합류. 미초대/만석/이미 파티소속이면 실패 사유 반환.
    JoinResult AcceptInvite(AccountId invitee, PartyId party);

    // 탈퇴. 리더가 나가면 다음 멤버로 승계(남으면), 비면 해산.
    void Leave(AccountId member);

    // 추방(리더만). 성공 시 true.
    bool Kick(PartyId party, AccountId byLeader, AccountId target);

    // 해산(리더만) — 전 멤버 파티 해제.
    bool Disband(PartyId party, AccountId byLeader);

    // ---- 조회 ----
    PartyId   PartyOf(AccountId acc) const;         // 0=없음
    AccountId LeaderOf(PartyId party) const;        // 0=없음
    std::vector<AccountId> Members(PartyId party) const;
    size_t    Size(PartyId party) const;
    bool      IsMember(PartyId party, AccountId acc) const;
    bool      IsInvited(PartyId party, AccountId acc) const;
    size_t    PartyCount() const { return m_parties.size(); }

private:
    struct Party {
        PartyId   id = 0;
        AccountId leader = 0;
        int       maxSize = 6;
        std::vector<AccountId>        members;   // 순서 유지(승계는 앞에서)
        std::unordered_set<AccountId> invites;
    };
    Party*       Find(PartyId id);
    const Party* Find(PartyId id) const;

    std::unordered_map<PartyId, Party>     m_parties;
    std::unordered_map<AccountId, PartyId> m_memberParty;   // 계정 → 소속 파티
    PartyId m_nextId = 1;
};

} // namespace mye::social
