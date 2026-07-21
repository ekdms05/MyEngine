// mye/phys/Collision.h — 충돌·간이 물리 스텁 (docs/03 §8) [M3+ 구현]
//
// M2-A: 시그니처 주석만(구현 금지). 자체 경량 2D(키네마틱 이동 + 벽 슬라이드 + 트리거),
// 층(FloorLevel) 인지 충돌. 본격 물리엔진은 IPhysicsWorld 플러그인으로 교체(docs/03 §8).
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>

namespace mye::phys {

// struct Collider2D {           // 셰이프 + 오프셋 + isTrigger + 레이어/floor 마스크
//     Shape2D  shape; Vec2 offset; bool isTrigger;
//     uint32_t layerMask; uint8_t floorMask;
// };
// struct KinematicBody2D { Vec2 velocity; bool snapToGround; };
//
// struct RayHit { ecs::Entity entity; Vec2 point, normal; float distance; };
//
// class PhysicsWorld2D {         // IPhysicsWorld 기본 내장 구현(플러그인 교체 가능)
// public:
//     std::optional<RayHit> raycast(Vec2 from, Vec2 dir, float maxDist,
//                                   uint32_t layerMask, int8_t floorLevel) const;
//     uint32_t overlapArea(const Shape2D&, Vec2 pos, int8_t floorLevel,
//                          std::span<ecs::Entity> out) const;
//     // 트리거 Enter/Stay/Exit는 World::events()(월드-로컬 버스)로 발행.
//     // 실행: PhaseFixedUpdate 내 고정 스텝. 쿼리는 모두 floorLevel 필터 인자를 받는다
//     //   (다리 위/아래: 같은 XY라도 floorLevel이 다르면 충돌하지 않음).
// };

} // namespace mye::phys
