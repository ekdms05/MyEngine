// mye/social/ChatRouter.h — 채팅 채널 라우팅 + 차단/필터 (docs/mmorpg/09, M12 소셜)
//
// [게임 레이어] 채널(월드/지역/파티/길드/귓속말)별 수신자 계산 + 욕설 필터. 차단(FriendGraph)은
// 상대 메시지 수신을 막는다(귓속말은 양방향 차단 검사). 순수 로직 — 결정론·단위 테스트.
#pragma once

#include "mye/social/FriendGraph.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mye::social {

enum class ChatChannel : uint8_t { World, Local, Party, Guild, Whisper };

struct ChatMessage {
    AccountId   from = 0;
    ChatChannel channel = ChatChannel::World;
    AccountId   to = 0;          // Whisper 대상(그 외 채널은 0)
    std::string text;
};

struct RoutedMessage {
    AccountId   recipient = 0;
    std::string text;            // 필터 적용된 본문
};

class ChatRouter {
public:
    // ---- 멤버십/위치(라우팅 대상 계산) ----
    void RegisterAccount(AccountId acc) { m_participants.insert(acc); }
    void SetZone(AccountId acc, uint64_t zoneId)  { m_participants.insert(acc); m_zone[acc] = zoneId; }
    void JoinParty(AccountId acc, uint64_t party) { m_participants.insert(acc); m_party[acc] = party; }
    void LeaveParty(AccountId acc)                { m_party.erase(acc); }
    void JoinGuild(AccountId acc, uint64_t guild) { m_participants.insert(acc); m_guild[acc] = guild; }
    void LeaveGuild(AccountId acc)                { m_guild.erase(acc); }

    // ---- 욕설/금칙어 필터 ----
    void AddBannedWord(std::string_view w);
    std::string Filter(std::string_view text) const;   // 금칙어를 *로 치환

    // ---- 라우팅 ----
    // 채널별 수신자(발신자 제외·차단 반영·온라인 필터) + 필터된 본문. isOnline nullptr = 전원 온라인 취급.
    std::vector<RoutedMessage> Route(const ChatMessage& msg, const FriendGraph& fg,
                                     const std::function<bool(AccountId)>& isOnline) const;

private:
    std::unordered_set<AccountId>              m_participants;
    std::unordered_map<AccountId, uint64_t>    m_zone;
    std::unordered_map<AccountId, uint64_t>    m_party;
    std::unordered_map<AccountId, uint64_t>    m_guild;
    std::vector<std::string>                   m_banned;
};

} // namespace mye::social
