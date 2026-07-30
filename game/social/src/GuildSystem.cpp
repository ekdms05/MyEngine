// mye/social/GuildSystem.cpp — 길드 시스템 구현 (GuildSystem.h 참조)
#include "mye/social/GuildSystem.h"

namespace mye::social {

GuildSystem::Guild*       GuildSystem::Find(GuildId id) {
    auto it = m_guilds.find(id); return it == m_guilds.end() ? nullptr : &it->second;
}
const GuildSystem::Guild* GuildSystem::Find(GuildId id) const {
    auto it = m_guilds.find(id); return it == m_guilds.end() ? nullptr : &it->second;
}

GuildId GuildSystem::CreateGuild(AccountId leader, std::string name, int maxSize) {
    if (m_memberGuild.count(leader)) return 0;
    const GuildId id = m_nextId++;
    Guild g;
    g.id = id;
    g.name = std::move(name);
    g.leader = leader;
    g.maxSize = maxSize < 1 ? 1 : maxSize;
    g.members.emplace(leader, GuildRank::Leader);
    m_guilds.emplace(id, std::move(g));
    m_memberGuild[leader] = id;
    return id;
}

bool GuildSystem::Invite(GuildId gid, AccountId inviter, AccountId invitee) {
    Guild* g = Find(gid);
    if (!g) return false;
    auto it = g->members.find(inviter);
    if (it == g->members.end() || it->second < GuildRank::Officer) return false;   // Officer+ 만
    if (m_memberGuild.count(invitee)) return false;
    g->invites.insert(invitee);
    return true;
}

GuildSystem::JoinResult GuildSystem::AcceptInvite(AccountId invitee, GuildId gid) {
    Guild* g = Find(gid);
    if (!g) return JoinResult::NoGuild;
    if (m_memberGuild.count(invitee)) return JoinResult::AlreadyInGuild;
    if (!g->invites.count(invitee)) return JoinResult::NotInvited;
    if (static_cast<int>(g->members.size()) >= g->maxSize) return JoinResult::Full;
    g->invites.erase(invitee);
    g->members.emplace(invitee, GuildRank::Member);
    m_memberGuild[invitee] = gid;
    return JoinResult::Ok;
}

void GuildSystem::Leave(AccountId member) {
    auto mit = m_memberGuild.find(member);
    if (mit == m_memberGuild.end()) return;
    const GuildId gid = mit->second;
    Guild* g = Find(gid);
    m_memberGuild.erase(mit);
    if (!g) return;
    const bool wasLeader = (g->leader == member);
    g->members.erase(member);
    g->invites.erase(member);
    if (g->members.empty()) { m_guilds.erase(gid); return; }
    if (wasLeader) {
        // 최상위 직급(그다음 계정 id 최소) 멤버로 승계.
        AccountId best = 0; GuildRank bestRank = GuildRank::Member; bool first = true;
        for (const auto& [acc, rank] : g->members) {
            if (first || rank > bestRank || (rank == bestRank && acc < best)) { best = acc; bestRank = rank; first = false; }
        }
        g->leader = best;
        g->members[best] = GuildRank::Leader;
    }
}

bool GuildSystem::Kick(GuildId gid, AccountId by, AccountId target) {
    Guild* g = Find(gid);
    if (!g || by == target) return false;
    auto bit = g->members.find(by);
    auto tit = g->members.find(target);
    if (bit == g->members.end() || tit == g->members.end()) return false;
    if (bit->second <= tit->second) return false;   // 상위 직급만 하위 추방
    g->members.erase(target);
    g->invites.erase(target);
    m_memberGuild.erase(target);
    return true;
}

bool GuildSystem::Promote(GuildId gid, AccountId by, AccountId target) {
    Guild* g = Find(gid);
    if (!g || RankOf(gid, by) != GuildRank::Leader) return false;
    auto tit = g->members.find(target);
    if (tit == g->members.end() || tit->second != GuildRank::Member) return false;
    tit->second = GuildRank::Officer;
    return true;
}

bool GuildSystem::Demote(GuildId gid, AccountId by, AccountId target) {
    Guild* g = Find(gid);
    if (!g || RankOf(gid, by) != GuildRank::Leader) return false;
    auto tit = g->members.find(target);
    if (tit == g->members.end() || tit->second != GuildRank::Officer) return false;
    tit->second = GuildRank::Member;
    return true;
}

bool GuildSystem::TransferLeadership(GuildId gid, AccountId leader, AccountId target) {
    Guild* g = Find(gid);
    if (!g || g->leader != leader || leader == target) return false;
    auto tit = g->members.find(target);
    if (tit == g->members.end()) return false;
    g->members[leader] = GuildRank::Officer;
    tit->second = GuildRank::Leader;
    g->leader = target;
    return true;
}

bool GuildSystem::Disband(GuildId gid, AccountId leader) {
    Guild* g = Find(gid);
    if (!g || g->leader != leader) return false;
    for (const auto& [acc, rank] : g->members) { (void)rank; m_memberGuild.erase(acc); }
    m_guilds.erase(gid);
    return true;
}

GuildId GuildSystem::GuildOf(AccountId acc) const {
    auto it = m_memberGuild.find(acc);
    return it == m_memberGuild.end() ? 0 : it->second;
}

bool GuildSystem::IsMember(GuildId gid, AccountId acc) const {
    const Guild* g = Find(gid);
    return g && g->members.count(acc) != 0;
}

GuildRank GuildSystem::RankOf(GuildId gid, AccountId acc) const {
    const Guild* g = Find(gid);
    if (!g) return GuildRank::Member;
    auto it = g->members.find(acc);
    return it == g->members.end() ? GuildRank::Member : it->second;
}

AccountId GuildSystem::LeaderOf(GuildId gid) const {
    const Guild* g = Find(gid);
    return g ? g->leader : 0;
}

std::string GuildSystem::NameOf(GuildId gid) const {
    const Guild* g = Find(gid);
    return g ? g->name : std::string{};
}

std::vector<AccountId> GuildSystem::Members(GuildId gid) const {
    std::vector<AccountId> out;
    const Guild* g = Find(gid);
    if (g) for (const auto& [acc, rank] : g->members) { (void)rank; out.push_back(acc); }
    return out;
}

size_t GuildSystem::Size(GuildId gid) const {
    const Guild* g = Find(gid);
    return g ? g->members.size() : 0;
}

bool GuildSystem::IsInvited(GuildId gid, AccountId acc) const {
    const Guild* g = Find(gid);
    return g && g->invites.count(acc) != 0;
}

} // namespace mye::social
