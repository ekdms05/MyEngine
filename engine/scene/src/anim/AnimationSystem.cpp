// mye/anim/AnimationSystem.cpp — 전이 평가·시스템 등록·월드 순회 (M3-B, docs/03 §6)
#include "mye/anim/AnimationSystem.h"

#include "mye/core/Events.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/SystemScheduler.h"

namespace mye::anim {

bool EvaluateCondition(const SpriteAnimator& a, const AnimCondition& c) {
    const AnimParam* p = a.FindParam(c.param);
    const float v = p ? p->value : 0.0f;
    switch (c.op) {
        case CmpOp::Greater:      return v >  c.threshold;
        case CmpOp::Less:         return v <  c.threshold;
        case CmpOp::GreaterEqual: return v >= c.threshold;
        case CmpOp::LessEqual:    return v <= c.threshold;
        case CmpOp::Equal:        return v == c.threshold;
        case CmpOp::NotEqual:     return v != c.threshold;
        case CmpOp::IsTrue:       return v != 0.0f;
        case CmpOp::IsFalse:      return v == 0.0f;
        default:                  return false;
    }
}

namespace {

// 전이 조건 전체가 만족되는지(모두 AND). onClipFinished 전이는 커서 finished 도 요구.
bool TransitionReady(const SpriteAnimator& a, const AnimTransition& t) {
    if (t.onClipFinished && !a.cursor.finished) return false;
    for (const AnimCondition& c : t.conditions) {
        if (!EvaluateCondition(a, c)) return false;
    }
    return true;
}

void ConsumeTriggers(SpriteAnimator& a, const AnimTransition& t) {
    for (const std::string& name : t.consumeTriggers) {
        if (AnimParam* p = a.FindParam(name)) p->value = 0.0f;
    }
    // 조건에서 참조된 Trigger 파라미터도 자동 소모(명시 목록 없이도 안전).
    for (const AnimCondition& c : t.conditions) {
        if (AnimParam* p = a.FindParam(c.param); p && p->type == ParamType::Trigger)
            p->value = 0.0f;
    }
}

} // namespace

bool StepTransitions(SpriteAnimator& a, bool keepPhase) {
    if (!a.machine) return false;
    const AnimStateMachine& sm = *a.machine;

    for (const AnimTransition& t : sm.transitions) {
        const bool fromMatch = (t.from < 0) || (t.from == a.currentState);
        if (!fromMatch) continue;
        if (t.to == a.currentState) continue;         // self-transition 무시
        if (!TransitionReady(a, t)) continue;

        // 전이 실행.
        ConsumeTriggers(a, t);
        const int prevState = a.currentState;
        a.currentState = t.to;

        // 위상 유지 정책: 걷기↔대기처럼 프레임 위상을 유지(step/timeInStep 보존).
        // 단 finished 플래그는 새 클립 기준으로 리셋(비루프 재진입 대비).
        if (!keepPhase) {
            a.cursor = ClipCursor{};
        } else {
            a.cursor.finished = false;
        }
        a.started = false;   // 새 상태 첫 프레임 이벤트 재발행
        (void)prevState;
        return true;         // 1스텝당 1전이(체이닝은 다음 프레임)
    }
    return false;
}

// ---------------------------------------------------------------------------
// 월드 순회 — SpriteAnimator + SpriteRenderer 를 갱신하고 이벤트를 월드 버스로 발행.
// ---------------------------------------------------------------------------
void RunAnimationSystem(ecs::World& world, float dt) {
    EventBus* bus = world.Events();
    auto view = world.Query<SpriteAnimator, scene::SpriteRenderer>();
    view.Each([&](ecs::Entity e, SpriteAnimator& a, scene::SpriteRenderer& r) {
        UpdateAnimator(a, dt, &r, [&](const AnimEventMarker& m) {
            if (!bus) return;
            AnimationEvent ev;
            ev.entity = e;
            ev.name = m.name.c_str();
            ev.stringArg = m.stringArg.c_str();
            ev.floatArg = m.floatArg;
            ev.frameIndex = m.frameIndex;
            bus->Publish(ev);
        });
    });
}

void RegisterAnimationSystem(scene::SystemScheduler& scheduler) {
    scene::SystemDesc desc;
    desc.name = "AnimationSystem";
    desc.phase = scene::Phase::Update;
    desc.fn = [](ecs::World& world, ecs::CommandBuffer& /*cb*/, const TimeStep& time) {
        RunAnimationSystem(world, static_cast<float>(time.deltaSeconds));
    };
    scheduler.RegisterSystem(std::move(desc));
}

} // namespace mye::anim
