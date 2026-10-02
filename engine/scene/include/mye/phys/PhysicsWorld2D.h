// mye/phys/PhysicsWorld2D.h — ECS adapter for shared value-only 2D motion.
//
// - 브로드페이즈: SpatialHash. 내로우페이즈: AABB/원 겹침 + MTV.
// - KinematicBody2D move-and-slide: 벽 슬라이드, 엔티티/타일 충돌 해결.
// - 트리거 Enter/Exit: 프레임 간 겹침 집합 diff → 월드 EventBus 발행.
// - floorLevel 필터: 다리 위/아래 같은 XY라도 층이 다르면 비충돌.
//
// 타일 충돌은 기존 ITileCollision 소스를 비소유로 참조한다.
#pragma once

#include "mye/phys/Collision.h"
#include "mye/phys/SpatialHash.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mye { class EventBus; }
namespace mye::ecs { class World; }

namespace mye::phys {

class PhysicsWorld2D {
public:
    explicit PhysicsWorld2D(float cellSize = 1.0f);

    void SetCellSize(float cellSize) { m_broadphase.SetCellSize(cellSize); }
    void SetTileCollision(const ITileCollision* tiles) { m_tiles = tiles; }

    // Failure leaves transforms, body results and the trigger history unchanged.
    Expected<void, Error> Step(ecs::World& world, EventBus* worldBus, float dt);

    std::optional<RayHit> Raycast(ecs::World& world, Vec2 from, Vec2 dir, float maxDist,
                                  uint32_t layerMask, int8_t floorLevel) const;

    uint32_t OverlapArea(ecs::World& world, const Shape2D& shape, Vec2 pos,
                         int8_t floorLevel, std::span<Entity> out) const;

private:
    // 한 콜라이더의 월드 표현(추출 캐시).
    using ColliderInst = CollisionBody2D;

    void GatherColliders(ecs::World& world, std::vector<ColliderInst>& out) const;
    Expected<void, Error> RebuildBroadphase(const std::vector<ColliderInst>& insts);

    SpatialHash              m_broadphase;
    const ITileCollision*    m_tiles = nullptr;   // 비소유
    // 직전 스텝의 트리거 겹침 집합(Enter/Exit diff용). 값 = (trigger, other, triggerId).
    struct TriggerPair { Entity trigger, other; uint32_t triggerId; };
    // 트리거 쌍 전순서(사전식 (trigger.Packed(), other.Packed())). 정렬·diff 병합 기준(충돌 없음).
    static bool TriggerPairLess(const TriggerPair& a, const TriggerPair& b);
    std::vector<TriggerPair> m_prevTriggers;       // 사전식 오름차순 정렬 유지
};

} // namespace mye::phys
