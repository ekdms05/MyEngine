// mye/phys/PhysicsWorld2D.h — 자체 경량 2D 물리 월드(IPhysicsWorld 기본 구현) (docs/03 §8)
//
// IPhysicsWorld 인터페이스 뒤에 자체 구현을 둔다(플러그인 교체 여지, docs/03 §확장 6).
// - 브로드페이즈: SpatialHash. 내로우페이즈: AABB/원 겹침 + MTV.
// - KinematicBody2D move-and-slide: 벽 슬라이드, 엔티티/타일 충돌 해결.
// - 트리거 Enter/Exit: 프레임 간 겹침 집합 diff → 월드 EventBus 발행.
// - floorLevel 필터: 다리 위/아래 같은 XY라도 층이 다르면 비충돌.
//
// 타일 충돌은 ITileCollision(선택 소스) 인터페이스를 통해 소비한다 — 타일맵 헤더 계약이
// 완성되기 전까지 phys가 tilemap 구현에 결합되지 않도록 얇은 질의 인터페이스만 의존한다.
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

// ---------------------------------------------------------------------------
// 타일 충돌 소스(선택) — 타일맵 청크 베이크 데이터의 얇은 질의 표면.
// 실제 구현은 tilemap 모듈(M2-B 타일맵 담당)이 제공하고 여기 주입한다. 없으면 타일 충돌 생략.
// ---------------------------------------------------------------------------
class ITileCollision {
public:
    virtual ~ITileCollision() = default;

    // 주어진 층(floorLevel)에서 월드 셰이프가 솔리드 타일과 겹치면, 빠져나오는 MTV 반환.
    // 겹치지 않으면 nullopt. (경사·계단 스냅은 SampleGroundHeight 참조.)
    virtual std::optional<Vec2> ResolveSolid(const Shape2D& shape, Vec2 pos,
                                             int8_t floorLevel) const = 0;

    // snapToGround용: 해당 XY·층의 지면 높이(경사 보간). 지면 없으면 nullopt.
    virtual std::optional<float> SampleGroundHeight(Vec2 worldXY, int8_t level) const {
        (void)worldXY; (void)level; return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// IPhysicsWorld — 플러그인 교체 경계(docs/03 §확장 6).
// ---------------------------------------------------------------------------
class IPhysicsWorld {
public:
    virtual ~IPhysicsWorld() = default;

    // 고정 스텝 1회 실행: 브로드페이즈 리빌드 → move-and-slide → 트리거 diff·발행.
    virtual void Step(ecs::World& world, EventBus* worldBus, float dt) = 0;

    // 레이캐스트(선택 최소): from에서 dir 방향 maxDist까지, layerMask·floorLevel 필터.
    virtual std::optional<RayHit> Raycast(ecs::World& world, Vec2 from, Vec2 dir,
                                          float maxDist, uint32_t layerMask,
                                          int8_t floorLevel) const = 0;

    // 영역 겹침 질의: 셰이프와 겹치는 엔티티를 out에 채우고 개수 반환(floorLevel 필터).
    virtual uint32_t OverlapArea(ecs::World& world, const Shape2D& shape, Vec2 pos,
                                 int8_t floorLevel, std::span<Entity> out) const = 0;
};

// ---------------------------------------------------------------------------
// PhysicsWorld2D — 기본 내장 구현.
// ---------------------------------------------------------------------------
class PhysicsWorld2D final : public IPhysicsWorld {
public:
    explicit PhysicsWorld2D(float cellSize = 1.0f);

    void SetCellSize(float cellSize) { m_broadphase.SetCellSize(cellSize); }
    void SetTileCollision(const ITileCollision* tiles) { m_tiles = tiles; }

    void Step(ecs::World& world, EventBus* worldBus, float dt) override;

    std::optional<RayHit> Raycast(ecs::World& world, Vec2 from, Vec2 dir, float maxDist,
                                  uint32_t layerMask, int8_t floorLevel) const override;

    uint32_t OverlapArea(ecs::World& world, const Shape2D& shape, Vec2 pos,
                         int8_t floorLevel, std::span<Entity> out) const override;

private:
    // 한 콜라이더의 월드 표현(추출 캐시).
    struct ColliderInst {
        Entity   entity = Entity::Null();
        Shape2D  shape;
        Vec2     pos{0, 0};        // 월드 중심(WorldTransform 위치 + offset)
        bool     isTrigger = false;
        uint32_t layerMask = 0;
        uint32_t collidesWith = 0;
        uint8_t  floorMask = 0x01;
        int8_t   floorLevel = 0;   // FloorLevel 있으면 그 값, 없으면 0
        uint32_t triggerId = 0;
        bool     kinematic = false;
    };

    void GatherColliders(ecs::World& world, std::vector<ColliderInst>& out) const;
    void RebuildBroadphase(const std::vector<ColliderInst>& insts);

    // 두 인스턴스가 층·레이어 마스크상 상호작용 가능한지.
    static bool CanInteract(const ColliderInst& a, const ColliderInst& b);

    // 트리거 겹침 쌍 키(정렬된 Entity 쌍 + triggerId).
    static uint64_t TriggerPairKey(Entity trigger, Entity other);

    SpatialHash              m_broadphase;
    const ITileCollision*    m_tiles = nullptr;   // 비소유
    // 직전 스텝의 트리거 겹침 집합(Enter/Exit diff용). 값 = (trigger, other, triggerId).
    struct TriggerPair { Entity trigger, other; uint32_t triggerId; };
    // 트리거 쌍 전순서(사전식 (trigger.Packed(), other.Packed())). 정렬·diff 병합 기준(충돌 없음).
    static bool TriggerPairLess(const TriggerPair& a, const TriggerPair& b);
    std::vector<TriggerPair> m_prevTriggers;       // 사전식 오름차순 정렬 유지
};

} // namespace mye::phys
