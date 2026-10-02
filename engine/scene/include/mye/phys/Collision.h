// mye/phys/Collision.h — ECS collision components and world-local trigger events.
//
// 자체 경량 2D: 키네마틱 이동 + 벽 슬라이드 + 트리거 + 층(FloorLevel) 인지 충돌.
//
// 규약(docs/03 §8):
//   - 셰이프: AABB·원. Collider2D = 셰이프+오프셋+isTrigger+layerMask+floorMask.
//   - 브로드페이즈: 균일 공간 해시 그리드(타일맵 셀 크기 정렬).
//   - KinematicBody2D: move-and-slide, 벽 슬라이드, FloorLevel 전이.
//   - 쿼리(Raycast/OverlapArea)는 모두 floorLevel 필터 인자를 받는다 — 다리 위/아래는
//     같은 XY라도 floorLevel이 다르면 충돌하지 않는다.
//   - 트리거 Enter/Exit는 월드-로컬 EventBus(World::Events())로 발행(05 Lua·06 오디오 대비).
//   - 실행: PhaseFixedUpdate 내 고정 스텝.
#pragma once

#include "mye/phys/Motion2D.h"
#include "mye/ecs/ComponentType.h"
#include "mye/ecs/Entity.h"
#include "mye/scene/RenderExtract.h"   // scene::FloorLevel(공유 컴포넌트) — 정본 소유는 렌더 추출 계약

#include <cstdint>
#include <optional>

namespace mye::phys {

using ecs::Entity;
using scene::FloorLevel;   // int8_t level — 충돌 필터·전이가 참조하는 공유 컴포넌트

// ---------------------------------------------------------------------------
// Collider2D 컴포넌트 — 셰이프 + 오프셋 + 트리거 플래그 + 레이어/floor 마스크
//
// layerMask: 이 콜라이더가 "속한" 레이어 비트. 쿼리·충돌은 상대 layerMask와 AND로 필터.
// floorMask: 이 콜라이더가 존재하는 층 비트(다리 위/아래 동시 존재 셀 등 복수 층 표현).
//            FloorLevel 컴포넌트가 없으면 기본 floor(0)로 간주. floorMask는 층-집합 필터.
// ---------------------------------------------------------------------------
struct Collider2D {
    MYE_COMPONENT(Collider2D);

    Shape2D  shape;
    Vec2     offset{0, 0};              // 엔티티 중심(WorldTransform 위치) 기준 오프셋
    bool     isTrigger = false;        // true = 겹침 이벤트만 발행, 물리 응답 없음
    uint32_t layerMask = 0xFFFFFFFFu;  // 소속 레이어 비트(기본 전체)
    uint32_t collidesWith = 0xFFFFFFFFu; // 충돌 대상 레이어 비트(기본 전체)
    uint8_t  floorMask = 0x01;         // 존재 층 비트(기본 floor 0). FloorLevel과 교차 필터.
    uint32_t triggerId = 0;            // 트리거 식별자(이벤트 페이로드에 실림, 05/06 소비)
};

// ---------------------------------------------------------------------------
// KinematicBody2D 컴포넌트 — 속도 기반 이동체. move-and-slide로 벽 슬라이드.
//
// velocity: 초당 이동(월드 단위). 스텝마다 velocity*dt만큼 이동 시도 후 충돌 슬라이드.
// snapToGround와 skin은 저장 호환용 필드다. 현재 2D 이동에서 사용하지 않는다.
// ---------------------------------------------------------------------------
struct KinematicBody2D {
    MYE_COMPONENT(KinematicBody2D);

    Vec2  velocity{0, 0};
    bool  snapToGround = false;
    float skin = 0.01f;        // 저장 호환용; 현재 2D 이동에는 미적용
    int   maxSlideIters = 4;   // move-and-slide 반복 상한

    // 직전 스텝 결과(읽기 전용 취급) — 게임 로직·애니메이션이 참조.
    Vec2  lastMove{0, 0};      // 실제 이동량
    bool  hitWall = false;     // 이번 스텝에 벽 충돌 발생
};

// ---------------------------------------------------------------------------
// 레이캐스트 결과(선택 최소 구현).
// ---------------------------------------------------------------------------
struct RayHit {
    Entity entity = Entity::Null();
    Vec2   point{0, 0};
    Vec2   normal{0, 0};
    float  distance = 0.0f;
};

// ---------------------------------------------------------------------------
// 트리거 Enter/Exit 이벤트 — 월드-로컬 EventBus로 발행(05 Lua·06 오디오 구독 대비).
// entity = 트리거에 진입/이탈한 상대(보통 KinematicBody), trigger = isTrigger 콜라이더.
// trivially copyable(Enqueue/Publish 양쪽 허용).
// ---------------------------------------------------------------------------
struct TriggerEnterEvent {
    MYE_EVENT(TriggerEnterEvent);
    Entity   trigger = Entity::Null();  // isTrigger 콜라이더 엔티티
    Entity   other = Entity::Null();    // 진입한 상대 엔티티
    uint32_t triggerId = 0;             // Collider2D::triggerId
};

struct TriggerExitEvent {
    MYE_EVENT(TriggerExitEvent);
    Entity   trigger = Entity::Null();
    Entity   other = Entity::Null();
    uint32_t triggerId = 0;
};

} // namespace mye::phys
