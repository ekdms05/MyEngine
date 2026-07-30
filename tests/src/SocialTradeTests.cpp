// SocialTradeTests.cpp — 2자 직접거래(escrow·확인·재확인·보존) (M12 경제)
#include "TestFramework.h"

#include "mye/social/TradeSystem.h"
#include "mye/persist/ItemLedger.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(TradeAtomicSwap) {
    persist::ItemLedger ledger;
    TradeSystem trade(ledger);
    const uint64_t a = 1, b = 2;

    // A: 골드 500 + 아이템 100x3. B: 아이템 200x1.
    (void)ledger.AdjustGold(a, 500, "seed");
    (void)ledger.Grant(a, 100, 3, "seed");
    (void)ledger.Grant(b, 200, 1, "seed");

    const TradeId t = trade.Begin(a, b);
    MYE_EXPECT(t != 0 && trade.IsActive(t));
    // 이미 거래 중이면 새 거래 불가.
    MYE_EXPECT(trade.Begin(a, 9) == 0);

    // A 제안(내건 것은 즉시 escrow 로 잠김).
    MYE_EXPECT(trade.SetGold(t, a, 500));
    MYE_EXPECT(trade.AddItem(t, a, 100, 3));
    MYE_EXPECT(ledger.GoldBalance(a) == 0 && ledger.ItemBalance(a, 100) == 0);
    MYE_EXPECT(ledger.GoldBalance(TradeSystem::EscrowAccount(t)) == 500);

    // B 제안.
    MYE_EXPECT(trade.AddItem(t, b, 200, 1));
    MYE_EXPECT(ledger.ItemBalance(b, 200) == 0);

    // 양쪽 확인 → 원자 스왑.
    MYE_EXPECT(trade.Confirm(t, a));
    MYE_EXPECT(!trade.IsComplete(t));   // 아직 한쪽
    MYE_EXPECT(trade.Confirm(t, b));
    MYE_EXPECT(trade.IsComplete(t) && !trade.IsActive(t));

    // A 가 낸 것 → B, B 가 낸 것 → A.
    MYE_EXPECT(ledger.GoldBalance(b) == 500 && ledger.ItemBalance(b, 100) == 3);
    MYE_EXPECT(ledger.ItemBalance(a, 200) == 1);
    MYE_EXPECT(ledger.GoldBalance(TradeSystem::EscrowAccount(t)) == 0);
    MYE_EXPECT(ledger.VerifyIntegrity());
}

MYE_TEST(TradeChangeResetsConfirm) {
    persist::ItemLedger ledger;
    TradeSystem trade(ledger);
    const uint64_t a = 1, b = 2;
    (void)ledger.AdjustGold(a, 1000, "seed");

    const TradeId t = trade.Begin(a, b);
    MYE_EXPECT(trade.SetGold(t, a, 100));

    // B 가 먼저 확인.
    MYE_EXPECT(trade.Confirm(t, b));
    MYE_EXPECT(trade.Confirmed(t, b) && !trade.Confirmed(t, a));

    // A 가 제안을 바꾸면 양쪽 확인이 리셋(변경 후 몰래 성사 방지 — 안티스캠).
    MYE_EXPECT(trade.SetGold(t, a, 200));
    MYE_EXPECT(!trade.Confirmed(t, a) && !trade.Confirmed(t, b));
    MYE_EXPECT(trade.IsActive(t) && !trade.IsComplete(t));

    // 다시 양쪽 확인해야 성사.
    MYE_EXPECT(trade.Confirm(t, a) && trade.Confirm(t, b));
    MYE_EXPECT(trade.IsComplete(t));
    MYE_EXPECT(ledger.GoldBalance(b) == 200);   // A 의 200 이 B 에게
}

MYE_TEST(TradeCancelReturnsEscrow) {
    persist::ItemLedger ledger;
    TradeSystem trade(ledger);
    const uint64_t a = 1, b = 2;
    (void)ledger.AdjustGold(a, 300, "seed");
    (void)ledger.Grant(b, 100, 5, "seed");

    const TradeId t = trade.Begin(a, b);
    trade.SetGold(t, a, 300);
    trade.AddItem(t, b, 100, 5);
    MYE_EXPECT(ledger.GoldBalance(a) == 0 && ledger.ItemBalance(b, 100) == 0);

    // 취소 → escrow 원소유자 반환.
    MYE_EXPECT(trade.Cancel(t, a));
    MYE_EXPECT(!trade.IsActive(t));
    MYE_EXPECT(ledger.GoldBalance(a) == 300 && ledger.ItemBalance(b, 100) == 5);
    MYE_EXPECT(ledger.VerifyIntegrity());

    // 취소 후 재확인·재취소 불가.
    MYE_EXPECT(!trade.Confirm(t, a) && !trade.Cancel(t, b));

    // 취소 후 두 참가자 새 거래 가능.
    MYE_EXPECT(trade.Begin(a, b) != 0);
}

MYE_TEST(TradeInsufficientBalanceRejected) {
    persist::ItemLedger ledger;
    TradeSystem trade(ledger);
    const uint64_t a = 1, b = 2;
    (void)ledger.AdjustGold(a, 50, "seed");

    const TradeId t = trade.Begin(a, b);
    // 잔고 초과 제안 → 실패, escrow 로 안 옮김.
    MYE_EXPECT(!trade.SetGold(t, a, 100));
    MYE_EXPECT(ledger.GoldBalance(a) == 50);
    MYE_EXPECT(trade.OfferGold(t, a) == 0);
    // 없는 아이템 제안 → 실패.
    MYE_EXPECT(!trade.AddItem(t, a, 999, 1));
    MYE_EXPECT(ledger.VerifyIntegrity());
}
