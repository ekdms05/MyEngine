// mye/scene/Transform.h — Transform 계층 컴포넌트 + 월드행렬 계산 시스템 (docs/03 §3)
//
// 전통적 씬 그래프(노드 객체 트리) 대신 계층을 컴포넌트로 표현한다:
//   - LocalTransform: 부모 기준 위치·회전·스케일. 모든 엔티티가 3D Transform 하나만 가진다.
//     2D 엔티티도 3D 좌표에 존재(좌표계·PPU는 02 소유 규약 인용, PPU=48).
//   - Parent:  자식이 부모를 가리킨다(단방향 진실).
//   - Children: 부모가 자식 목록을 캐시(파생 데이터, Parent 변경 시 시스템이 동기화).
//   - WorldTransform: 시스템이 계산하는 캐시. 사용자는 쓰지 않는다.
// TransformSystem(PhasePostUpdate): dirty 전파 + 계층 깊이 순 순회로 WorldTransform 갱신.
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"

#include <vector>

namespace mye::ecs { class World; class CommandBuffer; }

namespace mye::scene {

using ecs::Entity;

// 부모 기준 로컬 변환(3D 단일 규약). 기본값 = 원점·항등 회전·단위 스케일.
struct LocalTransform {
    MYE_COMPONENT(LocalTransform);
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::Identity();
    Vec3 scale{1, 1, 1};
    bool dirty = true;   // TransformSystem 증분 갱신용(Parent/Local 변경 시 set).
};

// 시스템이 계산하는 월드 행렬 캐시(읽기 전용 취급).
struct WorldTransform {
    MYE_COMPONENT(WorldTransform);
    Mat4 matrix = Mat4::Identity();
};

// 자식 → 부모(단방향 진실). null 부모 = 루트.
struct Parent {
    MYE_COMPONENT(Parent);
    Entity parent = Entity::Null();
};

// 부모 → 자식 캐시(파생). Parent 변경 시 TransformSystem이 동기화. 에디터 트리·순회용.
// (SmallVector 미도입 — M2-A는 std::vector; docs의 SmallVector<Entity,4> 의도는 후속 최적화.)
struct Children {
    MYE_COMPONENT(Children);
    std::vector<Entity> list;
};

// ---- TransformSystem (PhasePostUpdate) ----
// dirty 플래그 전파 + 계층 깊이 기준 정렬 순회로 WorldTransform 갱신.
// reparent(부모 재지정)는 CommandBuffer 경유, 월드 위치 유지 옵션 제공.
// M2-A: 계약 + 기준 구현(단일 스레드 전체 재계산). 증분 dirty 전파 최적화는 후속.
void UpdateWorldTransforms(ecs::World& world);

// CommandBuffer::SetParent가 Flush 시 호출하는 reparent 헬퍼(Parent/Children 동기화 +
// keepWorld 시 LocalTransform 보정). M2-A: 선언만(구현 스텁은 후속 구현 에이전트).
void ApplyReparent(ecs::World& world, Entity child, Entity parent, bool keepWorld);

// 로컬 TRS로부터 로컬 행렬을 만든다(row-vector: Scale→Rotation→Translation).
Mat4 ComputeLocalMatrix(const LocalTransform& lt);

} // namespace mye::scene
