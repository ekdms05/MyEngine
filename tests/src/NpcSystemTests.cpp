// NpcSystemTests.cpp — NPC 배회·접근·상호작용 헤드리스 테스트 (M6-B, 06)
//
// 검증 범위:
//   - 배회: Patrol 모드가 웨이포인트를 순서대로 따라간다(도착→대기→다음 웨이포인트).
//   - 접근: 플레이어가 alertRadius 안에 들면 이동을 멈추고 플레이어를 바라본다(AlertPaused).
//   - 상호작용(C++ 콜백): interact → Interacting 잠금 → busy=true 동안 정지 → busy=false 후 배회 재개.
//   - 상호작용(Lua 바인딩+대화 코루틴): mye.npc.interact → on_interact 코루틴 say → 대화 진행으로
//     종료 → NpcSystem 배회 재개. 대화 중 NPC 정지.
//   - 다중 NPC 독립: 한 NPC 상호작용 중에도 다른 NPC 는 계속 배회한다.
#include "TestFramework.h"

#include "mye/runtime/NpcSystem.h"
#include "mye/runtime/NpcBindings.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/RuntimeBindings.h"

#include "mye/script/ScriptRuntime.h"

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

#include "mye/core/Events.h"

#include <sol/sol.hpp>

using namespace mye;
using namespace mye::runtime;

namespace {

// 테스트용 엔티티 + LocalTransform 생성 헬퍼.
ecs::Entity MakeAt(ecs::World& w, Vec2 p) {
    ecs::Entity e = w.Create();
    auto& tf = w.Add<scene::LocalTransform>(e);
    tf.position = Vec3{p.x, p.y, 0.0f};
    return e;
}

Vec2 PosOf(ecs::World& w, ecs::Entity e) {
    auto* tf = w.TryGet<scene::LocalTransform>(e);
    return tf ? Vec2{tf->position.x, tf->position.y} : Vec2{0, 0};
}

// NpcSystem + MoveController 를 함께 한 시뮬 틱 전진(고정틱 규약).
void Step(NpcSystem& npc, MoveController& move, float dt) {
    move.Update(dt);   // 이동(Transform 전진)
    npc.Tick(dt);      // 상태기계·접근·상호작용
}

} // namespace

// ===========================================================================
// 배회 — Patrol 이 웨이포인트를 순서대로 따라간다
// ===========================================================================
MYE_TEST(NpcWanderFollowsWaypoints) {
    ecs::World world;
    ecs::Entity guard = MakeAt(world, Vec2{0, 0});

    MoveController move;
    move.Initialize(&world, nullptr);   // 직선 이동
    NpcSystem npc;
    npc.Initialize(&world, &move);

    NpcDesc d;
    d.entity = guard;
    d.id = "guard";
    d.mode = WanderMode::Patrol;
    d.loop = true;
    d.waypoints = { Vec2{5, 0}, Vec2{5, 5} };
    d.moveSpeed = 10.0f;
    d.waitMin = 0.1f; d.waitMax = 0.1f;   // 결정론적 짧은 대기
    d.alertRadius = 0.0f;                 // 플레이어 없음 → 무관
    MYE_EXPECT(npc.Register(d));
    MYE_EXPECT(npc.Count() == 1);

    // 초기 대기(0.1s) 소모 → 첫 웨이포인트로 이동 시작.
    Step(npc, move, 0.1f);   // waitTimer 만료 → PickNextTarget
    MYE_EXPECT(npc.State(guard) == NpcState::Moving);
    // wpIndex 0 에서 +1 → waypoints[1]? 아니다: 시작 wpIndex=0, next=0+1=1 → {5,5}? 확인:
    //   Patrol 첫 이동은 wpIndex(0)+wpDir(1)=1 → waypoints[1]={5,5}. (waypoints[0] 은 시작점 취급.)
    Vec2 first = npc.CurrentWaypoint(guard);
    MYE_EXPECT(first.x == 5.0f && first.y == 5.0f);

    // 이동 완료까지 틱(속도 10, 거리 ~7.07 → <1s).
    for (int i = 0; i < 20 && npc.State(guard) == NpcState::Moving; ++i)
        Step(npc, move, 0.1f);
    Vec2 arrived = PosOf(world, guard);
    MYE_EXPECT_NEAR(arrived.x, 5.0f, 1e-2f);
    MYE_EXPECT_NEAR(arrived.y, 5.0f, 1e-2f);
    MYE_EXPECT(npc.State(guard) == NpcState::Idle);

    // 대기 후 다음 웨이포인트(루프 → index 0 = {5,0}).
    Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(guard) == NpcState::Moving);
    Vec2 second = npc.CurrentWaypoint(guard);
    MYE_EXPECT(second.x == 5.0f && second.y == 0.0f);

    npc.Shutdown();
    move.Shutdown();
}

// ===========================================================================
// 접근 — 플레이어가 alertRadius 안에 들면 정지·주시
// ===========================================================================
MYE_TEST(NpcStopsWhenPlayerApproaches) {
    ecs::World world;
    ecs::Entity guard  = MakeAt(world, Vec2{0, 0});
    ecs::Entity player = MakeAt(world, Vec2{100, 100});   // 처음엔 멀리

    MoveController move;
    move.Initialize(&world, nullptr);
    NpcSystem npc;
    npc.Initialize(&world, &move);
    npc.SetPlayer(player);

    NpcDesc d;
    d.entity = guard; d.id = "guard";
    d.mode = WanderMode::Patrol;
    d.waypoints = { Vec2{5, 0} };
    d.moveSpeed = 5.0f;
    d.waitMin = 0.1f; d.waitMax = 0.1f;
    d.alertRadius = 2.5f;
    npc.Register(d);

    // 배회 시작(플레이어 멀어 정상 이동).
    Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(guard) == NpcState::Moving);
    Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(guard) == NpcState::Moving);

    // 플레이어를 NPC 근처로 이동 → 다음 틱에 AlertPaused.
    auto* ptf = world.TryGet<scene::LocalTransform>(player);
    ptf->position = Vec3{PosOf(world, guard).x + 1.0f, PosOf(world, guard).y, 0.0f};
    Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(guard) == NpcState::AlertPaused);

    // 정지 상태에서 위치가 더 이상 크게 변하지 않는다(이동 취소됨).
    Vec2 hold = PosOf(world, guard);
    Step(npc, move, 0.1f);
    Vec2 hold2 = PosOf(world, guard);
    MYE_EXPECT_NEAR(hold.x, hold2.x, 1e-3f);
    MYE_EXPECT_NEAR(hold.y, hold2.y, 1e-3f);

    // 플레이어가 멀어지면 배회 재개.
    ptf->position = Vec3{100, 100, 0};
    Step(npc, move, 0.1f);   // AlertPaused → Idle
    MYE_EXPECT(npc.State(guard) != NpcState::AlertPaused);

    npc.Shutdown();
    move.Shutdown();
}

// ===========================================================================
// 상호작용(C++ 콜백) — 개시→정지→종료→재개
// ===========================================================================
MYE_TEST(NpcInteractionLockAndResume) {
    ecs::World world;
    ecs::Entity guard  = MakeAt(world, Vec2{0, 0});
    ecs::Entity player = MakeAt(world, Vec2{1, 0});   // interactRadius 안

    MoveController move;
    move.Initialize(&world, nullptr);
    NpcSystem npc;
    npc.Initialize(&world, &move);
    npc.SetPlayer(player);

    NpcDesc d;
    d.entity = guard; d.id = "guard";
    d.mode = WanderMode::Patrol;
    d.waypoints = { Vec2{5, 0} };
    d.waitMin = 0.1f; d.waitMax = 0.1f;
    d.alertRadius = 0.0f;         // 접근 정지 배제(상호작용만 검증)
    d.interactRadius = 1.5f;
    npc.Register(d);

    // busy 플래그를 테스트가 제어(대화 시뮬레이션).
    bool busy = false;
    int beginCount = 0;
    npc.SetInteractHandlers(
        [&](const std::string& id, ecs::Entity) { (void)id; ++beginCount; busy = true; return true; },
        [&](const std::string&, ecs::Entity) { return busy; });

    // 상호작용 개시.
    ecs::Entity hit = npc.TryInteractNearest();
    MYE_EXPECT(hit == guard);
    MYE_EXPECT(beginCount == 1);
    MYE_EXPECT(npc.IsAnyInteracting());
    MYE_EXPECT(npc.InteractingNpc() == guard);
    MYE_EXPECT(npc.State(guard) == NpcState::Interacting);

    // 대화 중(busy=true): 여러 틱을 돌려도 Interacting 유지·이동 없음·중복 개시 거부.
    for (int i = 0; i < 5; ++i) Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(guard) == NpcState::Interacting);
    MYE_EXPECT(npc.TryInteractNearest().IsNull());   // 중복 개시 거부
    MYE_EXPECT(beginCount == 1);

    // 대화 종료(busy=false) → 다음 틱에 잠금 해제·배회 재개.
    busy = false;
    Step(npc, move, 0.1f);
    MYE_EXPECT(!npc.IsAnyInteracting());
    MYE_EXPECT(npc.State(guard) != NpcState::Interacting);

    npc.Shutdown();
    move.Shutdown();
}

// ===========================================================================
// 다중 NPC 독립 — 한 NPC 상호작용 중 다른 NPC 는 계속 배회
// ===========================================================================
MYE_TEST(NpcMultipleIndependent) {
    ecs::World world;
    ecs::Entity a = MakeAt(world, Vec2{0, 0});
    ecs::Entity b = MakeAt(world, Vec2{20, 0});
    ecs::Entity player = MakeAt(world, Vec2{1, 0});   // a 와 상호작용 거리, b 와는 멀다

    MoveController move;
    move.Initialize(&world, nullptr);
    NpcSystem npc;
    npc.Initialize(&world, &move);
    npc.SetPlayer(player);

    NpcDesc da; da.entity = a; da.id = "a"; da.mode = WanderMode::Patrol;
    da.waypoints = { Vec2{5, 0} }; da.waitMin = 0.1f; da.waitMax = 0.1f;
    da.alertRadius = 0.0f; da.interactRadius = 1.5f;
    NpcDesc db; db.entity = b; db.id = "b"; db.mode = WanderMode::Patrol;
    db.waypoints = { Vec2{25, 0} }; db.moveSpeed = 10.0f; db.waitMin = 0.1f; db.waitMax = 0.1f;
    db.alertRadius = 2.5f; db.interactRadius = 1.5f;
    npc.Register(da);
    npc.Register(db);
    MYE_EXPECT(npc.Count() == 2);

    bool busy = false;
    npc.SetInteractHandlers(
        [&](const std::string&, ecs::Entity) { busy = true; return true; },
        [&](const std::string&, ecs::Entity) { return busy; });

    // a 와 상호작용 개시.
    ecs::Entity hit = npc.TryInteractNearest();
    MYE_EXPECT(hit == a);
    MYE_EXPECT(npc.State(a) == NpcState::Interacting);

    // b 는 독립 배회 — 여러 틱에 걸쳐 웨이포인트로 전진(멀어서 접근/상호작용 무관).
    for (int i = 0; i < 20; ++i) Step(npc, move, 0.1f);
    MYE_EXPECT(npc.State(a) == NpcState::Interacting);   // a 는 여전히 대화 중
    Vec2 bpos = PosOf(world, b);
    MYE_EXPECT_NEAR(bpos.x, 25.0f, 1e-1f);               // b 는 목표 도달(독립 진행)

    npc.Shutdown();
    move.Shutdown();
}

// ===========================================================================
// 상호작용(Lua 바인딩 + 대화 코루틴) — mye.npc.interact → on_interact say → 진행 후 재개
// ===========================================================================
MYE_TEST(NpcLuaInteractionDrivesDialogue) {
    ecs::World world;
    ecs::Entity guard  = MakeAt(world, Vec2{0, 0});
    ecs::Entity player = MakeAt(world, Vec2{1, 0});

    MoveController move;
    move.Initialize(&world, nullptr);

    EventBus bus;
    DialogueSystem dlg;
    dlg.Initialize(nullptr, nullptr, nullptr, &bus);   // 헤드리스: 상태 폴링

    CutsceneRuntime cut;
    cut.Initialize(&dlg, &move, nullptr);

    NpcSystem npc;
    npc.Initialize(&world, &move);
    npc.SetPlayer(player);

    // 스크립트 런타임 + 대화·NPC 바인딩.
    script::ScriptRuntime rt;
    rt.Initialize(script::StdLibPolicy{}, &bus, nullptr);

    RuntimeBindings dlgBindings(&dlg, &cut, nullptr, nullptr, nullptr);
    NpcBindings npcBindings(&npc, &rt);
    rt.AddBindingModule(&dlgBindings);
    rt.AddBindingModule(&npcBindings);
    rt.ReapplyBindings();

    // Lua 로 NPC 등록(웨이포인트 없이 제자리 — 배회는 다른 테스트가 검증). on_interact 는 say 1회.
    sol::state& lua = rt.State();
    lua["_GUARD"] = static_cast<double>(guard.Packed());
    lua["_PLAYER"] = static_cast<double>(player.Packed());
    auto reg = rt.DoString(R"LUA(
        _flags = { talked = false }
        mye.npc.set_player(_PLAYER)
        mye.npc.register{
            entity = _GUARD, id = "guard", mode = "none",
            interact_radius = 1.5, alert_radius = 0.0,
            on_interact = function()
                _flags.talked = true
                mye.dialogue.say("경비병", "멈춰라.")
            end,
        }
    )LUA", "npc_setup.lua");
    MYE_EXPECT(bool(reg));
    MYE_EXPECT(npc.Count() == 1);

    // 상호작용 개시(플레이어 입력 시뮬).
    auto call = rt.DoString("_hit = mye.npc.interact()", "interact.lua");
    MYE_EXPECT(bool(call));
    MYE_EXPECT(lua["_hit"].get<double>() != 0.0);
    MYE_EXPECT(npc.IsAnyInteracting());
    MYE_EXPECT(npc.State(guard) == NpcState::Interacting);

    // on_interact 코루틴이 say 를 시작 → 대화창 표시 대기. talked 플래그·대화 상태 확인.
    MYE_EXPECT(lua["_flags"]["talked"].get<bool>());
    MYE_EXPECT(dlg.State() == DialogueState::ShowingLine);

    // 대화 중에는 NPC 정지 유지(틱을 돌려도 Interacting).
    for (int i = 0; i < 3; ++i) {
        Step(npc, move, 0.1f);
        rt.UpdateCoroutines(0.1f);
    }
    MYE_EXPECT(npc.State(guard) == NpcState::Interacting);
    MYE_EXPECT(npc.IsAnyInteracting());

    // 진행 입력 → say 완료 → 코루틴 종료 → busy=false → 배회 재개.
    dlg.Advance();
    dlg.Update(0.1f);            // Finished → Idle
    rt.UpdateCoroutines(0.1f);   // 코루틴 종료(busy=false)
    npc.Tick(0.1f);              // busy 폴링 → 잠금 해제
    MYE_EXPECT(!npc.IsAnyInteracting());
    MYE_EXPECT(npc.State(guard) != NpcState::Interacting);

    rt.Shutdown();
    npc.Shutdown();
    move.Shutdown();
}
