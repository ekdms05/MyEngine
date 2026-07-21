// PhysTests.cpp — 충돌·물리(경량 2D) 단위 테스트 (M2-B / docs/03 §8)
//
// 검증 불변식:
//   - AABB/원 겹침·MTV 정확
//   - 공간 해시 브로드페이즈가 느린 전수 대조와 동일한 후보 쌍 산출
//   - move-and-slide 벽 슬라이드(정면 벽에 막히고 접선 이동은 유지)
//   - floorLevel 충돌 필터(다리 위/아래 비충돌)
//   - 트리거 Enter/Exit 이벤트 발행 순서·중복 없음
//   - 레이캐스트 최소
#include "TestFramework.h"

#include "mye/core/Events.h"
#include "mye/core/Time.h"
#include "mye/ecs/View.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/phys/SpatialHash.h"
#include "mye/scene/Transform.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace mye;
using mye::ecs::Entity;
using mye::ecs::World;
using mye::phys::Collider2D;
using mye::phys::KinematicBody2D;
using mye::phys::PhysicsWorld2D;
using mye::phys::Shape2D;
using mye::phys::ShapeKind;
using mye::scene::FloorLevel;
using mye::scene::LocalTransform;
using mye::scene::WorldTransform;

namespace {

// 엔티티 생성 헬퍼: 위치 + 콜라이더.
Entity MakeBody(World& w, Vec2 pos, Shape2D shape, bool trigger = false,
                uint32_t triggerId = 0) {
    Entity e = w.Create();
    auto& lt = w.Add<LocalTransform>(e);
    lt.position = Vec3{pos.x, pos.y, 0};
    auto& wt = w.Add<WorldTransform>(e);
    wt.matrix.m[3][0] = pos.x;
    wt.matrix.m[3][1] = pos.y;
    auto& col = w.Add<Collider2D>(e);
    col.shape = shape;
    col.isTrigger = trigger;
    col.triggerId = triggerId;
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// 셰이프 겹침·MTV
// ---------------------------------------------------------------------------
MYE_TEST(PhysAabbOverlapAndMtv) {
    using namespace mye::phys;
    Shape2D a = Shape2D::MakeBox(1.0f, 1.0f);
    Shape2D b = Shape2D::MakeBox(1.0f, 1.0f);

    // 완전 분리.
    MYE_EXPECT(!Overlap(a, Vec2{0, 0}, b, Vec2{3, 0}));
    // x축으로 0.5 침투(중심 거리 1.5, half합 2).
    MYE_EXPECT(Overlap(a, Vec2{0, 0}, b, Vec2{1.5f, 0}));
    auto mtv = ResolveMTV(a, Vec2{0, 0}, b, Vec2{1.5f, 0});
    MYE_EXPECT(mtv.has_value());
    if (mtv) {
        // b를 +x로 0.5 밀어야 분리.
        MYE_EXPECT_NEAR(mtv->x, 0.5f, 1e-4f);
        MYE_EXPECT_NEAR(mtv->y, 0.0f, 1e-4f);
    }
}

MYE_TEST(PhysCircleOverlapAndMtv) {
    using namespace mye::phys;
    Shape2D a = Shape2D::MakeCircle(1.0f);
    Shape2D b = Shape2D::MakeCircle(1.0f);
    MYE_EXPECT(!Overlap(a, Vec2{0, 0}, b, Vec2{2.5f, 0}));
    MYE_EXPECT(Overlap(a, Vec2{0, 0}, b, Vec2{1.5f, 0}));   // dist 1.5 < r합 2
    auto mtv = ResolveMTV(a, Vec2{0, 0}, b, Vec2{1.5f, 0});
    MYE_EXPECT(mtv.has_value());
    if (mtv) MYE_EXPECT_NEAR(mtv->x, 0.5f, 1e-4f);   // pen = 2 - 1.5
}

MYE_TEST(PhysCircleAabbMixed) {
    using namespace mye::phys;
    Shape2D circ = Shape2D::MakeCircle(1.0f);
    Shape2D box = Shape2D::MakeBox(1.0f, 1.0f);
    // 원(0,0) vs box(1.5,0): 최근접점 x=1 → 원 표면까지 0.5 침투.
    MYE_EXPECT(Overlap(circ, Vec2{0, 0}, box, Vec2{1.5f, 0}));
    MYE_EXPECT(!Overlap(circ, Vec2{0, 0}, box, Vec2{2.5f, 0}));
    // 대각선 분리(모서리).
    MYE_EXPECT(!Overlap(circ, Vec2{0, 0}, box, Vec2{2.0f, 2.0f}));
}

// ---------------------------------------------------------------------------
// 공간 해시 브로드페이즈 — 느린 전수와 대조
// ---------------------------------------------------------------------------
MYE_TEST(PhysSpatialHashMatchesBruteForce) {
    using namespace mye::phys;

    struct Box { Vec2 min, max; };
    std::vector<Box> boxes;
    // 결정적 유사난수 배치.
    uint32_t seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) & 0xFFFF; };
    for (int i = 0; i < 60; ++i) {
        float x = static_cast<float>(rnd() % 200) * 0.1f;
        float y = static_cast<float>(rnd() % 200) * 0.1f;
        float hw = 0.3f + static_cast<float>(rnd() % 10) * 0.05f;
        boxes.push_back(Box{Vec2{x - hw, y - hw}, Vec2{x + hw, y + hw}});
    }

    // 브루트포스 겹침 쌍(정답).
    auto overlaps = [](const Box& a, const Box& b) {
        return !(a.max.x < b.min.x || b.max.x < a.min.x ||
                 a.max.y < b.min.y || b.max.y < a.min.y);
    };
    std::vector<uint64_t> brute;
    for (uint32_t i = 0; i < boxes.size(); ++i)
        for (uint32_t j = i + 1; j < boxes.size(); ++j)
            if (overlaps(boxes[i], boxes[j]))
                brute.push_back((static_cast<uint64_t>(i) << 32) | j);
    std::sort(brute.begin(), brute.end());

    // 공간 해시 결과.
    SpatialHash hash(1.0f);
    for (uint32_t i = 0; i < boxes.size(); ++i) {
        BroadphaseItem it;
        it.entity = Entity{i + 1, 1};
        it.min = boxes[i].min; it.max = boxes[i].max; it.userIndex = i;
        hash.Insert(it);
    }
    std::vector<uint64_t> got;
    hash.QueryPairs([&](uint32_t i, uint32_t j) {
        got.push_back((static_cast<uint64_t>(i) << 32) | j);
    });
    std::sort(got.begin(), got.end());

    // 공간 해시는 AABB 실제 겹침을 사전필터하므로 브루트포스와 정확히 일치해야 한다.
    MYE_EXPECT(got.size() == brute.size());
    MYE_EXPECT(got == brute);
    // 중복 없음.
    MYE_EXPECT(std::unique(got.begin(), got.end()) == got.end());
}

// ---------------------------------------------------------------------------
// move-and-slide — 정면 벽 차단 + 접선 슬라이드
// ---------------------------------------------------------------------------
MYE_TEST(PhysMoveAndSlideWall) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    // 벽: x=2에 큰 세로 벽(정적, 콜라이더만).
    Entity wall = MakeBody(w, Vec2{2.0f, 0.0f}, Shape2D::MakeBox(0.5f, 5.0f));
    (void)wall;

    // 이동체: x=0에서 오른쪽으로 이동, half 0.5.
    Entity mover = MakeBody(w, Vec2{0.0f, 0.0f}, Shape2D::MakeBox(0.5f, 0.5f));
    auto& kb = w.Add<KinematicBody2D>(mover);
    kb.velocity = Vec2{10.0f, 0.0f};   // 벽을 향해 빠르게

    // 여러 스텝: 벽면(x=1.5-0.5=1.0)에 막혀야 한다(mover 중심 ≤ 1.0).
    for (int i = 0; i < 20; ++i) phys.Step(w, &bus, 1.0f / 60.0f);

    Vec2 mp{w.TryGet<LocalTransform>(mover)->position.x, w.TryGet<LocalTransform>(mover)->position.y};
    // 벽 좌면 = 2 - 0.5 = 1.5; mover 우면이 거기 닿으면 중심 = 1.5 - 0.5 = 1.0.
    MYE_EXPECT(mp.x <= 1.0f + 0.05f);
    MYE_EXPECT(w.TryGet<KinematicBody2D>(mover)->hitWall);

    // 접선(y) 이동은 벽이 막지 않는다: 위로 이동 시 자유.
    Entity mover2 = MakeBody(w, Vec2{-3.0f, 0.0f}, Shape2D::MakeBox(0.5f, 0.5f));
    auto& kb2 = w.Add<KinematicBody2D>(mover2);
    kb2.velocity = Vec2{0.0f, 5.0f};
    for (int i = 0; i < 10; ++i) phys.Step(w, &bus, 1.0f / 60.0f);
    MYE_EXPECT(w.TryGet<LocalTransform>(mover2)->position.y > 0.5f);
}

// ---------------------------------------------------------------------------
// floorLevel 충돌 필터 — 다리 위/아래 비충돌
// ---------------------------------------------------------------------------
MYE_TEST(PhysFloorLevelFilter) {
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    // 같은 XY에 두 콜라이더: A는 floor 0, B는 floor 1(다리 위). 충돌하면 안 됨.
    Entity a = MakeBody(w, Vec2{0, 0}, Shape2D::MakeBox(0.5f, 0.5f));
    w.Add<FloorLevel>(a).level = 0;
    auto& akb = w.Add<KinematicBody2D>(a);
    akb.velocity = Vec2{5.0f, 0.0f};   // B 쪽으로 이동

    Entity b = MakeBody(w, Vec2{0.6f, 0.0f}, Shape2D::MakeBox(0.5f, 0.5f));
    w.Add<FloorLevel>(b).level = 1;    // 다리 위

    // A는 B를 무시하고 통과해야 한다(다른 층).
    for (int i = 0; i < 20; ++i) phys.Step(w, &bus, 1.0f / 60.0f);
    MYE_EXPECT(w.TryGet<LocalTransform>(a)->position.x > 1.0f);   // B 위치를 지나쳐 이동
    MYE_EXPECT(!w.TryGet<KinematicBody2D>(a)->hitWall);

    // 대조: 같은 층이면 막힌다.
    World w2;
    EventBus bus2; w2.SetEventBus(&bus2);
    PhysicsWorld2D phys2(1.0f);
    Entity c = MakeBody(w2, Vec2{0, 0}, Shape2D::MakeBox(0.5f, 0.5f));
    w2.Add<FloorLevel>(c).level = 0;
    auto& ckb = w2.Add<KinematicBody2D>(c);
    ckb.velocity = Vec2{5.0f, 0.0f};
    Entity d = MakeBody(w2, Vec2{1.5f, 0.0f}, Shape2D::MakeBox(0.5f, 0.5f));
    w2.Add<FloorLevel>(d).level = 0;   // 같은 층 → 막힘
    for (int i = 0; i < 30; ++i) phys2.Step(w2, &bus2, 1.0f / 60.0f);
    MYE_EXPECT(w2.TryGet<LocalTransform>(c)->position.x <= 0.55f);  // d 좌면(1.0)에 막힘
    MYE_EXPECT(w2.TryGet<KinematicBody2D>(c)->hitWall);
}

// ---------------------------------------------------------------------------
// 트리거 Enter/Exit — 발행 순서·중복 없음
// ---------------------------------------------------------------------------
MYE_TEST(PhysTriggerEnterExitEvents) {
    using namespace mye::phys;
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    std::vector<std::string> log;   // "enter:<id>" / "exit:<id>"
    int enterCount = 0, exitCount = 0;
    bus.Subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent& e) {
        log.push_back("enter:" + std::to_string(e.triggerId));
        ++enterCount; return false;
    });
    bus.Subscribe<TriggerExitEvent>([&](const TriggerExitEvent& e) {
        log.push_back("exit:" + std::to_string(e.triggerId));
        ++exitCount; return false;
    });

    // 트리거 영역: x=[1.5,2.5] 근방(중심 2, half 0.5).
    Entity trig = MakeBody(w, Vec2{2.0f, 0.0f}, Shape2D::MakeBox(0.5f, 0.5f),
                           /*trigger*/ true, /*id*/ 42);

    // 이동체가 왼→오른으로 트리거를 통과.
    Entity mover = MakeBody(w, Vec2{0.0f, 0.0f}, Shape2D::MakeBox(0.3f, 0.3f));
    auto& kb = w.Add<KinematicBody2D>(mover);
    kb.velocity = Vec2{3.0f, 0.0f};

    // 처음엔 겹침 없음 → 이벤트 없음.
    phys.Step(w, &bus, 1.0f / 60.0f);
    MYE_EXPECT(enterCount == 0 && exitCount == 0);

    // 통과 시뮬레이션(충분히 많은 스텝).
    for (int i = 0; i < 120; ++i) phys.Step(w, &bus, 1.0f / 60.0f);

    // 정확히 1회 Enter, 1회 Exit — 중복 없음.
    MYE_EXPECT(enterCount == 1);
    MYE_EXPECT(exitCount == 1);
    // 순서: Enter가 Exit보다 먼저.
    MYE_EXPECT(log.size() == 2);
    if (log.size() == 2) {
        MYE_EXPECT(log[0] == "enter:42");
        MYE_EXPECT(log[1] == "exit:42");
    }
    (void)trig;
}

MYE_TEST(PhysTriggerNoDuplicateWhileStaying) {
    using namespace mye::phys;
    World w;
    EventBus bus;
    w.SetEventBus(&bus);
    PhysicsWorld2D phys(1.0f);

    int enterCount = 0, exitCount = 0;
    bus.Subscribe<TriggerEnterEvent>([&](const TriggerEnterEvent&) { ++enterCount; return false; });
    bus.Subscribe<TriggerExitEvent>([&](const TriggerExitEvent&) { ++exitCount; return false; });

    // 트리거와 정지한 겹치는 바디 — 여러 스텝 머물러도 Enter 1회, Exit 0회.
    Entity trig = MakeBody(w, Vec2{0, 0}, Shape2D::MakeBox(1.0f, 1.0f), true, 7);
    Entity body = MakeBody(w, Vec2{0.2f, 0.0f}, Shape2D::MakeBox(0.3f, 0.3f));
    w.Add<KinematicBody2D>(body);   // 속도 0(정지)
    (void)trig;

    for (int i = 0; i < 10; ++i) phys.Step(w, &bus, 1.0f / 60.0f);
    MYE_EXPECT(enterCount == 1);   // 진입 1회
    MYE_EXPECT(exitCount == 0);    // 머무는 동안 재발행 없음(Stay)
}

// ---------------------------------------------------------------------------
// 레이캐스트 최소
// ---------------------------------------------------------------------------
MYE_TEST(PhysRaycastMinimal) {
    World w;
    PhysicsWorld2D phys(1.0f);

    Entity box = MakeBody(w, Vec2{5.0f, 0.0f}, Shape2D::MakeBox(1.0f, 1.0f));
    (void)box;
    // 원점에서 +x로 레이 → box 좌면(x=4)에 히트.
    auto hit = phys.Raycast(w, Vec2{0, 0}, Vec2{1, 0}, 100.0f, 0xFFFFFFFFu, 0);
    MYE_EXPECT(hit.has_value());
    if (hit) {
        MYE_EXPECT(hit->entity == box);
        MYE_EXPECT_NEAR(hit->distance, 4.0f, 1e-2f);
        MYE_EXPECT_NEAR(hit->point.x, 4.0f, 1e-2f);
    }
    // 반대 방향 → 미스.
    auto miss = phys.Raycast(w, Vec2{0, 0}, Vec2{-1, 0}, 100.0f, 0xFFFFFFFFu, 0);
    MYE_EXPECT(!miss.has_value());
}

// ---------------------------------------------------------------------------
// OverlapArea + floorLevel 필터
// ---------------------------------------------------------------------------
MYE_TEST(PhysOverlapAreaFloorFilter) {
    using namespace mye::phys;
    World w;
    PhysicsWorld2D phys(1.0f);

    Entity g = MakeBody(w, Vec2{0, 0}, Shape2D::MakeBox(0.5f, 0.5f));
    w.Add<FloorLevel>(g).level = 0;
    Entity up = MakeBody(w, Vec2{0, 0}, Shape2D::MakeBox(0.5f, 0.5f));
    w.Add<FloorLevel>(up).level = 1;

    Entity out[8];
    // floor 0 질의 → g만.
    uint32_t n0 = phys.OverlapArea(w, Shape2D::MakeBox(0.5f, 0.5f), Vec2{0, 0}, 0, out);
    MYE_EXPECT(n0 == 1 && out[0] == g);
    // floor 1 질의 → up만.
    uint32_t n1 = phys.OverlapArea(w, Shape2D::MakeBox(0.5f, 0.5f), Vec2{0, 0}, 1, out);
    MYE_EXPECT(n1 == 1 && out[0] == up);
}
