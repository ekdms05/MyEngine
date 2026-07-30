// SocialAuctionTests.cpp — 경매장(등록·구매·취소·escrow·보존) (M12 경제)
#include "TestFramework.h"

#include "mye/social/AuctionHouse.h"
#include "mye/persist/ItemLedger.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(AuctionListBuy) {
    persist::ItemLedger ledger;
    AuctionHouse ah(ledger);
    const uint64_t seller = 1, buyer = 2;

    (void)ledger.Grant(seller, 100, 5, "seed");   // 판매자 아이템
    (void)ledger.AdjustGold(buyer, 1000, "seed"); // 구매자 골드

    // 등록 → 아이템 escrow 잠금.
    const ListingId l = ah.List(seller, 100, 5, 300);
    MYE_EXPECT(l != 0);
    MYE_EXPECT(ledger.ItemBalance(seller, 100) == 0);
    MYE_EXPECT(ledger.ItemBalance(AuctionHouse::EscrowAccount(l), 100) == 5);
    MYE_EXPECT(ah.Active().size() == 1 && ah.ActiveByItem(100).size() == 1);

    // 구매 → 골드는 판매자, 아이템은 구매자(원자).
    MYE_EXPECT(ah.Buy(l, buyer));
    MYE_EXPECT(ledger.GoldBalance(buyer) == 700 && ledger.GoldBalance(seller) == 300);
    MYE_EXPECT(ledger.ItemBalance(buyer, 100) == 5);
    MYE_EXPECT(ah.Get(l)->sold && ah.Get(l)->buyer == buyer);
    MYE_EXPECT(ah.Active().empty());   // 더 이상 활성 아님

    // 이미 판매된 것 재구매 불가.
    MYE_EXPECT(!ah.Buy(l, buyer));
    MYE_EXPECT(ledger.VerifyIntegrity());
}

MYE_TEST(AuctionInsufficientAndSelfBuy) {
    persist::ItemLedger ledger;
    AuctionHouse ah(ledger);
    const uint64_t seller = 1, buyer = 2;
    (void)ledger.Grant(seller, 100, 1, "seed");
    (void)ledger.AdjustGold(buyer, 100, "seed");

    // 아이템 없는 등록 실패.
    MYE_EXPECT(ah.List(seller, 999, 1, 50) == 0);
    MYE_EXPECT(ah.List(seller, 100, 5, 50) == 0);   // 5개 없음

    const ListingId l = ah.List(seller, 100, 1, 500);   // 가격 500
    MYE_EXPECT(l != 0);

    // 골드 부족 구매 실패(무변경).
    MYE_EXPECT(!ah.Buy(l, buyer));
    MYE_EXPECT(ledger.GoldBalance(buyer) == 100 && ah.Get(l)->Active());
    // 본인 구매 불가.
    MYE_EXPECT(!ah.Buy(l, seller));
}

MYE_TEST(AuctionCancelReturnsItem) {
    persist::ItemLedger ledger;
    AuctionHouse ah(ledger);
    const uint64_t seller = 1;
    (void)ledger.Grant(seller, 100, 3, "seed");

    const ListingId l = ah.List(seller, 100, 3, 200);
    MYE_EXPECT(ledger.ItemBalance(seller, 100) == 0);

    // 비판매자 취소 불가.
    MYE_EXPECT(!ah.Cancel(l, 999));
    // 판매자 취소 → 아이템 반환.
    MYE_EXPECT(ah.Cancel(l, seller));
    MYE_EXPECT(ledger.ItemBalance(seller, 100) == 3);
    MYE_EXPECT(!ah.Get(l)->Active() && ah.Active().empty());

    // 취소 후 구매·재취소 불가.
    MYE_EXPECT(!ah.Buy(l, 2) && !ah.Cancel(l, seller));
    MYE_EXPECT(ledger.VerifyIntegrity());
}
