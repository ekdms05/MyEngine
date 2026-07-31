// mye/mmo/Monster.cpp — 몹 카탈로그 시드 (Monster.h 참조)
#include "mye/mmo/Monster.h"

namespace mye::mmo {

void RegisterStarterMonsters(MonsterCatalog& cat) {
    // 아이템 id 규약(예시): 100=하급물약, 200=슬라임젤리, 201=고블린가죽, 202=오크이빨.
    // 슬라임 — 가장 약함, 흔한 젤리 드롭.
    {
        MonsterDef m;
        m.id = 1; m.name = "슬라임"; m.level = 3; m.toughness = 0.8f; m.xpReward = 20;
        m.loot.rolls = 1; m.loot.goldMin = 2; m.loot.goldMax = 8;
        m.loot.entries = {
            { 200, 70, 1, 2 },   // 슬라임젤리(흔함)
            { 100, 30, 1, 1 },   // 하급물약
        };
        cat.Register(m);
    }
    // 고블린 — 중간, 가죽 드롭.
    {
        MonsterDef m;
        m.id = 2; m.name = "고블린"; m.level = 6; m.toughness = 1.1f; m.xpReward = 45;
        m.loot.rolls = 1; m.loot.goldMin = 6; m.loot.goldMax = 18;
        m.loot.entries = {
            { 201, 60, 1, 1 },   // 고블린가죽
            { 100, 35, 1, 2 },   // 하급물약
            { 200, 5,  1, 1 },
        };
        cat.Register(m);
    }
    // 오크 — 강함, 이빨(희귀) 드롭.
    {
        MonsterDef m;
        m.id = 3; m.name = "오크"; m.level = 10; m.toughness = 1.5f; m.xpReward = 90;
        m.loot.rolls = 2; m.loot.goldMin = 15; m.loot.goldMax = 40;
        m.loot.entries = {
            { 202, 20, 1, 1 },   // 오크이빨(희귀)
            { 201, 50, 1, 2 },
            { 100, 30, 1, 3 },
        };
        cat.Register(m);
    }
}

} // namespace mye::mmo
