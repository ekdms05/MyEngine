// mye/phys/PhysicsWorld2D.cpp — 자체 경량 2D 물리 월드 구현 (docs/03 §8)
#include "mye/phys/PhysicsWorld2D.h"

#include "mye/core/Events.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"   // WorldTransform·LocalTransform

#include <algorithm>
#include <cmath>

namespace mye::phys {

using scene::LocalTransform;
using scene::WorldTransform;

namespace {

// 엔티티 월드 XY(WorldTransform 있으면 행렬 이동, 없으면 LocalTransform, 없으면 원점).
Vec2 EntityWorldXY(ecs::World& w, Entity e) {
    if (auto* wt = w.TryGet<WorldTransform>(e)) {
        return Vec2{wt->matrix.m[3][0], wt->matrix.m[3][1]};
    }
    if (auto* lt = w.TryGet<LocalTransform>(e)) {
        return Vec2{lt->position.x, lt->position.y};
    }
    return Vec2{0, 0};
}

// 셰이프의 월드 AABB(min/max).
void ShapeAabb(const Shape2D& s, Vec2 pos, Vec2& outMin, Vec2& outMax) {
    Vec2 h = (s.kind == ShapeKind::Circle) ? Vec2{s.Radius(), s.Radius()} : s.half;
    outMin = pos - h;
    outMax = pos + h;
}

} // namespace

PhysicsWorld2D::PhysicsWorld2D(float cellSize) : m_broadphase(cellSize) {}

bool PhysicsWorld2D::CanInteract(const ColliderInst& a, const ColliderInst& b) {
    // 층 필터: floorMask 교차 없으면 비충돌(다리 위/아래 분리).
    if (!FloorsOverlap(a.floorMask, b.floorMask)) return false;
    // 레이어 필터: 서로 상대 레이어에 관심 있어야(양방향 중 하나라도).
    const bool aWantsB = (a.collidesWith & b.layerMask) != 0;
    const bool bWantsA = (b.collidesWith & a.layerMask) != 0;
    return aWantsB || bWantsA;
}

uint64_t PhysicsWorld2D::TriggerPairKey(Entity trigger, Entity other) {
    // trigger·other 순서 보존(트리거가 어느 쪽인지 의미 있음).
    return (static_cast<uint64_t>(trigger.index) << 32) ^ static_cast<uint64_t>(other.index)
           ^ (static_cast<uint64_t>(trigger.generation) << 16);
}

void PhysicsWorld2D::GatherColliders(ecs::World& world, std::vector<ColliderInst>& out) const {
    world.Query<Collider2D>().Each([&](Entity e, Collider2D& col) {
        ColliderInst inst;
        inst.entity = e;
        inst.shape = col.shape;
        inst.pos = EntityWorldXY(world, e) + col.offset;
        inst.isTrigger = col.isTrigger;
        inst.layerMask = col.layerMask;
        inst.collidesWith = col.collidesWith;
        inst.triggerId = col.triggerId;

        int8_t level = 0;
        uint8_t floorMask = col.floorMask;
        if (auto* fl = world.TryGet<FloorLevel>(e)) {
            level = fl->level;
            floorMask = FloorBit(level);   // FloorLevel 있으면 단일 층 존재로 강제
        }
        inst.floorLevel = level;
        inst.floorMask = floorMask;
        inst.kinematic = world.Has<KinematicBody2D>(e);
        out.push_back(inst);
    });
}

void PhysicsWorld2D::RebuildBroadphase(const std::vector<ColliderInst>& insts) {
    m_broadphase.Clear();
    m_broadphase.Reserve(insts.size());
    for (uint32_t i = 0; i < insts.size(); ++i) {
        BroadphaseItem item;
        item.entity = insts[i].entity;
        item.userIndex = i;
        ShapeAabb(insts[i].shape, insts[i].pos, item.min, item.max);
        m_broadphase.Insert(item);
    }
}

void PhysicsWorld2D::Step(ecs::World& world, EventBus* worldBus, float dt) {
    // 1) 콜라이더 추출.
    std::vector<ColliderInst> insts;
    GatherColliders(world, insts);

    // entity → inst 인덱스(이동 반영용).
    // 2) 키네마틱 이동: move-and-slide (솔리드 콜라이더·타일 대상 슬라이드).
    for (uint32_t ki = 0; ki < insts.size(); ++ki) {
        ColliderInst& body = insts[ki];
        if (!body.kinematic || body.isTrigger) continue;
        auto* kb = world.TryGet<KinematicBody2D>(body.entity);
        if (!kb) continue;

        Vec2 desired = kb->velocity * dt;
        Vec2 pos = body.pos;
        Vec2 remaining = desired;
        bool hitWall = false;

        for (int iter = 0; iter < kb->maxSlideIters; ++iter) {
            if (Abs(remaining.x) < 1e-7f && Abs(remaining.y) < 1e-7f) break;
            Vec2 tryPos = pos + remaining;

            // 이 후보 위치에서 솔리드와의 최대 침투를 찾아 밀어냄.
            Vec2 correction{0, 0};
            bool collided = false;

            // 엔티티 솔리드 대 슬라이드.
            for (uint32_t j = 0; j < insts.size(); ++j) {
                if (j == ki) continue;
                const ColliderInst& o = insts[j];
                if (o.isTrigger) continue;            // 트리거는 물리 응답 없음
                if (!CanInteract(body, o)) continue;
                auto mtv = ResolveMTV(o.shape, o.pos, body.shape, tryPos);
                if (mtv) {
                    tryPos += *mtv;                   // body를 o에서 분리
                    collided = true;
                    hitWall = true;
                }
            }

            // 타일 솔리드 대 슬라이드(선택 소스).
            if (m_tiles) {
                if (auto mtv = m_tiles->ResolveSolid(body.shape, tryPos, body.floorLevel)) {
                    tryPos += *mtv;
                    collided = true;
                    hitWall = true;
                }
            }

            pos = tryPos;
            if (!collided) break;
            // 슬라이드: 남은 이동에서 이미 소비된 성분 제거(간이 — 다음 반복이 잔여 해결).
            remaining = Vec2{0, 0};
        }

        kb->lastMove = pos - body.pos;
        kb->hitWall = hitWall;
        body.pos = pos;

        // 이동 결과를 Transform에 반영(LocalTransform XY만; Z·회전 불변).
        // 주: 부모 계층이 있으면 로컬≠월드지만 M2-B 키네마틱은 루트 이동을 가정한다.
        if (auto* lt = world.TryGet<LocalTransform>(body.entity)) {
            lt->position.x = pos.x;
            lt->position.y = pos.y;
            lt->dirty = true;
        }
        // WorldTransform도 즉시 반영(다음 트리거 diff가 최신 위치 사용).
        if (auto* wt = world.TryGet<WorldTransform>(body.entity)) {
            wt->matrix.m[3][0] = pos.x;
            wt->matrix.m[3][1] = pos.y;
        }
    }

    // 3) 브로드페이즈 리빌드(이동 반영된 위치 기준).
    RebuildBroadphase(insts);

    // 4) 트리거 겹침 집합 산출 → Enter/Exit diff.
    std::vector<TriggerPair> current;
    m_broadphase.QueryPairs([&](uint32_t i, uint32_t j) {
        const ColliderInst& A = insts[i];
        const ColliderInst& B = insts[j];
        // 트리거 이벤트는 (isTrigger 콜라이더) vs (임의 콜라이더) 겹침에서 발생.
        const bool aT = A.isTrigger, bT = B.isTrigger;
        if (aT == bT) return;                        // 둘 다 트리거거나 둘 다 아님 → 스킵
        if (!CanInteract(A, B)) return;              // 층·레이어 필터
        if (!Overlap(A.shape, A.pos, B.shape, B.pos)) return;

        const ColliderInst& trig = aT ? A : B;
        const ColliderInst& oth = aT ? B : A;
        current.push_back(TriggerPair{trig.entity, oth.entity, trig.triggerId});
    });

    // 정렬(diff·결정성).
    auto cmp = [](const TriggerPair& a, const TriggerPair& b) {
        return TriggerPairKey(a.trigger, a.other) < TriggerPairKey(b.trigger, b.other);
    };
    std::sort(current.begin(), current.end(), cmp);
    current.erase(std::unique(current.begin(), current.end(),
                  [](const TriggerPair& a, const TriggerPair& b) {
                      return a.trigger == b.trigger && a.other == b.other;
                  }), current.end());

    // diff: current \ prev = Enter, prev \ current = Exit. (둘 다 정렬됨)
    if (worldBus) {
        size_t p = 0, c = 0;
        while (p < m_prevTriggers.size() && c < current.size()) {
            const uint64_t kp = TriggerPairKey(m_prevTriggers[p].trigger, m_prevTriggers[p].other);
            const uint64_t kc = TriggerPairKey(current[c].trigger, current[c].other);
            if (kp == kc) { ++p; ++c; }              // 유지(Stay) — 이벤트 없음
            else if (kc < kp) {                      // 새로 진입
                worldBus->Publish(TriggerEnterEvent{current[c].trigger, current[c].other, current[c].triggerId});
                ++c;
            } else {                                 // 이탈
                worldBus->Publish(TriggerExitEvent{m_prevTriggers[p].trigger, m_prevTriggers[p].other, m_prevTriggers[p].triggerId});
                ++p;
            }
        }
        for (; c < current.size(); ++c)
            worldBus->Publish(TriggerEnterEvent{current[c].trigger, current[c].other, current[c].triggerId});
        for (; p < m_prevTriggers.size(); ++p)
            worldBus->Publish(TriggerExitEvent{m_prevTriggers[p].trigger, m_prevTriggers[p].other, m_prevTriggers[p].triggerId});
    }

    m_prevTriggers = std::move(current);
}

std::optional<RayHit> PhysicsWorld2D::Raycast(ecs::World& world, Vec2 from, Vec2 dir,
                                              float maxDist, uint32_t layerMask,
                                              int8_t floorLevel) const {
    // 최소 구현: 브로드페이즈 없이 전 콜라이더 슬래브/원 테스트, 최근접 히트 반환.
    Vec2 d = dir.Normalized();
    std::optional<RayHit> best;
    const uint8_t fbit = FloorBit(floorLevel);

    world.Query<Collider2D>().Each([&](Entity e, Collider2D& col) {
        uint8_t fmask = col.floorMask;
        if (auto* fl = world.TryGet<FloorLevel>(e)) fmask = FloorBit(fl->level);
        if (!FloorsOverlap(fbit, fmask)) return;
        if ((layerMask & col.layerMask) == 0) return;

        Vec2 c = EntityWorldXY(world, e) + col.offset;
        float tHit = 0.0f; Vec2 normal{0, 0};

        if (col.shape.kind == ShapeKind::Circle) {
            // ray-circle.
            Vec2 m = from - c;
            float b = Vec2::Dot(m, d);
            float cc = Vec2::Dot(m, m) - col.shape.Radius() * col.shape.Radius();
            if (cc > 0.0f && b > 0.0f) return;
            float disc = b * b - cc;
            if (disc < 0.0f) return;
            tHit = -b - std::sqrt(disc);
            if (tHit < 0.0f) tHit = 0.0f;
            if (tHit > maxDist) return;
            Vec2 pt = from + d * tHit;
            normal = (pt - c).Normalized();
            if (!best || tHit < best->distance) best = RayHit{e, pt, normal, tHit};
        } else {
            // ray-AABB 슬래브.
            Vec2 mn = c - col.shape.half, mx = c + col.shape.half;
            float tmin = 0.0f, tmax = maxDist;
            int hitAxis = -1; float hitSign = 0.0f;
            for (int a = 0; a < 2; ++a) {
                float o = (a == 0) ? from.x : from.y;
                float dd = (a == 0) ? d.x : d.y;
                float lo = (a == 0) ? mn.x : mn.y;
                float hi = (a == 0) ? mx.x : mx.y;
                if (Abs(dd) < 1e-8f) { if (o < lo || o > hi) return; continue; }
                float inv = 1.0f / dd;
                float t1 = (lo - o) * inv, t2 = (hi - o) * inv;
                float sign = -1.0f;
                if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
                if (t1 > tmin) { tmin = t1; hitAxis = a; hitSign = sign; }
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return;
            }
            tHit = tmin;
            if (tHit > maxDist) return;
            Vec2 pt = from + d * tHit;
            normal = (hitAxis == 0) ? Vec2{hitSign, 0} : (hitAxis == 1) ? Vec2{0, hitSign} : Vec2{0, 0};
            if (!best || tHit < best->distance) best = RayHit{e, pt, normal, tHit};
        }
    });
    return best;
}

uint32_t PhysicsWorld2D::OverlapArea(ecs::World& world, const Shape2D& shape, Vec2 pos,
                                     int8_t floorLevel, std::span<Entity> out) const {
    uint32_t count = 0;
    const uint8_t fbit = FloorBit(floorLevel);
    world.Query<Collider2D>().Each([&](Entity e, Collider2D& col) {
        uint8_t fmask = col.floorMask;
        if (auto* fl = world.TryGet<FloorLevel>(e)) fmask = FloorBit(fl->level);
        if (!FloorsOverlap(fbit, fmask)) return;
        Vec2 c = EntityWorldXY(world, e) + col.offset;
        if (!Overlap(shape, pos, col.shape, c)) return;
        if (count < out.size()) out[count] = e;
        ++count;
    });
    return count;
}

} // namespace mye::phys
