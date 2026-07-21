// AnimationTests.cpp — 스프라이트 애니메이션·8방향·상태머신·이벤트 단위 테스트 (M3-B / docs/03 §6)
//
// 검증 불변식:
//   - 클립 재생 프레임/타이밍 정확(frameDurations 반영)
//   - direction(Forward/Reverse/PingPong)·loop
//   - 8방향 선택(방향벡터→인덱스)·flipX 대칭 공유
//   - 상태 전이(파라미터 조건 만족 시)·위상 유지
//   - 애니 이벤트가 정확한 프레임에 1회 발행(큰 dt 스킵 시 유실 없음)
#include "TestFramework.h"

#include "mye/anim/AnimationSystem.h"
#include "mye/anim/AnimationTypes.h"
#include "mye/anim/ClipPlayback.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/asset/SpriteSheet.h"
#include "mye/core/Events.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"

#include <string>
#include <vector>

using namespace mye;
using namespace mye::anim;
using mye::asset::AnimationClipData;
using mye::asset::AnimEventMarker;
using mye::asset::SpriteFrame;
using mye::asset::SpriteSheet;

namespace {

// 4프레임, 각 0.1초, loop, Forward 클립. frameIndices = {10,11,12,13}.
AnimationClipData MakeClip(bool loop = true,
                           AnimationClipData::Direction dir = AnimationClipData::Direction::Forward) {
    AnimationClipData c;
    c.name = "walk";
    c.frameIndices = {10, 11, 12, 13};
    c.frameDurations = {0.1f, 0.1f, 0.1f, 0.1f};
    c.loop = loop;
    c.direction = dir;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// 클립 재생: 프레임/타이밍 정확 + loop wrap
// ---------------------------------------------------------------------------
MYE_TEST(AnimClipForwardTimingAndLoop) {
    AnimationClipData clip = MakeClip();
    ClipCursor cur;

    // t=0 → frame 10 (slot 0)
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 10);

    // +0.05 아직 slot 0
    AdvanceClip(clip, cur, 0.05f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 10);

    // +0.1 → slot 1 (frame 11)
    AdvanceClip(clip, cur, 0.1f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 11);

    // 총 경과 0.35 → slot 3 (frame 13)
    AdvanceClip(clip, cur, 0.2f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 13);

    // +0.1 → wrap loop → slot 0 (frame 10)
    AdvanceClip(clip, cur, 0.1f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 10);
    MYE_EXPECT(!cur.finished);
}

// ---------------------------------------------------------------------------
// 비루프 클립: 마지막 프레임에 정지 + finished
// ---------------------------------------------------------------------------
MYE_TEST(AnimClipNonLoopFinishes) {
    AnimationClipData clip = MakeClip(/*loop*/false);
    ClipCursor cur;
    // 큰 dt로 끝까지
    AdvanceClip(clip, cur, 10.0f, [](const AnimEventMarker&){});
    MYE_EXPECT(cur.finished);
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 13);   // 마지막 프레임 정지
    // 정지 후 추가 진행 no-op
    AdvanceClip(clip, cur, 1.0f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 13);
}

// ---------------------------------------------------------------------------
// direction: Reverse / PingPong
// ---------------------------------------------------------------------------
MYE_TEST(AnimClipReverse) {
    AnimationClipData clip = MakeClip(true, AnimationClipData::Direction::Reverse);
    ClipCursor cur;
    // slot0 → frameIndices[N-1-0]=frameIndices[3]=13
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 13);
    AdvanceClip(clip, cur, 0.1f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 12);
    AdvanceClip(clip, cur, 0.1f, [](const AnimEventMarker&){});
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == 11);
}

MYE_TEST(AnimClipPingPong) {
    AnimationClipData clip = MakeClip(true, AnimationClipData::Direction::PingPong);
    ClipCursor cur;
    // period = 2*4-2 = 6. 스텝 시퀀스 slot: 0,1,2,3,2,1,(wrap)0...
    uint32_t expect[] = {10, 11, 12, 13, 12, 11, 10, 11};
    MYE_EXPECT(CurrentFrameIndex(clip, cur) == expect[0]);
    for (int i = 1; i < 8; ++i) {
        AdvanceClip(clip, cur, 0.1f, [](const AnimEventMarker&){});
        MYE_EXPECT(CurrentFrameIndex(clip, cur) == expect[i]);
    }
}

// ---------------------------------------------------------------------------
// 8방향 선택: 방향벡터 → Dir8 인덱스
// ---------------------------------------------------------------------------
MYE_TEST(Anim8DirFromVector) {
    MYE_EXPECT(Dir8FromVector({0.0f, -1.0f}) == Dir8::Down);
    MYE_EXPECT(Dir8FromVector({-1.0f, 0.0f}) == Dir8::Left);
    MYE_EXPECT(Dir8FromVector({0.0f, 1.0f}) == Dir8::Up);
    MYE_EXPECT(Dir8FromVector({1.0f, 0.0f}) == Dir8::Right);
    MYE_EXPECT(Dir8FromVector({-1.0f, -1.0f}) == Dir8::DownLeft);
    MYE_EXPECT(Dir8FromVector({-1.0f, 1.0f}) == Dir8::UpLeft);
    MYE_EXPECT(Dir8FromVector({1.0f, 1.0f}) == Dir8::UpRight);
    MYE_EXPECT(Dir8FromVector({1.0f, -1.0f}) == Dir8::DownRight);
    // 영벡터 → fallback
    MYE_EXPECT(Dir8FromVector({0.0f, 0.0f}, Dir8::Up) == Dir8::Up);
    // 약간 치우친 입력도 가까운 섹터로 스냅
    MYE_EXPECT(Dir8FromVector({0.1f, -1.0f}) == Dir8::Down);
    MYE_EXPECT(Dir8FromVector({-0.9f, -0.1f}) == Dir8::Left);
    // 접미사 확인
    MYE_EXPECT(std::string(Dir8Suffix(Dir8::DownLeft)) == "down_left");
}

// ---------------------------------------------------------------------------
// flipX 대칭 공유: 우측 계열이 좌측 클립 + flipX로 해석
// ---------------------------------------------------------------------------
MYE_TEST(AnimDirMirrorFlipX) {
    AnimationClipData left;   left.name = "walk_left";
    AnimationClipData upLeft; upLeft.name = "walk_up_left";
    AnimationClipData down;   down.name = "walk_down";

    DirectionalAnimSet set;
    set.mirrorRight = true;
    set.clips[static_cast<size_t>(Dir8::Down)]   = &down;
    set.clips[static_cast<size_t>(Dir8::Left)]   = &left;
    set.clips[static_cast<size_t>(Dir8::UpLeft)] = &upLeft;

    // Right → Left 클립 + flipX
    auto r = set.Resolve(Dir8::Right);
    MYE_EXPECT(r.clip == &left);
    MYE_EXPECT(r.flipX == true);

    // UpRight → UpLeft 클립 + flipX
    auto ur = set.Resolve(Dir8::UpRight);
    MYE_EXPECT(ur.clip == &upLeft);
    MYE_EXPECT(ur.flipX == true);

    // Left 직접(플립 없음)
    auto l = set.Resolve(Dir8::Left);
    MYE_EXPECT(l.clip == &left);
    MYE_EXPECT(l.flipX == false);
}

// ---------------------------------------------------------------------------
// 애니 이벤트: 정확한 프레임에 1회 발행 + 큰 dt 스킵 시 유실 없음
// ---------------------------------------------------------------------------
MYE_TEST(AnimEventEmitNoLossOnSkip) {
    AnimationClipData clip = MakeClip();
    // frame slot 1, 3 에 footstep 마커.
    clip.events.push_back(AnimEventMarker{1, "footstep", "", 1.0f});
    clip.events.push_back(AnimEventMarker{3, "footstep", "", 2.0f});

    ClipCursor cur;
    std::vector<uint32_t> fired;   // 발행된 마커 frameIndex 순서

    // 큰 dt(0.35초)로 slot 0→3 를 한 번에 통과: slot1, slot3 마커 모두 발행돼야.
    AdvanceClip(clip, cur, 0.35f, [&](const AnimEventMarker& m) { fired.push_back(m.frameIndex); });
    MYE_EXPECT(fired.size() == 2);
    MYE_EXPECT(fired.size() == 2 && fired[0] == 1 && fired[1] == 3);

    // 다시 한 바퀴(loop wrap) → slot0(마커없음)→slot1 발행. 0.5초로 wrap 후 slot1 도달.
    fired.clear();
    AdvanceClip(clip, cur, 0.3f, [&](const AnimEventMarker& m) { fired.push_back(m.frameIndex); });
    // slot3(현재)→wrap→slot0→slot1... 경로에서 slot1 최소 1회.
    MYE_EXPECT(!fired.empty());
    bool has1 = false; for (auto f : fired) if (f == 1) has1 = true;
    MYE_EXPECT(has1);
}

// 초기 진입 프레임(slot 0)에 마커가 있으면 EmitStepEnterEvents 로 1회 발행.
MYE_TEST(AnimEventFirstFrameEmit) {
    AnimationClipData clip = MakeClip();
    clip.events.push_back(AnimEventMarker{0, "start", "", 0.0f});
    int count = 0;
    EmitStepEnterEvents(clip, 0, [&](const AnimEventMarker&){ ++count; });
    MYE_EXPECT(count == 1);
    EmitStepEnterEvents(clip, 1, [&](const AnimEventMarker&){ ++count; });
    MYE_EXPECT(count == 1);   // slot 1 엔 마커 없음
}

// ---------------------------------------------------------------------------
// 상태 전이: 파라미터 조건 만족 시 전이 + 위상 유지
// ---------------------------------------------------------------------------
MYE_TEST(AnimStateTransitionOnCondition) {
    AnimationClipData idle = MakeClip();  idle.name = "idle";
    AnimationClipData walk = MakeClip();  walk.name = "walk";

    AnimStateMachine sm;
    // state 0 = idle, state 1 = walk
    { AnimState s; s.name = "idle"; s.directional = false; s.singleClip = &idle; sm.states.push_back(s); }
    { AnimState s; s.name = "walk"; s.directional = false; s.singleClip = &walk; sm.states.push_back(s); }
    sm.initialState = 0;
    // idle→walk when isMoving true
    { AnimTransition t; t.from = 0; t.to = 1; t.conditions.push_back({"isMoving", CmpOp::IsTrue, 0}); sm.transitions.push_back(t); }
    // walk→idle when isMoving false
    { AnimTransition t; t.from = 1; t.to = 0; t.conditions.push_back({"isMoving", CmpOp::IsFalse, 0}); sm.transitions.push_back(t); }

    SpriteAnimator a;
    a.machine = &sm;
    a.currentState = sm.initialState;

    // isMoving=false → 전이 없음
    a.SetBool("isMoving", false);
    MYE_EXPECT(!StepTransitions(a));
    MYE_EXPECT(a.currentState == 0);

    // 위상 진행 후 전이 시 위상 유지 확인.
    a.cursor.step = 2;
    a.cursor.timeInStep = 0.03f;
    a.SetBool("isMoving", true);
    MYE_EXPECT(StepTransitions(a));
    MYE_EXPECT(a.currentState == 1);
    MYE_EXPECT(a.cursor.step == 2);           // keepPhase: 위상 유지
    MYE_EXPECT_NEAR(a.cursor.timeInStep, 0.03f, 1e-6f);

    // walk→idle
    a.SetBool("isMoving", false);
    MYE_EXPECT(StepTransitions(a));
    MYE_EXPECT(a.currentState == 0);
}

// trigger 소모 전이
MYE_TEST(AnimStateTriggerConsumed) {
    AnimationClipData idle = MakeClip(); AnimationClipData atk = MakeClip();
    AnimStateMachine sm;
    { AnimState s; s.name="idle"; s.directional=false; s.singleClip=&idle; sm.states.push_back(s); }
    { AnimState s; s.name="attack"; s.directional=false; s.singleClip=&atk; sm.states.push_back(s); }
    sm.initialState = 0;
    { AnimTransition t; t.from=0; t.to=1; t.conditions.push_back({"attack", CmpOp::IsTrue, 0}); sm.transitions.push_back(t); }

    SpriteAnimator a; a.machine=&sm; a.currentState=0;
    a.EnsureParam("attack", ParamType::Trigger);
    a.SetTrigger("attack");
    MYE_EXPECT(StepTransitions(a));
    MYE_EXPECT(a.currentState == 1);
    // trigger 자동 소모 → 다시 idle로 돌아가도 재전이 안 됨
    MYE_EXPECT(a.GetFloat("attack") == 0.0f);
}

// onClipFinished 전이(비루프 클립 종료 시)
MYE_TEST(AnimStateClipFinishedTransition) {
    AnimationClipData atk = MakeClip(/*loop*/false); AnimationClipData idle = MakeClip();
    AnimStateMachine sm;
    { AnimState s; s.name="attack"; s.directional=false; s.singleClip=&atk; sm.states.push_back(s); }
    { AnimState s; s.name="idle"; s.directional=false; s.singleClip=&idle; sm.states.push_back(s); }
    sm.initialState = 0;
    { AnimTransition t; t.from=0; t.to=1; t.onClipFinished=true; sm.transitions.push_back(t); }

    SpriteAnimator a; a.machine=&sm; a.currentState=0;
    // 아직 안 끝남 → 전이 없음
    MYE_EXPECT(!StepTransitions(a));
    // 클립 끝냄
    a.cursor.finished = true;
    MYE_EXPECT(StepTransitions(a));
    MYE_EXPECT(a.currentState == 1);
}

// ---------------------------------------------------------------------------
// 통합: UpdateAnimator 가 SpriteRenderer 를 갱신 + 이벤트 발행
// ---------------------------------------------------------------------------
MYE_TEST(AnimSystemSamplesRendererAndEmits) {
    // 시트: 프레임 14개(0..13). frame 10..13 에 서로 다른 uv/rect.
    SpriteSheet sheet;
    for (uint32_t i = 0; i < 14; ++i) {
        SpriteFrame f;
        f.rect = { static_cast<int>(i) * 16, 0, 16, 24 };
        f.uv = { static_cast<float>(i) * 0.05f, 0.0f, 0.05f, 0.1f };
        f.pivot = { 0.5f, 0.0f };
        f.pivotInPixels = false;
        sheet.frames.push_back(f);
    }
    AnimationClipData clip = MakeClip();
    clip.events.push_back(AnimEventMarker{1, "footstep", "cue_step", 0.0f});

    SpriteAnimator a;
    a.sheet = &sheet;
    a.directClip = &clip;

    scene::SpriteRenderer r;
    std::vector<std::string> events;

    // t=0 초기화 + 첫 프레임 샘플 (frame 10).
    UpdateAnimator(a, 0.0f, &r, [&](const AnimEventMarker& m){ events.push_back(m.name); });
    MYE_EXPECT(a.currentFrameIndex == 10);
    MYE_EXPECT_NEAR(r.srcUV.x, 10 * 0.05f, 1e-5f);
    // pivotPx = pivot(정규화) * rect 크기 → (0.5*16, 0*24) = (8,0)
    MYE_EXPECT_NEAR(r.pivotPx.x, 8.0f, 1e-4f);

    // +0.1 → frame 11 + footstep 발행
    UpdateAnimator(a, 0.1f, &r, [&](const AnimEventMarker& m){ events.push_back(m.name); });
    MYE_EXPECT(a.currentFrameIndex == 11);
    MYE_EXPECT_NEAR(r.srcUV.x, 11 * 0.05f, 1e-5f);
    bool hasStep = false; for (auto& e : events) if (e == "footstep") hasStep = true;
    MYE_EXPECT(hasStep);
}

// ---------------------------------------------------------------------------
// 월드 순회: RunAnimationSystem 이 월드 버스로 AnimationEvent 발행
// ---------------------------------------------------------------------------
MYE_TEST(AnimSystemWorldBusPublish) {
    static AnimationClipData s_clip = MakeClip();
    if (s_clip.events.empty())
        s_clip.events.push_back(AnimEventMarker{1, "footstep", "", 0.0f});
    static SpriteSheet s_sheet;
    if (s_sheet.frames.empty())
        for (uint32_t i = 0; i < 14; ++i) { SpriteFrame f; f.rect = {0,0,16,16}; s_sheet.frames.push_back(f); }

    ecs::World world;
    EventBus bus;
    world.SetEventBus(&bus);

    int footsteps = 0;
    ecs::Entity captured{};
    bus.Subscribe<AnimationEvent>([&](const AnimationEvent& e) {
        if (std::string(e.name) == "footstep") { ++footsteps; captured = e.entity; }
        return false;
    });

    ecs::Entity e = world.Create();
    {
        SpriteAnimator a;
        a.sheet = &s_sheet;
        a.directClip = &s_clip;
        world.Add<SpriteAnimator>(e) = a;
    }
    world.Add<scene::SpriteRenderer>(e);

    // init(첫 프레임, slot0 마커 없음) → 아직 footstep 0
    RunAnimationSystem(world, 0.0f);
    MYE_EXPECT(footsteps == 0);

    // +0.1 → slot1 footstep 1회
    RunAnimationSystem(world, 0.1f);
    MYE_EXPECT(footsteps == 1);
    MYE_EXPECT(captured == e);
}
