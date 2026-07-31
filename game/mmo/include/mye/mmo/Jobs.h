// mye/mmo/Jobs.h — 3직업(검사/마법사/버퍼) 정의 (게임 레이어)
//
// [게임 레이어 — 엔진 아님] "아기자기한 파티 사냥 MMO"의 첫 3직업. 각자 뚜렷한 역할:
//   - 검사(Swordsman): 물리 근접·높은 HP/방어 — 앞에서 버티며 때린다.
//   - 마법사(Mage): INT 기반 마법 딜러·낮은 HP — 뒤에서 강한 한 방(버퍼 버프의 최대 수혜자).
//   - 버퍼(Buffer): 파티 공격력 버프 — 스스로는 약하나 동료를 강하게(파티 상호의존의 핵심).
// engine/gameplay(Stats/Combat)를 사용한다. attack 파생이 STR 단일이라, 마법사는 INT→attack
// 패시브 수정자로 만들어 버퍼의 %공격 버프가 검사·마법사 모두에게 이득이 되게 한다.
#pragma once

#include "mye/gameplay/Stats.h"
#include "mye/gameplay/Combat.h"   // DamageType

#include <string>
#include <string_view>

namespace mye::mmo {

enum class JobClass : uint8_t { Swordsman, Mage, Buffer, Count };

// 마법사 스펠 공격력 패시브 수정자 식별(제거·중복 방지용 sourceId).
inline constexpr uint32_t kSpellPowerSource = 0xA1;
// 버퍼 파티 버프 식별.
inline constexpr uint32_t kPartyBuffSource  = 0xB2;

struct JobDef {
    JobClass    cls = JobClass::Swordsman;
    std::string name;                          // 한국어 직업명
    gameplay::BaseAttributes base;             // 레벨 1 기본 스탯
    gameplay::BaseAttributes growthPerLevel;   // 레벨업당 증가(level 필드는 무시)
    gameplay::DamageType     attackType = gameplay::DamageType::Physical;
    float                    skillPower = 1.0f;   // 기본 공격 배율
    bool                     isSupport = false;   // 버퍼
};

// 직업 정의 조회.
const JobDef& GetJob(JobClass cls);
std::string_view JobName(JobClass cls);

// 해당 직업·레벨의 캐릭터 Stats 생성(파생 계산 + HP/MP 최대치). 마법사는 INT→attack 패시브 포함.
gameplay::Stats MakeJobStats(JobClass cls, int level);

// 파티 공격 버프(버퍼가 동료에게). percent 만큼 공격력 증가. 재계산 포함.
void ApplyPartyAttackBuff(gameplay::Stats& target, float percent);
void RemovePartyAttackBuff(gameplay::Stats& target);

} // namespace mye::mmo
