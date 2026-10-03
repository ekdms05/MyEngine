// mye/anim/AnimationSystem.h — 애니메이션 시스템(샘플링·전이·이벤트) (docs/03 §6, M3-B)
//
// PhasePostUpdate(또는 Update)에서 SpriteAnimator + SpriteRenderer 를 순회:
//   1) 파라미터 조건으로 상태 전이 평가(트리거 소모·클립종료 전이 포함).
//   2) 현재 클립을 dt(×speed)로 진행 — direction/loop/frameDurations 반영(ClipPlayback).
//   3) 경과 프레임의 AnimEventMarker 를 월드-로컬 EventBus 로 AnimationEvent 발행(유실 없음).
//   4) 현재 SpriteFrame(sheet->frames[idx]) → SpriteRenderer.srcUV/pivotPx/flipX/sprite 갱신.
//
// EvaluateAndSample 는 ECS/EventBus 비의존 헬퍼(테스트 용이). onEvent 콜백으로 이벤트를 받는다.
#pragma once

#include "mye/anim/SpriteAnimator.h"
#include "mye/core/Time.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"

namespace mye { class EventBus; }
namespace mye::ecs { class World; class CommandBuffer; }
namespace mye::scene { class SystemScheduler; }

namespace mye::anim {

// One playback cursor per entity, shared by 2D sprites and 3D billboards.
template<class Fn> void ForEachAnimatedRenderer(ecs::World& world, Fn&& fn) {
    world.Query<SpriteAnimator>().Each([&](ecs::Entity entity, SpriteAnimator& animator) {
        if (auto* sprite = world.TryGet<scene::SpriteRenderer>(entity)) fn(entity, animator, *sprite);
        else if (auto* billboard = world.TryGet<scene::BillboardRenderer>(entity)) fn(entity, animator, *billboard);
    });
}

// 파라미터 조건 하나를 평가.
bool EvaluateCondition(const SpriteAnimator& a, const AnimCondition& c);

// 상태 전이 평가(1스텝). 전이가 일어나면 currentState 갱신 + 커서 리셋 정책 적용 후 true.
// keepPhase=true(기본): 전이 시 클립 전체 시간의 진행률을 유지한다(걷기↔대기 자연 전환).
bool StepTransitions(SpriteAnimator& a, bool keepPhase = true);

// Presentation-only sampling: binding/rendering must not consume transitions or entry events.
template <typename RendererT>
void SampleAnimator(SpriteAnimator& animator, RendererT* renderer) {
    const auto resolved = ResolveActiveClip(animator);
    if (!resolved.clip || resolved.clip->frameIndices.empty()) return;
    animator.flipX = resolved.flipX;
    animator.currentFrameIndex = CurrentFrameIndex(*resolved.clip, CursorForFacing(animator, resolved));
    const auto* sheet = ResolveActiveSheet(animator);
    if (!renderer || !sheet || animator.currentFrameIndex >= sheet->frames.size()) return;
    const auto& frame = sheet->frames[animator.currentFrameIndex];
    renderer->srcUV = frame.uv;
    renderer->pivotPx = frame.pivotInPixels ? frame.pivot :
        Vec2{frame.pivot.x * frame.rect.w, frame.pivot.y * frame.rect.h};
    renderer->flipX = animator.flipX;
    renderer->sprite = sheet->texture;
}

// 한 애니메이터를 dt 만큼 진행·샘플링. onEvent(entity, marker) 로 경과 이벤트를 보고한다.
// renderer 가 non-null 이면 현재 프레임을 렌더러 필드로 옮긴다(sprite/srcUV/pivotPx/flipX).
// SpriteRenderer 는 전방 선언만 필요하므로 템플릿으로 받는다(scene 헤더 강결합 회피).
template <typename RendererT, typename OnEvent>
void UpdateAnimator(SpriteAnimator& a, float dt, RendererT* renderer, OnEvent&& onEvent) {
    // 초기화: 상태 머신이 있고 아직 상태가 없으면 초기 상태 진입.
    if (a.machine && a.currentState < 0) {
        a.currentState = a.machine->initialState;
        a.cursor = ClipCursor{};
        a.started = false;
    }

    // 전이 평가(진행 전). 전이로 클립이 바뀌면 위상 유지 정책이 커서를 정리한다.
    StepTransitions(a, /*keepPhase*/true);

    ResolvedClip rc = ResolveActiveClip(a);
    a.flipX = rc.flipX;
    if (!rc.clip || rc.clip->frameIndices.empty()) return;
    const AnimationClipData& clip = *rc.clip;
    a.cursor = CursorForFacing(a, rc);
    a.playbackFacing = a.facing;

    // 커서 step 이 클립 범위를 벗어났으면(상태/클립 교체 후) 클램프.
    const uint32_t period = ClipStepPeriod(clip);
    if (period > 0 && a.cursor.step >= period) { a.cursor.step %= period; }

    // 첫 진입 프레임의 이벤트 발행(재생 시작·상태 전환 직후 1회).
    if (!a.started) {
        a.started = true;
        EmitStepEnterEvents(clip, a.cursor.step, [&](const AnimEventMarker& m) { onEvent(m); });
    }

    if (a.playing && dt > 0.0f) {
        const float scaled = dt * (a.speed > 0.0f ? a.speed : 0.0f);
        AdvanceClip(clip, a.cursor, scaled,
                    [&](const AnimEventMarker& m) { onEvent(m); });
    }

    SampleAnimator(a, renderer);
}

// SystemScheduler(Update 페이즈)에 AnimationSystem 등록. 월드-로컬 버스로 AnimationEvent 발행.
// (SpriteRenderer 갱신은 같은 시스템에서 수행 — RenderExtract 이전 페이즈이므로 안전.)
void RegisterAnimationSystem(scene::SystemScheduler& scheduler);

// 월드 순회 1회 실행(스케줄러 없이 호출 가능 — 테스트·수동 틱). world->Events() 로 발행.
void RunAnimationSystem(ecs::World& world, float dt);

} // namespace mye::anim
