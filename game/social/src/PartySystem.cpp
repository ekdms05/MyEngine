// mye/social/PartySystem.cpp — 파티 시스템 구현 (PartySystem.h 참조)
#include "mye/social/PartySystem.h"

#include <algorithm>

namespace mye::social {

PartySystem::Party*       PartySystem::Find(PartyId id) {
    auto it = m_parties.find(id); return it == m_parties.end() ? nullptr : &it->second;
}
const PartySystem::Party* PartySystem::Find(PartyId id) const {
    auto it = m_parties.find(id); return it == m_parties.end() ? nullptr : &it->second;
}

PartyId PartySystem::CreateParty(AccountId leader, int maxSize) {
    if (m_memberParty.count(leader)) return 0;   // 이미 파티 소속
    const PartyId id = m_nextId++;
    Party p;
    p.id = id;
    p.leader = leader;
    p.maxSize = maxSize < 1 ? 1 : maxSize;
    p.members.push_back(leader);
    m_parties.emplace(id, std::move(p));
    m_memberParty[leader] = id;
    return id;
}

bool PartySystem::Invite(PartyId party, AccountId inviter, AccountId invitee) {
    Party* p = Find(party);
    if (!p) return false;
    if (std::find(p->members.begin(), p->members.end(), inviter) == p->members.end()) return false;  // 초대자 비소속
    if (m_memberParty.count(invitee)) return false;   // 이미 다른(또는 이 ) 파티 소속
    p->invites.insert(invitee);
    return true;
}

PartySystem::JoinResult PartySystem::AcceptInvite(AccountId invitee, PartyId party) {
    Party* p = Find(party);
    if (!p) return JoinResult::NoParty;
    if (m_memberParty.count(invitee)) return JoinResult::AlreadyInParty;
    if (!p->invites.count(invitee)) return JoinResult::NotInvited;
    if (static_cast<int>(p->members.size()) >= p->maxSize) return JoinResult::Full;
    p->invites.erase(invitee);
    p->members.push_back(invitee);
    m_memberParty[invitee] = party;
    return JoinResult::Ok;
}

void PartySystem::Leave(AccountId member) {
    auto mit = m_memberParty.find(member);
    if (mit == m_memberParty.end()) return;
    const PartyId pid = mit->second;
    Party* p = Find(pid);
    m_memberParty.erase(mit);
    if (!p) return;
    p->members.erase(std::remove(p->members.begin(), p->members.end(), member), p->members.end());
    p->invites.erase(member);
    if (p->members.empty()) {
        m_parties.erase(pid);   // 해산
        return;
    }
    if (p->leader == member) p->leader = p->members.front();   // 승계(앞 멤버)
}

bool PartySystem::Kick(PartyId party, AccountId byLeader, AccountId target) {
    Party* p = Find(party);
    if (!p || p->leader != byLeader || byLeader == target) return false;
    if (std::find(p->members.begin(), p->members.end(), target) == p->members.end()) return false;
    p->members.erase(std::remove(p->members.begin(), p->members.end(), target), p->members.end());
    m_memberParty.erase(target);
    return true;
}

bool PartySystem::Disband(PartyId party, AccountId byLeader) {
    Party* p = Find(party);
    if (!p || p->leader != byLeader) return false;
    for (AccountId m : p->members) m_memberParty.erase(m);
    m_parties.erase(party);
    return true;
}

PartyId PartySystem::PartyOf(AccountId acc) const {
    auto it = m_memberParty.find(acc);
    return it == m_memberParty.end() ? 0 : it->second;
}

AccountId PartySystem::LeaderOf(PartyId party) const {
    const Party* p = Find(party);
    return p ? p->leader : 0;
}

std::vector<AccountId> PartySystem::Members(PartyId party) const {
    const Party* p = Find(party);
    return p ? p->members : std::vector<AccountId>{};
}

size_t PartySystem::Size(PartyId party) const {
    const Party* p = Find(party);
    return p ? p->members.size() : 0;
}

bool PartySystem::IsMember(PartyId party, AccountId acc) const {
    const Party* p = Find(party);
    return p && std::find(p->members.begin(), p->members.end(), acc) != p->members.end();
}

bool PartySystem::IsInvited(PartyId party, AccountId acc) const {
    const Party* p = Find(party);
    return p && p->invites.count(acc) != 0;
}

} // namespace mye::social
