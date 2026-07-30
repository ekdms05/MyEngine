// SocialChatTests.cpp — 채팅 채널 라우팅 + 차단/필터 (M12 소셜)
#include "TestFramework.h"

#include "mye/social/ChatRouter.h"

#include <algorithm>

using namespace mye;
using namespace mye::social;

namespace {
bool HasRecipient(const std::vector<RoutedMessage>& v, AccountId a) {
    return std::any_of(v.begin(), v.end(), [&](const RoutedMessage& m) { return m.recipient == a; });
}
}

MYE_TEST(ChatWorldAndLocalRouting) {
    ChatRouter r;
    FriendGraph fg;
    // 1,2 는 존 A, 3 은 존 B. 전원 등록.
    r.SetZone(1, 100); r.SetZone(2, 100); r.SetZone(3, 200);

    // 월드: 발신자(1) 제외 전원(2,3).
    ChatMessage w{1, ChatChannel::World, 0, "hello"};
    auto rw = r.Route(w, fg, nullptr);
    MYE_EXPECT(rw.size() == 2 && HasRecipient(rw, 2) && HasRecipient(rw, 3));
    MYE_EXPECT(!HasRecipient(rw, 1));   // 발신자 제외
    MYE_EXPECT(rw[0].text == "hello");

    // 지역(존 A): 같은 존 2 만.
    ChatMessage l{1, ChatChannel::Local, 0, "local"};
    auto rl = r.Route(l, fg, nullptr);
    MYE_EXPECT(rl.size() == 1 && HasRecipient(rl, 2) && !HasRecipient(rl, 3));
}

MYE_TEST(ChatPartyGuildAndOnline) {
    ChatRouter r;
    FriendGraph fg;
    r.JoinParty(1, 7); r.JoinParty(2, 7); r.JoinParty(3, 9);   // 1,2 파티7, 3 파티9
    r.JoinGuild(1, 50); r.JoinGuild(3, 50);                    // 1,3 길드50

    // 파티: 같은 파티 2 만.
    auto rp = r.Route(ChatMessage{1, ChatChannel::Party, 0, "party"}, fg, nullptr);
    MYE_EXPECT(rp.size() == 1 && HasRecipient(rp, 2));

    // 길드: 같은 길드 3 만.
    auto rg = r.Route(ChatMessage{1, ChatChannel::Guild, 0, "guild"}, fg, nullptr);
    MYE_EXPECT(rg.size() == 1 && HasRecipient(rg, 3));

    // 온라인 필터: 2 오프라인이면 월드에서 제외.
    auto isOnline = [](AccountId a) { return a != 2; };
    auto rw = r.Route(ChatMessage{1, ChatChannel::World, 0, "hi"}, fg, isOnline);
    MYE_EXPECT(!HasRecipient(rw, 2) && HasRecipient(rw, 3));
}

MYE_TEST(ChatWhisperAndBlock) {
    ChatRouter r;
    FriendGraph fg;
    r.RegisterAccount(1); r.RegisterAccount(2);

    // 정상 귓속말.
    auto rok = r.Route(ChatMessage{1, ChatChannel::Whisper, 2, "hey"}, fg, nullptr);
    MYE_EXPECT(rok.size() == 1 && rok[0].recipient == 2 && rok[0].text == "hey");

    // 2 가 1 을 차단 → 귓속말 전달 안 됨.
    fg.Block(2, 1);
    auto rblk = r.Route(ChatMessage{1, ChatChannel::Whisper, 2, "hey"}, fg, nullptr);
    MYE_EXPECT(rblk.empty());

    // 차단은 월드 채널에서도 수신 차단(2 는 1 메시지 안 받음).
    r.SetZone(1, 1); r.SetZone(2, 1);
    auto rworld = r.Route(ChatMessage{1, ChatChannel::World, 0, "all"}, fg, nullptr);
    MYE_EXPECT(!HasRecipient(rworld, 2));

    // 자기 자신 귓속말 무시.
    auto rself = r.Route(ChatMessage{1, ChatChannel::Whisper, 1, "me"}, fg, nullptr);
    MYE_EXPECT(rself.empty());
}

MYE_TEST(ChatProfanityFilter) {
    ChatRouter r;
    FriendGraph fg;
    r.AddBannedWord("욕설");
    r.AddBannedWord("badword");
    r.RegisterAccount(1); r.RegisterAccount(2);

    // 필터 직접.
    MYE_EXPECT(r.Filter("이건 욕설 이야") == "이건 ****** 이야");   // '욕설'(6바이트 UTF-8) → ******
    MYE_EXPECT(r.Filter("a badword b badword") == "a ******* b *******");
    MYE_EXPECT(r.Filter("clean") == "clean");

    // 라우팅 시 본문에 필터 적용.
    auto rr = r.Route(ChatMessage{1, ChatChannel::Whisper, 2, "hello badword"}, fg, nullptr);
    MYE_EXPECT(rr.size() == 1 && rr[0].text == "hello *******");
}
