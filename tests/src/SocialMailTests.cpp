// SocialMailTests.cpp — 우편(오프라인 배송·escrow·보존) (M12 소셜)
#include "TestFramework.h"

#include "mye/social/MailSystem.h"
#include "mye/persist/ItemLedger.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(MailSendClaimEscrow) {
    persist::ItemLedger ledger;
    MailSystem mail(ledger);
    const uint64_t sender = 1, receiver = 2;

    // 발신자에게 골드·아이템 지급.
    (void)ledger.AdjustGold(sender, 1000, "seed");
    (void)ledger.Grant(sender, 100, 10, "seed");

    // 발송(골드 300 + 아이템 5) → 발신자 잔고 차감, escrow 보관.
    std::vector<MailItem> att = { {100, 5} };
    const MailId id = mail.Send(sender, receiver, "선물", "받으세요", 300, att);
    MYE_EXPECT(id != 0);
    MYE_EXPECT(ledger.GoldBalance(sender) == 700 && ledger.ItemBalance(sender, 100) == 5);
    // 수령 전엔 수신자 잔고 변화 없음(escrow 에 잠김).
    MYE_EXPECT(ledger.GoldBalance(receiver) == 0 && ledger.ItemBalance(receiver, 100) == 0);
    MYE_EXPECT(ledger.GoldBalance(MailSystem::EscrowAccount(id)) == 300);

    // 수신함에 미수령 우편.
    MYE_EXPECT(mail.Inbox(receiver).size() == 1 && mail.Inbox(receiver)[0] == id);

    // 다른 사람은 수령 불가.
    MYE_EXPECT(!mail.Claim(id, 999));

    // 수령 → 첨부 이관, claimed.
    MYE_EXPECT(mail.Claim(id, receiver));
    MYE_EXPECT(ledger.GoldBalance(receiver) == 300 && ledger.ItemBalance(receiver, 100) == 5);
    MYE_EXPECT(ledger.GoldBalance(MailSystem::EscrowAccount(id)) == 0);
    MYE_EXPECT(mail.Get(id)->claimed && mail.Inbox(receiver).empty());

    // 재수령 불가.
    MYE_EXPECT(!mail.Claim(id, receiver));

    // 보존·무결성.
    MYE_EXPECT(ledger.VerifyIntegrity());
}

MYE_TEST(MailInsufficientAndTextOnly) {
    persist::ItemLedger ledger;
    MailSystem mail(ledger);
    (void)ledger.AdjustGold(1, 100, "seed");

    // 잔고 초과 첨부 → 발송 실패, 아무것도 안 옮김.
    MYE_EXPECT(mail.Send(1, 2, "s", "b", 500, {}) == 0);
    MYE_EXPECT(ledger.GoldBalance(1) == 100);
    MYE_EXPECT(mail.Count() == 0);

    // 없는 아이템 첨부 → 실패.
    std::vector<MailItem> att = { {77, 1} };
    MYE_EXPECT(mail.Send(1, 2, "s", "b", 0, att) == 0);

    // 첨부 없는 텍스트 우편 → 성공, 수령 시 잔고 변화 없음.
    const MailId id = mail.Send(1, 2, "안녕", "본문만", 0, {});
    MYE_EXPECT(id != 0 && !mail.Get(id)->HasAttachment());
    MYE_EXPECT(mail.Claim(id, 2));
    MYE_EXPECT(ledger.GoldBalance(2) == 0);
    MYE_EXPECT(ledger.VerifyIntegrity());
}

MYE_TEST(MailInboxMultiple) {
    persist::ItemLedger ledger;
    MailSystem mail(ledger);

    // 여러 통(첨부 없음) 발송 순서 유지.
    const MailId a = mail.Send(1, 5, "1", "", 0, {});
    const MailId b = mail.Send(2, 5, "2", "", 0, {});
    const MailId c = mail.Send(3, 6, "3", "", 0, {});   // 다른 수신자

    auto inbox5 = mail.Inbox(5);
    MYE_EXPECT(inbox5.size() == 2 && inbox5[0] == a && inbox5[1] == b);
    MYE_EXPECT(mail.Inbox(6).size() == 1 && mail.Inbox(6)[0] == c);

    // a 수령 후 수신함에서 제외.
    mail.Claim(a, 5);
    MYE_EXPECT(mail.Inbox(5).size() == 1 && mail.Inbox(5)[0] == b);
}
