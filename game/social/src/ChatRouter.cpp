// mye/social/ChatRouter.cpp — 채팅 라우팅/필터 구현 (ChatRouter.h 참조)
#include "mye/social/ChatRouter.h"

#include <algorithm>

namespace mye::social {

void ChatRouter::AddBannedWord(std::string_view w) {
    if (!w.empty()) m_banned.emplace_back(w);
}

std::string ChatRouter::Filter(std::string_view text) const {
    std::string out(text);
    for (const std::string& bad : m_banned) {
        if (bad.empty()) continue;
        size_t pos = 0;
        while ((pos = out.find(bad, pos)) != std::string::npos) {
            for (size_t i = 0; i < bad.size(); ++i) out[pos + i] = '*';
            pos += bad.size();
        }
    }
    return out;
}

std::vector<RoutedMessage> ChatRouter::Route(const ChatMessage& msg, const FriendGraph& fg,
                                             const std::function<bool(AccountId)>& isOnline) const {
    auto online = [&](AccountId a) { return !isOnline || isOnline(a); };
    const std::string body = Filter(msg.text);
    std::vector<AccountId> recipients;

    if (msg.channel == ChatChannel::Whisper) {
        // 귓속말: 대상 1명. 온라인 + 양방향 차단 없어야 전달.
        if (msg.to != 0 && msg.to != msg.from && online(msg.to) && fg.CanInteract(msg.from, msg.to))
            recipients.push_back(msg.to);
    } else {
        // 후보 = 채널 조건을 만족하는 참가자.
        auto zoneIt = m_zone.find(msg.from);
        auto partyIt = m_party.find(msg.from);
        auto guildIt = m_guild.find(msg.from);
        for (AccountId a : m_participants) {
            if (a == msg.from) continue;                 // 발신자 제외(클라 에코)
            if (!online(a)) continue;
            if (fg.IsBlocked(a, msg.from)) continue;      // 수신자가 발신자를 차단 → 안 받음
            bool ok = false;
            switch (msg.channel) {
                case ChatChannel::World: ok = true; break;
                case ChatChannel::Local: {
                    auto az = m_zone.find(a);
                    ok = zoneIt != m_zone.end() && az != m_zone.end() && az->second == zoneIt->second;
                    break;
                }
                case ChatChannel::Party: {
                    auto ap = m_party.find(a);
                    ok = partyIt != m_party.end() && ap != m_party.end() && ap->second == partyIt->second;
                    break;
                }
                case ChatChannel::Guild: {
                    auto ag = m_guild.find(a);
                    ok = guildIt != m_guild.end() && ag != m_guild.end() && ag->second == guildIt->second;
                    break;
                }
                default: break;
            }
            if (ok) recipients.push_back(a);
        }
    }

    std::sort(recipients.begin(), recipients.end());   // 결정론 순서
    std::vector<RoutedMessage> out;
    out.reserve(recipients.size());
    for (AccountId r : recipients) out.push_back(RoutedMessage{ r, body });
    return out;
}

} // namespace mye::social
