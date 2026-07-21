// mye/scene/Transform.cpp — Transform 계층 시스템 구현 (docs/03 §3)
//
// M2-A 기준 구현: 부모 → 자식(계층 깊이) 순으로 WorldTransform을 재계산한다. dirty
// 전파는 "부모가 dirty면 자식도 dirty"를 깊이 순회로 전달한다. 정렬 캐시·부분 갱신 등
// 최적화는 후속 몫. 계약(함수 시그니처·컴포넌트 규약)은 동결.
//
// 행렬 규약(docs/03·02 인용): row-vector(v*M), TRS = Scale→Rotation→Translation,
// world = local * parentWorld (자식 로컬이 먼저 적용된 뒤 부모 월드로 승격).
#include "mye/scene/Transform.h"

#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mye::scene {

Mat4 ComputeLocalMatrix(const LocalTransform& lt) {
    return Mat4::TRS(lt.position, lt.rotation, lt.scale);
}

namespace {

// 부모 체인을 따라 로컬 행렬을 합성해 월드 행렬을 즉시 계산(캐시 WorldTransform에 비의존).
// reparent 순간엔 캐시가 stale일 수 있어, keepWorld 보정은 로컬 체인으로 직접 산출한다.
Mat4 ComputeChainWorld(ecs::World& world, Entity e) {
    LocalTransform* lt = world.TryGet<LocalTransform>(e);
    Mat4 local = lt ? ComputeLocalMatrix(*lt) : Mat4::Identity();
    for (uint32_t guard = 0; guard < 4096; ++guard) {
        Parent* p = world.TryGet<Parent>(e);
        if (!p || p->parent.IsNull() || !world.Valid(p->parent)) break;
        e = p->parent;
        LocalTransform* plt = world.TryGet<LocalTransform>(e);
        Mat4 pl = plt ? ComputeLocalMatrix(*plt) : Mat4::Identity();
        local = local * pl;   // 자식 먼저(row-vector).
    }
    return local;
}

// 월드/로컬 행렬(row-vector, 마지막 행이 translation)에서 TRS를 분해해 LocalTransform에 기록.
// 셰어(전단)·비균등 스케일 편차는 근사(회전은 정규직교화). M2-A keepWorld 보정용 기준 구현.
void DecomposeInto(LocalTransform& out, const Mat4& m) {
    // translation = 마지막 행.
    out.position = Vec3{m.m[3][0], m.m[3][1], m.m[3][2]};

    // 각 기저 행 벡터 길이 = 스케일.
    Vec3 bx{m.m[0][0], m.m[0][1], m.m[0][2]};
    Vec3 by{m.m[1][0], m.m[1][1], m.m[1][2]};
    Vec3 bz{m.m[2][0], m.m[2][1], m.m[2][2]};
    const float sx = bx.Length();
    const float sy = by.Length();
    const float sz = bz.Length();
    out.scale = Vec3{sx, sy, sz};

    // 회전 = 정규화된 기저로 구성한 회전 행렬 → 쿼터니언.
    if (sx > 1e-8f) bx = bx / sx;
    if (sy > 1e-8f) by = by / sy;
    if (sz > 1e-8f) bz = bz / sz;

    // 회전 행렬(row-major, row-vector) → 쿼터니언(표준 trace 방식).
    const float r00 = bx.x, r01 = bx.y, r02 = bx.z;
    const float r10 = by.x, r11 = by.y, r12 = by.z;
    const float r20 = bz.x, r21 = bz.y, r22 = bz.z;
    const float trace = r00 + r11 + r22;
    Quat q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r12 - r21) / s;
        q.y = (r20 - r02) / s;
        q.z = (r01 - r10) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q.w = (r12 - r21) / s;
        q.x = 0.25f * s;
        q.y = (r10 + r01) / s;
        q.z = (r20 + r02) / s;
    } else if (r11 > r22) {
        float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q.w = (r20 - r02) / s;
        q.x = (r10 + r01) / s;
        q.y = 0.25f * s;
        q.z = (r21 + r12) / s;
    } else {
        float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q.w = (r01 - r10) / s;
        q.x = (r20 + r02) / s;
        q.y = (r21 + r12) / s;
        q.z = 0.25f * s;
    }
    out.rotation = q.Normalized();
}

// 엔티티의 계층 깊이(루트=0). 부모 체인을 따라 계산. 순환(비정상 데이터)은 상한으로 방어.
uint32_t HierarchyDepth(ecs::World& world, Entity e) {
    uint32_t depth = 0;
    Entity cur = e;
    for (uint32_t guard = 0; guard < 4096; ++guard) {
        Parent* p = world.TryGet<Parent>(cur);
        if (!p || p->parent.IsNull() || !world.Valid(p->parent)) break;
        cur = p->parent;
        ++depth;
    }
    return depth;
}

} // namespace

void UpdateWorldTransforms(ecs::World& world) {
    // 1) 갱신 대상 수집: LocalTransform + WorldTransform 보유 엔티티.
    struct Node { Entity e; uint32_t depth; };
    std::vector<Node> nodes;
    world.Query<LocalTransform, WorldTransform>().Each(
        [&](Entity e, LocalTransform&, WorldTransform&) {
            nodes.push_back(Node{e, HierarchyDepth(world, e)});
        });

    // 2) 깊이 오름차순 정렬 → 부모가 항상 자식보다 먼저 처리된다(안정 정렬로 순회 순서 보존).
    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const Node& a, const Node& b) { return a.depth < b.depth; });

    // 3) 깊이 순 단일 패스로 월드 행렬 계산 + dirty 하향 전파.
    for (const Node& n : nodes) {
        LocalTransform* lt = world.TryGet<LocalTransform>(n.e);
        WorldTransform* wt = world.TryGet<WorldTransform>(n.e);
        if (!lt || !wt) continue;

        bool parentDirty = false;
        Mat4 parentWorld = Mat4::Identity();
        if (Parent* p = world.TryGet<Parent>(n.e);
            p && !p->parent.IsNull() && world.Valid(p->parent)) {
            if (WorldTransform* pwt = world.TryGet<WorldTransform>(p->parent))
                parentWorld = pwt->matrix;
            // 부모가 이번 프레임 dirty였다면(부모 lt.dirty는 이미 false로 클리어됐을 수 있어
            // 별도 표식 없이) 자식도 재계산해야 하므로 dirty를 전파한다.
            if (LocalTransform* plt = world.TryGet<LocalTransform>(p->parent))
                parentDirty = plt->dirty;
        }

        if (lt->dirty || parentDirty) {
            wt->matrix = ComputeLocalMatrix(*lt) * parentWorld;
            // 부모 dirty가 이 자식을 다시 dirty로 만들도록 유지하되, 자기 로컬 dirty는 소거.
            // (같은 프레임 내 더 깊은 자식이 이 노드를 부모로 볼 때 재계산되도록 dirty 승계)
            lt->dirty = lt->dirty || parentDirty;
        }
    }

    // 4) 모든 노드 계산 완료 후 dirty 일괄 클리어(다음 프레임 증분 준비).
    for (const Node& n : nodes) {
        if (LocalTransform* lt = world.TryGet<LocalTransform>(n.e)) lt->dirty = false;
    }
}

void ApplyReparent(ecs::World& world, Entity child, Entity parent, bool keepWorld) {
    if (!world.Valid(child)) return;
    if (!parent.IsNull() && !world.Valid(parent)) parent = Entity::Null();

    Parent* pc = world.TryGet<Parent>(child);
    const Entity oldParent = pc ? pc->parent : Entity::Null();
    if (oldParent == parent) return;   // 변화 없음.

    // keepWorld: 재부모 후에도 월드 위치가 유지되도록 child의 LocalTransform을 보정한다.
    // newLocal = oldWorld * inverse(newParentWorld) (row-vector: local*parentWorld=world).
    if (keepWorld) {
        if (LocalTransform* lt = world.TryGet<LocalTransform>(child)) {
            const Mat4 oldWorld = ComputeChainWorld(world, child);
            Mat4 newParentWorld = Mat4::Identity();
            if (!parent.IsNull())
                newParentWorld = ComputeChainWorld(world, parent);
            const Mat4 newLocal = oldWorld * newParentWorld.Inverse();
            DecomposeInto(*lt, newLocal);
        }
    }

    // 이전 부모의 Children 캐시에서 child 제거.
    if (!oldParent.IsNull() && world.Valid(oldParent)) {
        if (Children* oc = world.TryGet<Children>(oldParent)) {
            auto& l = oc->list;
            l.erase(std::remove(l.begin(), l.end(), child), l.end());
        }
    }

    // Parent 단방향 진실 갱신.
    if (!pc) pc = &world.Add<Parent>(child);
    pc->parent = parent;

    // 새 부모의 Children 캐시에 child 추가(중복 방지).
    if (!parent.IsNull()) {
        Children* nc = world.TryGet<Children>(parent);
        if (!nc) nc = &world.Add<Children>(parent);
        if (std::find(nc->list.begin(), nc->list.end(), child) == nc->list.end())
            nc->list.push_back(child);
    }

    if (LocalTransform* lt = world.TryGet<LocalTransform>(child)) lt->dirty = true;
}

// CommandBuffer::SetParent(Flush 시)가 scene 계층 없이 reparent를 잇도록, 이 TU 로드 시점에
// ApplyReparent를 ecs CommandBuffer의 reparent 후크로 설치한다(정적 라이브러리 링크 보장).
namespace {
struct ReparentHookInstaller {
    ReparentHookInstaller() {
        ecs::CommandBuffer::SetReparentHook(&ApplyReparent);
    }
};
const ReparentHookInstaller g_reparentHookInstaller;
} // namespace

} // namespace mye::scene
