// GameServerTests.cpp — 게임플레이↔영속 통합: 로그인→로드→조작→저장→정합 (M8·M10 통합)
#include "TestFramework.h"

#include "mye/gameserver/GameServer.h"

#include <string>

using namespace mye;
using namespace mye::gameserver;

namespace {
// 아이템 카탈로그(포션 스택99, 검 스택1).
gameplay::ItemCatalog MakeCatalog() {
    gameplay::ItemCatalog c;
    gameplay::ItemDef potion; potion.id = 100; potion.name = "potion"; potion.type = gameplay::ItemType::Consumable; potion.stackMax = 99;
    gameplay::ItemDef sword;  sword.id = 200;  sword.name = "sword";  sword.type = gameplay::ItemType::Equipment;  sword.stackMax = 1;
    c.Register(potion);
    c.Register(sword);
    return c;
}
}

MYE_TEST(GameServerJoinLoadsCharacterState) {
    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("hero", "pw").Value();
    const auto cid = p.Characters().Create(acc, "Knight").Value();
    persist::CharacterRecord* rec = p.Characters().GetMutable(cid);
    rec->level = 5; rec->xp = 120;
    rec->strength = 20; rec->vitality = 15;
    rec->posX = 42.0f; rec->posY = 7.0f; rec->sceneId = "town";
    rec->hp = 0;   // 신규 → 스폰 시 최대치

    GameServer gs(p);
    auto sid = gs.Join(acc, cid);
    MYE_EXPECT(static_cast<bool>(sid));
    const PlayerSession* s = gs.Get(sid.Value());
    MYE_EXPECT(s != nullptr);
    MYE_EXPECT(s->characterId == cid && s->accountId == acc);
    MYE_EXPECT(s->prog.level == 5 && s->prog.xp == 120);
    MYE_EXPECT(s->stats.base.strength == 20 && s->stats.base.vitality == 15);
    MYE_EXPECT(s->x == 42.0f && s->y == 7.0f && s->sceneId == "town");
    // 파생 스탯 계산 + 신규라 HP 최대치로 스폰(>0).
    MYE_EXPECT(s->stats.derived.maxHp > 0);
    MYE_EXPECT(s->stats.hp == s->stats.derived.maxHp);

    // charId 생략 시 계정 첫 캐릭터. 이미 접속 중이므로 재조인은 거부(단일 세션).
    MYE_EXPECT(!gs.Join(acc, cid));
    // 다른 계정으로는 이 캐릭터 못 씀.
    const auto acc2 = p.Accounts().Register("other", "pw").Value();
    MYE_EXPECT(!gs.Join(acc2, cid));
}

MYE_TEST(GameServerItemGrantKeepsLedgerConsistent) {
    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("trader", "pw").Value();
    const auto cid = p.Characters().Create(acc, "Merchant").Value();
    const gameplay::ItemCatalog cat = MakeCatalog();

    GameServer gs(p);
    const SessionId sid = gs.Join(acc, cid).Value();

    // 지급: 인벤과 원장이 함께 증가.
    MYE_EXPECT(gs.GrantItem(sid, 100, 10, cat) == 10);
    MYE_EXPECT(gs.GrantItem(sid, 200, 1, cat) == 1);
    MYE_EXPECT(gameplay::CountItem(gs.Get(sid)->inv, 100) == 10);
    MYE_EXPECT(p.Ledger().ItemBalance(cid, 100) == 10);
    MYE_EXPECT(p.Ledger().ItemBalance(cid, 200) == 1);

    // 소모: 함께 감소.
    MYE_EXPECT(gs.ConsumeItem(sid, 100, 4) == 4);
    MYE_EXPECT(gameplay::CountItem(gs.Get(sid)->inv, 100) == 6);
    MYE_EXPECT(p.Ledger().ItemBalance(cid, 100) == 6);

    // 골드: 원장 반영, 음수 잔고 거부.
    MYE_EXPECT(gs.AddGold(sid, 500));
    MYE_EXPECT(gs.Get(sid)->inv.gold == 500);
    MYE_EXPECT(p.Ledger().GoldBalance(cid) == 500);
    MYE_EXPECT(!gs.AddGold(sid, -600));            // 부족 → 실패
    MYE_EXPECT(gs.Get(sid)->inv.gold == 500);

    // 퇴장 후 저장된 스냅샷이 원장과 정합(Reconcile).
    MYE_EXPECT(static_cast<bool>(gs.Leave(sid)));
    const persist::ReconcileReport rep = p.Reconcile(cid);
    MYE_EXPECT(rep.matches);
    MYE_EXPECT(gs.SessionCount() == 0);
}

MYE_TEST(GameServerRoundtripPersistsProgress) {
    persist::PersistenceService p;
    const auto acc = p.Accounts().Register("grinder", "pw").Value();
    const auto cid = p.Characters().Create(acc, "Warrior").Value();
    const gameplay::ItemCatalog cat = MakeCatalog();

    {
        GameServer gs(p);
        const SessionId sid = gs.Join(acc, cid).Value();
        gs.ApplyMove(sid, 10.0f, -3.0f);
        gs.GainXp(sid, 5000);                      // 레벨업
        gs.GrantItem(sid, 100, 25, cat);
        gs.AddGold(sid, 1234);
        MYE_EXPECT(static_cast<bool>(gs.Leave(sid)));
    }

    // 저장된 CharacterRecord 에 진행이 반영됨.
    const persist::CharacterRecord* rec = p.Characters().Get(cid);
    MYE_EXPECT(rec != nullptr);
    MYE_EXPECT(rec->level > 1);                    // 레벨업 반영
    MYE_EXPECT(rec->posX == 10.0f && rec->posY == -3.0f);
    MYE_EXPECT(rec->gold == 1234);
    MYE_EXPECT(rec->items.size() == 1 && rec->items[0].itemId == 100 && rec->items[0].count == 25);

    // 재접속 시 진행이 복원되고 원장과 정합.
    {
        GameServer gs2(p);
        const SessionId sid2 = gs2.Join(acc, cid).Value();
        const PlayerSession* s = gs2.Get(sid2);
        MYE_EXPECT(s->prog.level == rec->level);
        MYE_EXPECT(gameplay::CountItem(s->inv, 100) == 25);
        MYE_EXPECT(s->inv.gold == 1234);
        MYE_EXPECT(p.Reconcile(cid).matches);
    }
}
