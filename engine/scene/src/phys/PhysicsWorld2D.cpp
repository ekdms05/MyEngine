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

// 트리거 쌍 전순서(사전식). XOR 키와 달리 서로 다른 쌍이 절대 같은 순서로 뭉치지 않아
// 정렬·Enter/Exit diff 병합 스캔이 안정적이다(키 충돌로 Enter/Exit가 뒤바뀌거나 유실되지 않음).
bool PhysicsWorld2D::TriggerPairLess(const TriggerPair& a, const TriggerPair& b) {
    const uint64_t at = a.trigger.Packed(), bt = b.trigger.Packed();
    if (at != bt) return at < bt;
    return a.other.Packed() < b.other.Packed();
}

Expected<std::vector<CollisionBody2D>, Error> GatherCollisionBodies2D(ecs::World& world) {
    std::vector<CollisionBody2D> out;
    world.Query<Collider2D>().Each([&](Entity e, Collider2D& col) {
        CollisionBody2D inst;
        inst.id = e.Packed();
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
    if (auto valid = ValidateCollisionBodies2D(out); !valid) return valid.GetError();
    return out;
}

Expected<void, Error> PhysicsWorld2D::RebuildBroadphase(const std::vector<ColliderInst>& insts) {
    m_broadphase.Clear();
    m_broadphase.Reserve(insts.size());
    for (uint32_t i = 0; i < insts.size(); ++i) {
        BroadphaseItem item;
        item.entity = Entity::FromPacked(insts[i].id);
        item.userIndex = i;
        ShapeAabb(insts[i].shape, insts[i].pos, item.min, item.max);
        if (auto inserted = m_broadphase.Insert(item); !inserted) return inserted.GetError();
    }
    return {};
}

Expected<void, Error> PhysicsWorld2D::Step(ecs::World& world, EventBus* worldBus, float dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return Error{"2D physics dt must be in (0,1]", 1};
    auto gathered = GatherCollisionBodies2D(world);
    if (!gathered) return gathered.GetError();
    auto insts = std::move(gathered.Value());

    struct PendingMove { Entity entity; MotionResult2D result; Vec2 origin; };
    std::vector<PendingMove> moves;
    for (auto& body : insts) {
        if (!body.kinematic || body.isTrigger) continue;
        const Entity entity = Entity::FromPacked(body.id);
        const auto* kb = world.TryGet<KinematicBody2D>(entity);
        const auto* parent = world.TryGet<scene::Parent>(entity);
        if (!world.Has<LocalTransform>(entity) || (parent && !parent->parent.IsNull()))
            return Error{"2D kinematic movement requires a root LocalTransform", 1};
        auto moved = MoveAndSlide2D(body, insts, kb->velocity, dt, kb->maxSlideIters);
        if (!moved) return moved.GetError();
        const Vec2 origin = moved.Value().position - world.TryGet<Collider2D>(entity)->offset;
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y))
            return Error{"2D kinematic origin overflow", 1};
        moves.push_back({entity, moved.Value(), origin});
        body.pos = moved.Value().position;
    }
    if (auto rebuilt = RebuildBroadphase(insts); !rebuilt) return rebuilt.GetError();

    // Solve in value storage first. One failed body must not commit the earlier bodies.
    for (const auto& move : moves) {
        auto* kb = world.TryGet<KinematicBody2D>(move.entity);
        kb->lastMove = move.result.lastMove;
        kb->hitWall = move.result.hitWall;
        auto* lt = world.TryGet<LocalTransform>(move.entity);
        lt->position.x = move.origin.x;
        lt->position.y = move.origin.y;
        lt->dirty = true;
        if (auto* wt = world.TryGet<WorldTransform>(move.entity)) {
            wt->matrix.m[3][0] = move.origin.x;
            wt->matrix.m[3][1] = move.origin.y;
        }
    }

    // 트리거 겹침 집합 산출 → Enter/Exit diff.
    std::vector<TriggerPair> current;
    m_broadphase.QueryPairs([&](uint32_t i, uint32_t j) {
        const ColliderInst& A = insts[i];
        const ColliderInst& B = insts[j];
        // 트리거 이벤트는 (isTrigger 콜라이더) vs (임의 콜라이더) 겹침에서 발생.
        const bool aT = A.isTrigger, bT = B.isTrigger;
        if (aT == bT) return;                        // 둘 다 트리거거나 둘 다 아님 → 스킵
        if (!CanInteract2D(A, B)) return;            // 층·레이어 필터
        if (!Overlap(A.shape, A.pos, B.shape, B.pos)) return;

        const ColliderInst& trig = aT ? A : B;
        const ColliderInst& oth = aT ? B : A;
        current.push_back(TriggerPair{Entity::FromPacked(trig.id), Entity::FromPacked(oth.id), trig.triggerId});
    });

    // 정렬(diff·결정성). 충돌 없는 사전식 전순서 사용.
    std::sort(current.begin(), current.end(), TriggerPairLess);
    current.erase(std::unique(current.begin(), current.end(),
                  [](const TriggerPair& a, const TriggerPair& b) {
                      return a.trigger == b.trigger && a.other == b.other;
                  }), current.end());

    // diff: current \ prev = Enter, prev \ current = Exit. (둘 다 정렬됨)
    if (worldBus) {
        size_t p = 0, c = 0;
        while (p < m_prevTriggers.size() && c < current.size()) {
            const bool prevLess = TriggerPairLess(m_prevTriggers[p], current[c]);
            const bool currLess = TriggerPairLess(current[c], m_prevTriggers[p]);
            if (!prevLess && !currLess) { ++p; ++c; } // 동일(Stay) — 이벤트 없음
            else if (currLess) {                      // 새로 진입(current가 앞)
                worldBus->Publish(TriggerEnterEvent{current[c].trigger, current[c].other, current[c].triggerId});
                ++c;
            } else {                                  // 이탈(prev가 앞)
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
    return {};
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
