// mye/mmo/Jobs.cpp — 3직업 구현 (Jobs.h 참조)
#include "mye/mmo/Jobs.h"

#include "mye/gameplay/StatSystem.h"   // ComputeDerived

#include <array>

namespace mye::mmo {

namespace {
using gameplay::BaseAttributes;
using gameplay::DamageType;

// 직업 테이블(레벨1 기본 + 레벨업 성장). 역할이 스탯으로 드러난다.
const std::array<JobDef, 3>& Jobs() {
    static const std::array<JobDef, 3> table = {{
        // 검사: STR·VIT 높음 → 물리 공격·HP·방어. 앞라인 딜탱.
        JobDef{ JobClass::Swordsman, "검사",
                BaseAttributes{ 1, 18, 10, 6, 16 },
                BaseAttributes{ 0, 3, 1, 0, 2 },
                DamageType::Physical, 1.0f, false },
        // 마법사: INT 높음 → MP·마법 공격(패시브 INT→attack). VIT 낮음 → 물몸. 강한 딜러.
        JobDef{ JobClass::Mage, "마법사",
                BaseAttributes{ 1, 6, 12, 20, 8 },
                BaseAttributes{ 0, 0, 1, 3, 1 },
                DamageType::Magic, 1.0f, false },
        // 버퍼: 균형 스탯·낮은 딜. 파티 버프로 존재감(상호의존 핵심).
        JobDef{ JobClass::Buffer, "버퍼",
                BaseAttributes{ 1, 10, 12, 12, 12 },
                BaseAttributes{ 0, 1, 2, 2, 2 },
                DamageType::Magic, 0.5f, true },
    }};
    return table;
}
} // namespace

const JobDef& GetJob(JobClass cls) {
    const auto& t = Jobs();
    const size_t i = static_cast<size_t>(cls);
    return t[i < t.size() ? i : 0];
}

std::string_view JobName(JobClass cls) { return GetJob(cls).name; }

gameplay::Stats MakeJobStats(JobClass cls, int level) {
    if (level < 1) level = 1;
    const JobDef& job = GetJob(cls);
    gameplay::Stats s;
    // 레벨 성장 반영.
    const int lv = level;
    s.base.level     = lv;
    s.base.strength  = job.base.strength  + job.growthPerLevel.strength  * (lv - 1);
    s.base.agility   = job.base.agility   + job.growthPerLevel.agility   * (lv - 1);
    s.base.intellect = job.base.intellect + job.growthPerLevel.intellect * (lv - 1);
    s.base.vitality  = job.base.vitality  + job.growthPerLevel.vitality  * (lv - 1);

    // 마법사: INT → 공격력 패시브(마법 딜러가 attack 파생을 INT 에서 얻게). 버퍼도 약한 마법.
    if (job.attackType == gameplay::DamageType::Magic) {
        const float scale = job.isSupport ? 1.2f : 2.5f;   // 마법사가 버퍼보다 강함
        gameplay::StatModifier m;
        m.field = gameplay::StatField::Attack;
        m.flat = static_cast<float>(s.base.intellect) * scale;
        m.percent = 0.0f;
        m.sourceId = kSpellPowerSource;
        s.modifiers.push_back(m);
    }

    s.dirty = true;
    gameplay::ComputeDerived(s, /*fillToMax=*/true);
    return s;
}

void ApplyPartyAttackBuff(gameplay::Stats& target, float percent) {
    RemovePartyAttackBuff(target);   // 중복 방지(재적용 = 갱신)
    gameplay::StatModifier m;
    m.field = gameplay::StatField::Attack;
    m.flat = 0.0f;
    m.percent = percent;
    m.sourceId = kPartyBuffSource;
    target.modifiers.push_back(m);
    target.dirty = true;
    gameplay::ComputeDerived(target, /*fillToMax=*/false);
}

void RemovePartyAttackBuff(gameplay::Stats& target) {
    auto& mods = target.modifiers;
    for (auto it = mods.begin(); it != mods.end();) {
        if (it->sourceId == kPartyBuffSource) it = mods.erase(it);
        else ++it;
    }
    target.dirty = true;
    gameplay::ComputeDerived(target, /*fillToMax=*/false);
}

} // namespace mye::mmo
