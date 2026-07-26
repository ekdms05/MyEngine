// mye/gameplay/Stats.h — RPG 스탯 컴포넌트 + 파생 계산 (docs/mmorpg/04, M8)
//
// 데이터드리븐 스탯: 기본 스탯(레벨·힘·민첩·지능·체력)에서 파생 스탯(HP/MP/공격/방어/크리 등)을
// 공식으로 계산한다. 장비·버프 수정자는 StatModifiers 로 합산해 파생에 반영한다.
// ECS 컴포넌트(Stats)로 엔티티에 부착 — 플레이어·몬스터·NPC 공용.
#pragma once

#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::gameplay {

// 파생 스탯을 결정하는 1차 스탯. 성장(레벨업)·장비로 변한다.
struct BaseAttributes {
    int32_t level     = 1;
    int32_t strength  = 10;   // 공격력·물리
    int32_t agility   = 10;   // 크리·공속·회피
    int32_t intellect = 10;   // MP·마법
    int32_t vitality  = 10;   // HP·방어
};

// 장비/버프가 더하는 수정자. flat 합 후 percent(1.0=+100%) 적용.
enum class StatField : uint8_t {
    MaxHp, MaxMp, Attack, Defense, CritChance, AttackSpeed, MoveSpeed, Count
};

struct StatModifier {
    StatField field = StatField::Attack;
    float     flat = 0.0f;      // 절대 가산
    float     percent = 0.0f;   // 비율 가산(0.2 = +20%)
    uint32_t  sourceId = 0;     // 출처(장비/버프 식별 — 제거용)
};

// 파생 스탯(StatSystem::ComputeDerived 가 채운다). 읽기 전용 취급.
struct DerivedStats {
    int32_t maxHp = 0;
    int32_t maxMp = 0;
    int32_t attack = 0;
    int32_t defense = 0;
    float   critChance = 0.0f;    // 0..1
    float   attackSpeed = 1.0f;   // 초당 공격 배율
    float   moveSpeed = 1.0f;     // 이동 배율
};

// 스탯 컴포넌트 — 기본·수정자·파생·현재 자원(HP/MP).
struct Stats {
    MYE_COMPONENT(Stats);
    BaseAttributes            base;
    std::vector<StatModifier> modifiers;   // 장비·버프 수정자(장착/해제 시 갱신)
    DerivedStats              derived;      // 계산 결과(ComputeDerived)
    int32_t                   hp = 0;       // 현재 HP(≤ derived.maxHp)
    int32_t                   mp = 0;       // 현재 MP(≤ derived.maxMp)
    bool                      dirty = true; // 재계산 필요
};

} // namespace mye::gameplay
