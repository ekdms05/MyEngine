// PersistCharacterTests.cpp — 캐릭터 생성·계정 인덱스·월드 상태·영속 왕복 (docs/mmorpg/03, M10)
#include "TestFramework.h"

#include "mye/persist/CharacterStore.h"

#include <filesystem>
#include <string>

using namespace mye;
using namespace mye::persist;

MYE_TEST(CharacterCreateAndAccountIndex) {
    CharacterStore store;

    auto a = store.Create(1, "Aria");
    MYE_EXPECT(static_cast<bool>(a));
    const CharacterId ariaId = a.Value();
    MYE_EXPECT(ariaId != 0);

    // 같은 계정에 둘째 캐릭터.
    auto b = store.Create(1, "Borin");
    MYE_EXPECT(static_cast<bool>(b));

    // 다른 계정 캐릭터.
    MYE_EXPECT(static_cast<bool>(store.Create(2, "Cyra")));

    // 유효하지 않은 계정·빈 이름·이름 중복 거부.
    MYE_EXPECT(!store.Create(0, "Nobody"));
    MYE_EXPECT(!store.Create(1, ""));
    MYE_EXPECT(!store.Create(1, "Aria"));   // 전역 이름 중복

    // 계정 1 은 캐릭터 2명, 계정 2 는 1명.
    MYE_EXPECT(store.ListByAccount(1).size() == 2);
    MYE_EXPECT(store.ListByAccount(2).size() == 1);
    MYE_EXPECT(store.ListByAccount(99).empty());

    // 이름 조회.
    const CharacterRecord* aria = store.FindByName("Aria");
    MYE_EXPECT(aria != nullptr && aria->id == ariaId && aria->accountId == 1);
}

MYE_TEST(CharacterWorldStateAndDelete) {
    CharacterStore store;
    const CharacterId id = store.Create(7, "Dorn").Value();

    // 월드 상태 편집(재접속 복원 대상).
    CharacterRecord* c = store.GetMutable(id);
    MYE_EXPECT(c != nullptr);
    c->sceneId = "village_01";
    c->posX = 128.5f; c->posY = -64.25f;
    c->level = 12; c->xp = 3400;
    c->strength = 22; c->vitality = 18;
    c->hp = 240; c->mp = 55; c->gold = 999;
    c->items.push_back({100, 5});   // 포션 x5
    c->items.push_back({200, 1});   // 검 x1

    const CharacterRecord* r = store.Get(id);
    MYE_EXPECT(r->level == 12 && r->items.size() == 2 && r->gold == 999);

    // 삭제 → 이름 인덱스도 해제되어 재사용 가능.
    MYE_EXPECT(store.Delete(id));
    MYE_EXPECT(store.Get(id) == nullptr);
    MYE_EXPECT(store.FindByName("Dorn") == nullptr);
    MYE_EXPECT(static_cast<bool>(store.Create(7, "Dorn")));   // 이름 재사용
}

MYE_TEST(CharacterPersistRoundtrip) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_characters.json").string();
    std::error_code ec; fs::remove(path, ec);

    CharacterId erisId = 0;
    {
        CharacterStore store;
        erisId = store.Create(5, "Eris").Value();
        CharacterRecord* c = store.GetMutable(erisId);
        c->sceneId = "dungeon_03";
        c->posX = 42.0f; c->posY = 7.5f;
        c->level = 30; c->xp = 123456;
        c->intellect = 40; c->hp = 500; c->mp = 300; c->gold = 250000;
        c->items = {{10, 99}, {11, 1}, {12, 3}};
        (void)store.Create(6, "Faye");   // 다른 계정 캐릭터
        MYE_EXPECT(static_cast<bool>(store.SaveToFile(path)));
    }
    {
        CharacterStore loaded;
        MYE_EXPECT(static_cast<bool>(loaded.LoadFromFile(path)));
        MYE_EXPECT(loaded.Count() == 2);

        const CharacterRecord* c = loaded.Get(erisId);
        MYE_EXPECT(c != nullptr);
        MYE_EXPECT(c->name == "Eris" && c->accountId == 5);
        MYE_EXPECT(c->sceneId == "dungeon_03");
        MYE_EXPECT(c->posX == 42.0f && c->posY == 7.5f);
        MYE_EXPECT(c->level == 30 && c->xp == 123456);
        MYE_EXPECT(c->intellect == 40 && c->hp == 500 && c->mp == 300);
        MYE_EXPECT(c->gold == 250000);
        MYE_EXPECT(c->items.size() == 3 && c->items[0].itemId == 10 && c->items[0].count == 99);

        // 이름 인덱스 재구축 확인.
        MYE_EXPECT(loaded.FindByName("Faye") != nullptr);
        MYE_EXPECT(loaded.ListByAccount(5).size() == 1);

        // nextId 이어짐(새 캐릭터 id 는 기존보다 큼).
        auto g = loaded.Create(5, "Gus");
        MYE_EXPECT(static_cast<bool>(g) && g.Value() > erisId);
    }
    fs::remove(path, ec);
}
