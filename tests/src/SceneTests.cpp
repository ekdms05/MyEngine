// SceneTests.cpp — ECS 코어·Transform·스케줄러·CommandBuffer 단위 테스트 (M2-A / docs/03)
//
// 검증 불변식:
//   - 엔티티 생성/파괴/재사용 + generation 무효화(dangling 핸들 감지)
//   - View 다중 컴포넌트 교집합 순회(최소 풀 기준)
//   - CommandBuffer 지연 구조변경(Flush 타이밍) + 동적 경로
//   - Transform 부모-자식 월드행렬(row-vector, world=local*parentWorld) + reparent keepWorld
//   - SystemScheduler 5페이즈 + runBefore/runAfter 토폴로지 순서 + 순환 폴백
#include "TestFramework.h"

#include "mye/core/Events.h"
#include "mye/core/Time.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/scene/SystemScheduler.h"
#include "mye/scene/Transform.h"

#include <string>
#include <vector>

using namespace mye;
using mye::ecs::Entity;
using mye::ecs::World;
using mye::ecs::CommandBuffer;

namespace {

struct Position { MYE_COMPONENT(Position); float x = 0, y = 0; };
struct Velocity { MYE_COMPONENT(Velocity); float dx = 0, dy = 0; };
struct Tag      { MYE_COMPONENT(Tag);      int id = 0; };

bool MatNear(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::abs(a.m[r][c] - b.m[r][c]) > eps) return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 엔티티 수명 · generation 재사용/무효화
// ---------------------------------------------------------------------------
MYE_TEST(EcsEntityCreateDestroyReuse) {
    World w;
    Entity a = w.Create();
    Entity b = w.Create();
    MYE_EXPECT(w.Valid(a) && w.Valid(b));
    MYE_EXPECT(a.index != b.index);
    MYE_EXPECT(a.generation >= 1 && b.generation >= 1);   // gen 0 = null

    // 파괴 → 핸들 무효화. 세대 증가로 dangling 감지.
    w.Destroy(a);
    MYE_EXPECT(!w.Valid(a));

    // 인덱스 재사용 + 세대 증가 → 옛 핸들은 여전히 무효.
    Entity c = w.Create();
    MYE_EXPECT(c.index == a.index);
    MYE_EXPECT(c.generation != a.generation);
    MYE_EXPECT(w.Valid(c) && !w.Valid(a));

    // Null 핸들은 항상 무효.
    MYE_EXPECT(!w.Valid(Entity::Null()));
    MYE_EXPECT(Entity::Null().IsNull());
}

MYE_TEST(EcsPackedRoundTrip) {
    Entity e{42u, 7u};
    Entity r = Entity::FromPacked(e.Packed());
    MYE_EXPECT(r.index == 42u && r.generation == 7u);
    MYE_EXPECT(r == e);
}

// ---------------------------------------------------------------------------
// 컴포넌트 add/get/has/remove (swap-remove 안정성)
// ---------------------------------------------------------------------------
MYE_TEST(EcsComponentAddGetRemove) {
    World w;
    Entity e = w.Create();
    MYE_EXPECT(!w.Has<Position>(e));

    Position& p = w.Add<Position>(e);
    p.x = 3.0f; p.y = 4.0f;
    MYE_EXPECT(w.Has<Position>(e));
    Position* got = w.TryGet<Position>(e);
    MYE_EXPECT(got != nullptr && got->x == 3.0f && got->y == 4.0f);

    // 여러 엔티티에 add 후 중간 제거 → swap-remove 후에도 나머지 접근 정상.
    Entity e2 = w.Create();
    Entity e3 = w.Create();
    w.Add<Position>(e2).x = 20.0f;
    w.Add<Position>(e3).x = 30.0f;
    w.Remove<Position>(e2);
    MYE_EXPECT(!w.Has<Position>(e2));
    MYE_EXPECT(w.TryGet<Position>(e)->x == 3.0f);
    MYE_EXPECT(w.TryGet<Position>(e3)->x == 30.0f);

    // Destroy는 모든 풀에서 컴포넌트 제거.
    w.Destroy(e);
    MYE_EXPECT(w.TryGet<Position>(e) == nullptr);
}

// ---------------------------------------------------------------------------
// View — 다중 컴포넌트 교집합(최소 풀 기준)
// ---------------------------------------------------------------------------
MYE_TEST(EcsViewIntersection) {
    World w;
    // e1: Pos+Vel, e2: Pos만, e3: Pos+Vel, e4: Vel만
    Entity e1 = w.Create(), e2 = w.Create(), e3 = w.Create(), e4 = w.Create();
    w.Add<Position>(e1); w.Add<Velocity>(e1);
    w.Add<Position>(e2);
    w.Add<Position>(e3); w.Add<Velocity>(e3);
    w.Add<Velocity>(e4);

    int count = 0;
    std::vector<uint32_t> seen;
    w.Query<Position, Velocity>().Each([&](Entity e, Position&, Velocity&) {
        ++count; seen.push_back(e.index);
    });
    MYE_EXPECT(count == 2);   // e1, e3만 교집합
    bool hasE1 = false, hasE3 = false, hasOthers = false;
    for (uint32_t idx : seen) {
        if (idx == e1.index) hasE1 = true;
        else if (idx == e3.index) hasE3 = true;
        else hasOthers = true;
    }
    MYE_EXPECT(hasE1 && hasE3 && !hasOthers);

    // 미등록 컴포넌트가 섞인 뷰는 비어야 한다.
    auto v = w.Query<Position, Tag>();
    MYE_EXPECT(v.Empty());
    int tagCount = 0;
    v.Each([&](Entity, Position&, Tag&) { ++tagCount; });
    MYE_EXPECT(tagCount == 0);
}

// ---------------------------------------------------------------------------
// CommandBuffer — 지연 구조변경, Flush 타이밍
// ---------------------------------------------------------------------------
MYE_TEST(EcsCommandBufferDeferredFlush) {
    World w;
    CommandBuffer cb(w);

    // CreateDeferred는 즉시 유효 핸들 반환(같은 프레임 참조 연결 가능).
    Entity e = cb.CreateDeferred();
    MYE_EXPECT(w.Valid(e));

    // Add는 기록만 — Flush 전엔 컴포넌트 없음.
    cb.Add(e, Position{1.0f, 2.0f});
    MYE_EXPECT(!w.Has<Position>(e));
    MYE_EXPECT(!cb.Empty());

    cb.Flush();
    MYE_EXPECT(cb.Empty());
    Position* p = w.TryGet<Position>(e);
    MYE_EXPECT(p != nullptr && p->x == 1.0f && p->y == 2.0f);

    // Destroy도 지연 — Flush 시점에 반영.
    cb.Destroy(e);
    MYE_EXPECT(w.Valid(e));   // 아직 유효
    cb.Flush();
    MYE_EXPECT(!w.Valid(e));
}

MYE_TEST(EcsCommandBufferDynamicPath) {
    World w;
    // 동적(비템플릿) 등록 + AddDynamic — 플러그인/Lua 경로 모사.
    auto desc = ecs::MakeComponentTypeDesc<Tag>("Tag");
    w.RegisterComponent(desc);
    MYE_EXPECT(w.IsRegistered(Tag::kComponentTypeId));

    CommandBuffer cb(w);
    Entity e = cb.CreateDeferred();
    Tag value{99};
    cb.AddDynamic(e, desc, &value);
    cb.Flush();

    Tag* t = w.TryGet<Tag>(e);
    MYE_EXPECT(t != nullptr && t->id == 99);
}

// ---------------------------------------------------------------------------
// Transform — 로컬 행렬·부모-자식 월드행렬
// ---------------------------------------------------------------------------
MYE_TEST(TransformLocalMatrixTRS) {
    scene::LocalTransform lt;
    lt.position = Vec3{10, 20, 0};
    lt.scale = Vec3{2, 2, 2};
    Mat4 m = scene::ComputeLocalMatrix(lt);
    // 원점 → translation, 단위점(1,0,0) → scale 적용 후 translation.
    MYE_EXPECT(ApproxEqual(TransformPoint(Vec3{0, 0, 0}, m), Vec3{10, 20, 0}));
    MYE_EXPECT(ApproxEqual(TransformPoint(Vec3{1, 0, 0}, m), Vec3{12, 20, 0}));
}

MYE_TEST(TransformParentChildWorld) {
    World w;
    Entity parent = w.Create();
    Entity child  = w.Create();

    auto& pl = w.Add<scene::LocalTransform>(parent);
    pl.position = Vec3{100, 0, 0};
    w.Add<scene::WorldTransform>(parent);

    auto& cl = w.Add<scene::LocalTransform>(child);
    cl.position = Vec3{5, 0, 0};
    w.Add<scene::WorldTransform>(child);
    w.Add<scene::Parent>(child).parent = parent;

    scene::UpdateWorldTransforms(w);

    // child 월드 위치 = local(5) 를 parent(100) 로 승격 → 105.
    Mat4 cw = w.TryGet<scene::WorldTransform>(child)->matrix;
    MYE_EXPECT(ApproxEqual(TransformPoint(Vec3{0, 0, 0}, cw), Vec3{105, 0, 0}));

    // 부모 이동 후 재갱신 → 자식 월드도 따라감(dirty 전파).
    w.TryGet<scene::LocalTransform>(parent)->position = Vec3{200, 0, 0};
    w.TryGet<scene::LocalTransform>(parent)->dirty = true;
    scene::UpdateWorldTransforms(w);
    cw = w.TryGet<scene::WorldTransform>(child)->matrix;
    MYE_EXPECT(ApproxEqual(TransformPoint(Vec3{0, 0, 0}, cw), Vec3{205, 0, 0}));
}

MYE_TEST(TransformReparentKeepWorld) {
    World w;
    Entity a = w.Create();   // parent A at x=100
    Entity b = w.Create();   // parent B at x=500
    Entity c = w.Create();   // child, under A at local x=5 → world 105

    w.Add<scene::LocalTransform>(a).position = Vec3{100, 0, 0};
    w.Add<scene::WorldTransform>(a);
    w.Add<scene::LocalTransform>(b).position = Vec3{500, 0, 0};
    w.Add<scene::WorldTransform>(b);
    w.Add<scene::LocalTransform>(c).position = Vec3{5, 0, 0};
    w.Add<scene::WorldTransform>(c);
    w.Add<scene::Parent>(c).parent = a;
    w.Add<scene::Children>(a).list.push_back(c);
    scene::UpdateWorldTransforms(w);

    // A → B로 keepWorld reparent: 월드 위치(105)는 유지, 로컬은 B 기준으로 보정(105-500=-395).
    scene::ApplyReparent(w, c, b, /*keepWorld*/ true);
    scene::UpdateWorldTransforms(w);
    Mat4 cw = w.TryGet<scene::WorldTransform>(c)->matrix;
    MYE_EXPECT(ApproxEqual(TransformPoint(Vec3{0, 0, 0}, cw), Vec3{105, 0, 0}, 1e-2f));

    // Children 캐시 동기화: A에서 빠지고 B에 추가.
    MYE_EXPECT(w.TryGet<scene::Children>(a)->list.empty());
    auto& bl = w.TryGet<scene::Children>(b)->list;
    MYE_EXPECT(bl.size() == 1 && bl[0] == c);
    MYE_EXPECT(w.TryGet<scene::Parent>(c)->parent == b);
}

MYE_TEST(TransformReparentViaCommandBuffer) {
    // CommandBuffer::SetParent → Flush → scene reparent 후크 경유 반영(배선 검증).
    World w;
    Entity parent = w.Create();
    Entity child  = w.Create();
    w.Add<scene::LocalTransform>(parent).position = Vec3{100, 0, 0};
    w.Add<scene::WorldTransform>(parent);
    w.Add<scene::LocalTransform>(child);
    w.Add<scene::WorldTransform>(child);

    // 후크가 이 TU 정적 설치 또는 SceneModule 배선으로 활성화되어야 함 — 테스트는 명시 설치.
    ecs::CommandBuffer::SetReparentHook(&scene::ApplyReparent);

    CommandBuffer cb(w);
    cb.SetParent(child, parent, /*keepWorld*/ false);
    MYE_EXPECT(w.TryGet<scene::Parent>(child) == nullptr ||
               w.TryGet<scene::Parent>(child)->parent.IsNull());
    cb.Flush();
    MYE_EXPECT(w.TryGet<scene::Parent>(child) != nullptr);
    MYE_EXPECT(w.TryGet<scene::Parent>(child)->parent == parent);
}

// ---------------------------------------------------------------------------
// SystemScheduler — 페이즈 실행 · 토폴로지 순서 · 순환 폴백
// ---------------------------------------------------------------------------
MYE_TEST(SchedulerTopologicalOrder) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    scene::SystemScheduler sched(w, &bus);

    std::vector<std::string> order;

    scene::SystemDesc b;
    b.name = "B"; b.phase = scene::Phase::Update;
    b.runAfter = {"A"};
    b.fn = [&order](World&, CommandBuffer&, const TimeStep&) { order.push_back("B"); };
    scene::SystemDesc a;
    a.name = "A"; a.phase = scene::Phase::Update;
    a.runBefore = {"C"};
    a.fn = [&order](World&, CommandBuffer&, const TimeStep&) { order.push_back("A"); };
    scene::SystemDesc c;
    c.name = "C"; c.phase = scene::Phase::Update;
    c.runAfter = {"B"};
    c.fn = [&order](World&, CommandBuffer&, const TimeStep&) { order.push_back("C"); };

    // 등록 순서를 일부러 뒤섞어 토폴로지 정렬이 실제 순서를 결정함을 확인.
    sched.RegisterSystem(std::move(b));
    sched.RegisterSystem(std::move(a));
    sched.RegisterSystem(std::move(c));

    TimeStep ts;
    sched.RunPhase(scene::Phase::Update, ts);

    // 제약: A→B(B after A), A→C(A before C), B→C(C after B) ⇒ A, B, C.
    MYE_EXPECT(order.size() == 3);
    if (order.size() == 3) {
        MYE_EXPECT(order[0] == "A" && order[1] == "B" && order[2] == "C");
    }
}

MYE_TEST(SchedulerPhaseIsolation) {
    // 다른 페이즈 시스템은 해당 페이즈 RunPhase에서만 실행.
    World w;
    EventBus bus;
    scene::SystemScheduler sched(w, &bus);

    int inputRuns = 0, updateRuns = 0;
    scene::SystemDesc in;
    in.name = "InputSys"; in.phase = scene::Phase::Input;
    in.fn = [&](World&, CommandBuffer&, const TimeStep&) { ++inputRuns; };
    scene::SystemDesc up;
    up.name = "UpdateSys"; up.phase = scene::Phase::Update;
    up.fn = [&](World&, CommandBuffer&, const TimeStep&) { ++updateRuns; };
    sched.RegisterSystem(std::move(in));
    sched.RegisterSystem(std::move(up));

    TimeStep ts;
    sched.RunPhase(scene::Phase::Input, ts);
    MYE_EXPECT(inputRuns == 1 && updateRuns == 0);
    sched.RunPhase(scene::Phase::Update, ts);
    MYE_EXPECT(inputRuns == 1 && updateRuns == 1);
}

MYE_TEST(SchedulerCyclicFallback) {
    // 순환 제약(A after B, B after A)은 등록 순 폴백으로 모두 실행되어야 한다(데드락 금지).
    World w;
    EventBus bus;
    scene::SystemScheduler sched(w, &bus);

    int runs = 0;
    scene::SystemDesc a;
    a.name = "CycA"; a.phase = scene::Phase::Update; a.runAfter = {"CycB"};
    a.fn = [&](World&, CommandBuffer&, const TimeStep&) { ++runs; };
    scene::SystemDesc b;
    b.name = "CycB"; b.phase = scene::Phase::Update; b.runAfter = {"CycA"};
    b.fn = [&](World&, CommandBuffer&, const TimeStep&) { ++runs; };
    sched.RegisterSystem(std::move(a));
    sched.RegisterSystem(std::move(b));

    TimeStep ts;
    sched.RunPhase(scene::Phase::Update, ts);
    MYE_EXPECT(runs == 2);   // 순환이어도 폴백으로 둘 다 실행
}

MYE_TEST(SchedulerCommandBufferFlushAtBoundary) {
    // 시스템이 CB에 기록한 구조변경은 페이즈 경계(RunPhase 말미)에서 반영.
    World w;
    EventBus bus;
    scene::SystemScheduler sched(w, &bus);

    Entity target = w.Create();
    scene::SystemDesc s;
    s.name = "Spawner"; s.phase = scene::Phase::Update;
    s.fn = [target](World&, CommandBuffer& cb, const TimeStep&) {
        cb.Add(target, Position{7.0f, 0.0f});
    };
    sched.RegisterSystem(std::move(s));

    MYE_EXPECT(!w.Has<Position>(target));
    TimeStep ts;
    sched.RunPhase(scene::Phase::Update, ts);
    // 페이즈 경계 Flush 후 반영.
    Position* p = w.TryGet<Position>(target);
    MYE_EXPECT(p != nullptr && p->x == 7.0f);
}

// row-vector·TRS 정합 컴파일타임 고정(부모-자식 world=local*parentWorld 전제).
static_assert(sizeof(Entity) == 8, "Entity handle must be 64-bit (index:32 | generation:32)");
