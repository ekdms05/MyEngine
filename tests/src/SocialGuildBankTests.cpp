// SocialGuildBankTests.cpp — 길드 은행(원장 연동·직급 권한·보존) (M12 소셜)
#include "TestFramework.h"

#include "mye/social/GuildBank.h"
#include "mye/social/GuildSystem.h"
#include "mye/persist/ItemLedger.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(GuildBankDepositWithdrawGold) {
    persist::ItemLedger ledger;
    GuildBank bank(ledger, GuildRank::Officer);
    const GuildId g = 1;
    const uint64_t charA = 100, charB = 200;

    // 캐릭터에 초기 골드 지급(원장).
    (void)ledger.AdjustGold(charA, 1000, "seed");

    // 예치: 캐릭터 A 골드 → 금고.
    MYE_EXPECT(bank.DepositGold(g, charA, 300));
    MYE_EXPECT(bank.Gold(g) == 300);
    MYE_EXPECT(ledger.GoldBalance(charA) == 700);

    // 잔고 초과 예치 실패(원장 보존).
    MYE_EXPECT(!bank.DepositGold(g, charA, 999999));
    MYE_EXPECT(bank.Gold(g) == 300);

    // 인출: Officer 권한이면 성공.
    MYE_EXPECT(bank.WithdrawGold(g, charB, GuildRank::Officer, 100));
    MYE_EXPECT(bank.Gold(g) == 200 && ledger.GoldBalance(charB) == 100);

    // Member 권한은 인출 불가.
    MYE_EXPECT(!bank.WithdrawGold(g, charB, GuildRank::Member, 50));
    MYE_EXPECT(bank.Gold(g) == 200);

    // 금고 잔고 초과 인출 실패.
    MYE_EXPECT(!bank.WithdrawGold(g, charB, GuildRank::Leader, 9999));

    // 무결성: 원장 음수 잔고 없음.
    MYE_EXPECT(ledger.VerifyIntegrity());
}

MYE_TEST(GuildBankItemsAndConservation) {
    persist::ItemLedger ledger;
    GuildBank bank(ledger);
    const GuildId g = 5;
    const uint64_t charA = 10, charB = 20;

    // A 에게 아이템 지급.
    (void)ledger.Grant(charA, 100, 20, "seed");

    // 예치 8개 → 금고 8, A 12.
    MYE_EXPECT(bank.DepositItem(g, charA, 100, 8));
    MYE_EXPECT(bank.ItemBalance(g, 100) == 8 && ledger.ItemBalance(charA, 100) == 12);

    // B(Leader)가 5개 인출 → 금고 3, B 5.
    MYE_EXPECT(bank.WithdrawItem(g, charB, GuildRank::Leader, 100, 5));
    MYE_EXPECT(bank.ItemBalance(g, 100) == 3 && ledger.ItemBalance(charB, 100) == 5);

    // 보존: A(12) + 금고(3) + B(5) = 원래 20(복제/유실 없음).
    MYE_EXPECT(ledger.ItemBalance(charA, 100) + bank.ItemBalance(g, 100) + ledger.ItemBalance(charB, 100) == 20);
    MYE_EXPECT(ledger.VerifyIntegrity());

    // 금고 초과 인출 실패(보존).
    MYE_EXPECT(!bank.WithdrawItem(g, charB, GuildRank::Leader, 100, 99));
    MYE_EXPECT(bank.ItemBalance(g, 100) == 3);
}
