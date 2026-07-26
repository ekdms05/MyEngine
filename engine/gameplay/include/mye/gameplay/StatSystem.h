// mye/gameplay/StatSystem.h — 파생 스탯 계산 (docs/mmorpg/04, M8)
//
// 기본 스탯 + 수정자(장비/버프)에서 파생 스탯을 공식으로 계산한다. 순수 로직(GPU 무관, 결정론).
// 공식은 여기 한 곳에서 정의 — 밸런스 조정 지점.
#pragma once

namespace mye::ecs { class World; }

namespace mye::gameplay {

struct Stats;

// 한 Stats의 파생 스탯을 재계산하고 현재 HP/MP를 새 최대치로 클램프한다. dirty=false 로.
//   fillToMax=true 면 HP/MP를 최대치로 채운다(생성·부활 시).
void ComputeDerived(Stats& stats, bool fillToMax = false);

// World의 dirty Stats를 일괄 재계산(게임플레이 Update 페이즈).
void RunStatSystem(mye::ecs::World& world);

} // namespace mye::gameplay
