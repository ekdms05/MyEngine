// mye/gameplay/StatSystem.cpp — 파생 스탯 계산 구현 (StatSystem.h 참조)
#include "mye/gameplay/StatSystem.h"
#include "mye/gameplay/Stats.h"

#include "mye/ecs/World.h"
#include "mye/ecs/View.h"

#include <algorithm>
#include <array>

namespace mye::gameplay {

namespace {

// 필드별 수정자 합(flat, percent) 집계.
struct FieldAgg { float flat = 0.0f; float percent = 0.0f; };

std::array<FieldAgg, static_cast<size_t>(StatField::Count)>
Aggregate(const std::vector<StatModifier>& mods) {
    std::array<FieldAgg, static_cast<size_t>(StatField::Count)> agg{};
    for (const StatModifier& m : mods) {
        const size_t i = static_cast<size_t>(m.field);
        if (i >= agg.size()) continue;
        agg[i].flat += m.flat;
        agg[i].percent += m.percent;
    }
    return agg;
}

int32_t ApplyI(float base, const FieldAgg& a) {
    const float v = (base + a.flat) * (1.0f + a.percent);
    return static_cast<int32_t>(v < 0.0f ? 0.0f : v + 0.5f);   // 반올림, 음수 클램프
}
float ApplyF(float base, const FieldAgg& a) {
    const float v = (base + a.flat) * (1.0f + a.percent);
    return v < 0.0f ? 0.0f : v;
}

} // namespace

void ComputeDerived(Stats& s, bool fillToMax) {
    const auto& b = s.base;
    const auto agg = Aggregate(s.modifiers);
    auto A = [&](StatField f) -> const FieldAgg& { return agg[static_cast<size_t>(f)]; };

    // ---- 파생 공식(밸런스 조정 지점) ----
    DerivedStats& d = s.derived;
    d.maxHp       = ApplyI(static_cast<float>(b.vitality) * 12.0f + static_cast<float>(b.level) * 25.0f, A(StatField::MaxHp));
    d.maxMp       = ApplyI(static_cast<float>(b.intellect) * 8.0f + static_cast<float>(b.level) * 12.0f, A(StatField::MaxMp));
    d.attack      = ApplyI(static_cast<float>(b.strength) * 2.0f + static_cast<float>(b.level), A(StatField::Attack));
    d.defense     = ApplyI(static_cast<float>(b.vitality) + static_cast<float>(b.agility) * 0.5f, A(StatField::Defense));
    d.critChance  = std::clamp(ApplyF(static_cast<float>(b.agility) * 0.004f, A(StatField::CritChance)), 0.0f, 1.0f);
    d.attackSpeed = ApplyF(1.0f, A(StatField::AttackSpeed));
    d.moveSpeed   = ApplyF(1.0f, A(StatField::MoveSpeed));

    if (fillToMax) {
        s.hp = d.maxHp;
        s.mp = d.maxMp;
    } else {
        s.hp = std::clamp(s.hp, 0, d.maxHp);
        s.mp = std::clamp(s.mp, 0, d.maxMp);
    }
    s.dirty = false;
}

void RunStatSystem(mye::ecs::World& world) {
    world.Query<Stats>().Each([](mye::ecs::Entity, Stats& s) {
        if (s.dirty) ComputeDerived(s);
    });
}

} // namespace mye::gameplay
