// mye/phys/PhysicsSystem.h — 물리 스텝 시스템 등록 (docs/03 §8, §2)
//
// PhysicsWorld2D의 고정 스텝을 SystemScheduler의 FixedUpdate 페이즈에 등록한다.
// 물리 월드 인스턴스는 호출자가 소유(교체 가능) — 시스템은 참조만 캡처한다.
#pragma once

#include "mye/scene/SystemScheduler.h"

namespace mye::phys {

class PhysicsWorld2D;

// FixedUpdate 페이즈에 물리 스텝 시스템을 등록하고 SystemId 반환.
// world는 스케줄러의 World와 동일해야 하며, 스텝은 각 FixedUpdate 틱마다 fixedDt로 실행된다.
scene::SystemId RegisterPhysicsSystem(scene::SystemScheduler& scheduler, PhysicsWorld2D& physics);

} // namespace mye::phys
