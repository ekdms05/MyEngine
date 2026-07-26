// GameplayQuestEconomyTests.cpp — 퀘스트 진행 + 상점 매매 검증 (docs/mmorpg/04, M8)
#include "TestFramework.h"

#include "mye/gameplay/Quest.h"
#include "mye/gameplay/Item.h"
#include "mye/gameplay/Inventory.h"
#include "mye/gameplay/Economy.h"

using namespace mye;
using namespace mye::gameplay;

MYE_TEST(QuestProgressAndTurnIn) {
    QuestDef def;
    def.id = 1;
    def.objectives.push_back(QuestObjective{ObjectiveType::KillMob, /*mob*/ 50, /*req*/ 3, 0});
    def.objectives.push_back(QuestObjective{ObjectiveType::CollectItem, /*item*/ 100, /*req*/ 5, 0});

    QuestLog log;
    MYE_EXPECT(AcceptQuest(log, def));
    MYE_EXPECT(!AcceptQuest(log, def));   // 중복 거부
    MYE_EXPECT(!IsQuestComplete(log, 1));

    // 몹 2마리 처치 → 미완.
    MYE_EXPECT(ReportEvent(log, ObjectiveType::KillMob, 50, 2) == 1);
    MYE_EXPECT(!IsQuestComplete(log, 1));
    // 다른 몹은 무관.
    MYE_EXPECT(ReportEvent(log, ObjectiveType::KillMob, 99, 5) == 0);
    // 1마리 더(상한 클램프) + 아이템 5개 → 완료.
    ReportEvent(log, ObjectiveType::KillMob, 50, 10);   // 상한 3
    ReportEvent(log, ObjectiveType::CollectItem, 100, 5);
    MYE_EXPECT(IsQuestComplete(log, 1));

    // 턴인 → completed 이동, 재수락 불가.
    MYE_EXPECT(TurnInQuest(log, 1));
    MYE_EXPECT(log.active.empty());
    MYE_EXPECT(!AcceptQuest(log, def));   // 완료된 퀘스트 재수락 거부
}

MYE_TEST(EconomyBuySell) {
    ItemCatalog cat;
    cat.Register(ItemDef{100, "Potion", ItemType::Consumable, 10, EquipSlot::None, {}, /*value*/ 20});

    Inventory inv; inv.capacity = 10; inv.gold = 100;

    // 구매 3개(정가 20×3=60) → 골드 40, 포션 3.
    MYE_EXPECT(BuyItem(inv, cat, 100, 3, 1.0f));
    MYE_EXPECT(inv.gold == 40);
    MYE_EXPECT(CountItem(inv, 100) == 3);

    // 골드 부족(정가 20×5=100 > 40) → 실패(무변화).
    MYE_EXPECT(!BuyItem(inv, cat, 100, 5, 1.0f));
    MYE_EXPECT(inv.gold == 40 && CountItem(inv, 100) == 3);

    // 판매 2개(반값 10×2=20) → 골드 60, 포션 1.
    MYE_EXPECT(SellItem(inv, cat, 100, 2, 0.5f));
    MYE_EXPECT(inv.gold == 60);
    MYE_EXPECT(CountItem(inv, 100) == 1);

    // 보유 부족 판매 → 실패.
    MYE_EXPECT(!SellItem(inv, cat, 100, 5, 0.5f));
    MYE_EXPECT(inv.gold == 60 && CountItem(inv, 100) == 1);
}
