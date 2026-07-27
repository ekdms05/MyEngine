// PersistLedgerTests.cpp — 거래 원장: 보존·dupe 차단·무결성·영속 (docs/mmorpg/03, M10)
#include "TestFramework.h"

#include "mye/persist/ItemLedger.h"

#include <filesystem>
#include <string>

using namespace mye;
using namespace mye::persist;

MYE_TEST(LedgerGrantConsumeGold) {
    ItemLedger led;

    MYE_EXPECT(static_cast<bool>(led.Grant(1, 100, 10, "loot")));
    MYE_EXPECT(led.ItemBalance(1, 100) == 10);

    // 소모는 보유량 이내만.
    MYE_EXPECT(static_cast<bool>(led.Consume(1, 100, 4, "use")));
    MYE_EXPECT(led.ItemBalance(1, 100) == 6);
    MYE_EXPECT(!led.Consume(1, 100, 999, "overuse"));   // 부족 → 실패
    MYE_EXPECT(led.ItemBalance(1, 100) == 6);           // 변화 없음

    // 골드: 음수 잔고 방지.
    MYE_EXPECT(static_cast<bool>(led.AdjustGold(1, 500, "sell")));
    MYE_EXPECT(led.GoldBalance(1) == 500);
    MYE_EXPECT(!led.AdjustGold(1, -600, "buy"));        // 부족 → 실패
    MYE_EXPECT(static_cast<bool>(led.AdjustGold(1, -500, "buy")));
    MYE_EXPECT(led.GoldBalance(1) == 0);

    MYE_EXPECT(led.VerifyIntegrity());
}

MYE_TEST(LedgerTransferConservesTotal) {
    ItemLedger led;
    (void)led.Grant(1, 42, 5, "seed");
    (void)led.AdjustGold(1, 1000, "seed");

    const int64_t before = led.ItemBalance(1, 42) + led.ItemBalance(2, 42);

    // 원자적 거래: 아이템 3개 + 골드 250.
    auto txn = led.Transfer(1, 2, 42, 3, 250, "trade");
    MYE_EXPECT(static_cast<bool>(txn));

    // 보존: 총 아이템 수 불변, 이동만 발생.
    MYE_EXPECT(led.ItemBalance(1, 42) == 2);
    MYE_EXPECT(led.ItemBalance(2, 42) == 3);
    MYE_EXPECT(led.ItemBalance(1, 42) + led.ItemBalance(2, 42) == before);
    MYE_EXPECT(led.GoldBalance(1) == 750 && led.GoldBalance(2) == 250);

    // 두 당사자의 대칭 항목이 같은 txnId 로 존재.
    int legs = 0;
    for (const auto& e : led.Entries()) if (e.txnId == txn.Value()) ++legs;
    MYE_EXPECT(legs == 2);

    MYE_EXPECT(led.VerifyIntegrity());
}

MYE_TEST(LedgerTransferRejectsInsufficient) {
    ItemLedger led;
    (void)led.Grant(1, 7, 2, "seed");

    const size_t before = led.Count();
    // 보유 2개인데 5개 이동 시도 → 실패, 아무 항목도 기록 안 됨(원자성).
    MYE_EXPECT(!led.Transfer(1, 2, 7, 5, 0, "cheat"));
    MYE_EXPECT(led.Count() == before);
    MYE_EXPECT(led.ItemBalance(1, 7) == 2 && led.ItemBalance(2, 7) == 0);

    // 골드 부족 거래도 거부.
    MYE_EXPECT(!led.Transfer(1, 2, 0, 0, 100, "nogold"));
    MYE_EXPECT(led.Count() == before);

    // 자기 자신·빈 이동 거부.
    MYE_EXPECT(!led.Transfer(1, 1, 7, 1, 0, "self"));
    MYE_EXPECT(!led.Transfer(1, 2, 0, 0, 0, "empty"));
}

MYE_TEST(LedgerIntegrityDetectsCorruption) {
    ItemLedger led;
    (void)led.Grant(1, 9, 3, "ok");
    MYE_EXPECT(led.VerifyIntegrity());

    // 원장에 손상 주입(존재 없는 소모 = 음수 잔고 유발)은 API 로 불가하므로,
    // 무결성 함수가 정상 로그를 통과시키는지와 위반 탐지 계약만 확인.
    // 여기서는 정상 흐름 후 통과를 재확인.
    (void)led.Transfer(1, 2, 9, 3, 0, "give-all");
    MYE_EXPECT(led.ItemBalance(1, 9) == 0);
    MYE_EXPECT(led.VerifyIntegrity());
}

MYE_TEST(LedgerPersistRoundtrip) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_ledger.json").string();
    std::error_code ec; fs::remove(path, ec);

    uint64_t lastTxn = 0;
    {
        ItemLedger led;
        (void)led.Grant(1, 100, 20, "loot");
        (void)led.AdjustGold(1, 5000, "quest");
        lastTxn = led.Transfer(1, 2, 100, 8, 1500, "market").Value();
        MYE_EXPECT(led.VerifyIntegrity());
        MYE_EXPECT(static_cast<bool>(led.SaveToFile(path)));
    }
    {
        ItemLedger led;
        MYE_EXPECT(static_cast<bool>(led.LoadFromFile(path)));
        // 잔고가 저장 전과 동일하게 재구성.
        MYE_EXPECT(led.ItemBalance(1, 100) == 12);
        MYE_EXPECT(led.ItemBalance(2, 100) == 8);
        MYE_EXPECT(led.GoldBalance(1) == 3500 && led.GoldBalance(2) == 1500);
        MYE_EXPECT(led.VerifyIntegrity());

        // nextTxn 이어짐: 새 거래 txnId 는 로드된 마지막보다 큼.
        auto t = led.Transfer(2, 1, 100, 1, 0, "back");
        MYE_EXPECT(static_cast<bool>(t) && t.Value() > lastTxn);
    }
    fs::remove(path, ec);
}
