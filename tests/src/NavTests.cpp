// NavTests.cpp — 길찾기(그리드 A*·높이·다리) + ecs::World::CreateWithId (M5-B / docs/03 §9·§1)
//
// 검증 불변식:
//   - A*: 장애물 우회·최단 경로·경로 없음·시작/목표 불통
//   - 다리 위/아래 분리 경로(level이 다른 노드 → 다른 경로)
//   - 비용 보정 콜백(진영 봉쇄로 우회, 통행 불가 판정)
//   - 대각선·코너 컷 방지
//   - 경로 스무딩(직선 구간 축약)
//   - 비동기 요청 완료(JobSystem 워커 계산 + Poll/콜백)
//   - CreateWithId: 파괴 후 같은 핸들 복원·기존 EntityRef 유효·generation 정합·중복 방지
#include "TestFramework.h"

#include "mye/core/Jobs.h"
#include "mye/ecs/World.h"
#include "mye/nav/Pathfinding.h"
#include "mye/tilemap/Tilemap.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace mye;
using mye::ecs::Entity;
using mye::ecs::World;
namespace nav = mye::nav;
namespace tmap = mye::tilemap;

// ---------------------------------------------------------------------------
// 테스트용 그리드 소스 — level 0 평면 그리드. '#'=벽, '.'=통행. 다리는 별도 소스로.
// ---------------------------------------------------------------------------
class GridNavSource final : public nav::INavSource {
public:
    GridNavSource(int w, int h, std::vector<int> blocked)
        : m_w(w), m_h(h), m_blocked(std::move(blocked)) {}

    bool IsWalkable(int32_t cx, int32_t cy, int32_t level) const override {
        if (level != 0) return false;
        if (cx < 0 || cy < 0 || cx >= m_w || cy >= m_h) return false;
        return m_blocked[cy * m_w + cx] == 0;
    }

private:
    int m_w, m_h;
    std::vector<int> m_blocked;   // 1 = 막힘
};

// ---------------------------------------------------------------------------
// A* 기본: 빈 그리드 최단 경로 길이(직교만).
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarStraightLine) {
    GridNavSource src(5, 1, std::vector<int>(5, 0));
    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = false;
    nav::PathResult r = nav::FindPath(src, {0, 0, 0}, {4, 0, 0}, p);
    MYE_EXPECT(r.status == nav::PathStatus::Success);
    MYE_EXPECT(r.points.size() == 5);        // (0..4,0)
    MYE_EXPECT(r.points.front().node == (nav::NavNode{0, 0, 0}));
    MYE_EXPECT(r.points.back().node == (nav::NavNode{4, 0, 0}));
}

// ---------------------------------------------------------------------------
// A* 장애물 우회: 세로 벽에 틈 하나. 경로는 반드시 틈을 지난다.
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarObstacleDetour) {
    // 5x5, 열 x=2 가 y=0..3 막힘, y=4 만 열림(틈).
    std::vector<int> b(25, 0);
    for (int y = 0; y < 4; ++y) b[y * 5 + 2] = 1;
    GridNavSource src(5, 5, b);

    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = false;
    nav::PathResult r = nav::FindPath(src, {0, 0, 0}, {4, 0, 0}, p);
    MYE_EXPECT(r.status == nav::PathStatus::Success);
    // 경로에 (2,4) 틈을 반드시 통과.
    bool through = false;
    for (auto& pt : r.points)
        if (pt.node.cellX == 2 && pt.node.cellY == 4) through = true;
    MYE_EXPECT(through);
    // 어떤 점도 벽을 밟지 않는다.
    for (auto& pt : r.points)
        MYE_EXPECT(!(pt.node.cellX == 2 && pt.node.cellY < 4));
}

// ---------------------------------------------------------------------------
// A* 경로 없음: 완전 봉쇄된 목표.
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarNoPath) {
    // 3x3, 가운데 열 전체 막힘 → 좌우 분리.
    std::vector<int> b(9, 0);
    b[0 * 3 + 1] = 1; b[1 * 3 + 1] = 1; b[2 * 3 + 1] = 1;
    GridNavSource src(3, 3, b);
    nav::NavQueryParams p; p.allowDiagonal = false;
    nav::PathResult r = nav::FindPath(src, {0, 0, 0}, {2, 0, 0}, p);
    MYE_EXPECT(r.status == nav::PathStatus::NoPath);
    MYE_EXPECT(r.points.empty());
}

// ---------------------------------------------------------------------------
// A* 시작/목표 불통.
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarInvalidEndpoints) {
    std::vector<int> b(9, 0);
    b[1 * 3 + 1] = 1;   // 중앙 막힘
    GridNavSource src(3, 3, b);
    nav::PathResult rGoal = nav::FindPath(src, {0, 0, 0}, {1, 1, 0});
    MYE_EXPECT(rGoal.status == nav::PathStatus::InvalidGoal);
    nav::PathResult rStart = nav::FindPath(src, {1, 1, 0}, {0, 0, 0});
    MYE_EXPECT(rStart.status == nav::PathStatus::InvalidStart);
}

// ---------------------------------------------------------------------------
// 대각 이동 + 코너 컷 방지.
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarDiagonalAndCornerCut) {
    // 빈 3x3에서 대각 허용이면 대각선으로 질러간다(3점: (0,0),(1,1),(2,2)).
    GridNavSource open(3, 3, std::vector<int>(9, 0));
    nav::NavQueryParams p; p.allowDiagonal = true; p.smoothPath = false; p.preventCornerCut = true;
    nav::PathResult r = nav::FindPath(open, {0, 0, 0}, {2, 2, 0}, p);
    MYE_EXPECT(r.status == nav::PathStatus::Success);
    MYE_EXPECT(r.points.size() == 3);

    // 코너 컷 방지: (1,0)·(0,1) 둘 다 막으면 (0,0)→(1,1) 대각 금지 → 우회 필요.
    std::vector<int> b(9, 0);
    b[0 * 3 + 1] = 1;   // (1,0)
    b[1 * 3 + 0] = 1;   // (0,1)
    GridNavSource corner(3, 3, b);
    nav::PathResult rc = nav::FindPath(corner, {0, 0, 0}, {2, 2, 0}, p);
    // (1,1)로 가는 유일한 대각 진입이 코너컷으로 막혔고 직교 이웃도 벽 → (0,0) 고립.
    MYE_EXPECT(rc.status == nav::PathStatus::NoPath);
}

// ---------------------------------------------------------------------------
// 경로 스무딩: 직선 구간이 시작·끝 두 점으로 축약.
// ---------------------------------------------------------------------------
MYE_TEST(NavAStarSmoothing) {
    GridNavSource src(6, 1, std::vector<int>(6, 0));
    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = true;
    nav::PathResult r = nav::FindPath(src, {0, 0, 0}, {5, 0, 0}, p);
    MYE_EXPECT(r.status == nav::PathStatus::Success);
    MYE_EXPECT(r.points.size() == 2);   // 직선 → 양 끝만
    MYE_EXPECT(r.points.front().node == (nav::NavNode{0, 0, 0}));
    MYE_EXPECT(r.points.back().node == (nav::NavNode{5, 0, 0}));
}

// ---------------------------------------------------------------------------
// 비용 보정 콜백: 특정 셀(진영 봉쇄)을 통행 불가로 만들어 우회 유도.
// ---------------------------------------------------------------------------
static int g_blockCellX = -1;
static float BlockColumnModifier(const nav::NavNode&, const nav::NavNode& to, void*) {
    return to.cellX == g_blockCellX ? -1.0f : 1.0f;   // 음수 = 통행 불가
}

MYE_TEST(NavCostModifierBlocking) {
    GridNavSource src(5, 3, std::vector<int>(15, 0));
    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = false;

    // 먼저 보정 없이 직선 경로(y=0 유지) 확인.
    nav::PathResult plain = nav::FindPath(src, {0, 0, 0}, {4, 0, 0}, p);
    MYE_EXPECT(plain.status == nav::PathStatus::Success);

    // x=2 열을 봉쇄. 단, 이건 콜백이 "진입" 셀을 막으므로 x=2 인 노드 전부 회피.
    g_blockCellX = 2;
    nav::CostModifier cm; cm.fn = &BlockColumnModifier; cm.user = nullptr;
    nav::PathResult r = nav::FindPath(src, {0, 0, 0}, {4, 0, 0}, p, cm);
    MYE_EXPECT(r.status == nav::PathStatus::NoPath);   // 5칸 폭 전체가 x=2에서 막힘
    g_blockCellX = -1;

    // 부분 봉쇄(x=2를 y=0,1만 벽으로) + 콜백 없이 → y=2로 우회.
    std::vector<int> b(15, 0);
    b[0 * 5 + 2] = 1; b[1 * 5 + 2] = 1;
    GridNavSource src2(5, 3, b);
    nav::PathResult r2 = nav::FindPath(src2, {0, 0, 0}, {4, 0, 0}, p);
    MYE_EXPECT(r2.status == nav::PathStatus::Success);
    for (auto& pt : r2.points)
        MYE_EXPECT(!(pt.node.cellX == 2 && pt.node.cellY < 2));
}

// ---------------------------------------------------------------------------
// 다리 위/아래 분리 경로 — TilemapNavSource로 실제 다중 컬럼 셀 질의.
//
// 구성: y=0 행에 지면(level 0) 통행로. 셀 (2,0)에 지면(level 0)과 다리(level 2)가 공존.
//   지면 경로는 level 0을 지나고, 다리 경로는 level 2를 지난다 → 서로 다른 노드.
// ---------------------------------------------------------------------------
MYE_TEST(NavBridgeSeparateLevels) {
    tmap::TilemapWorld world;
    tmap::TileLayer& ground = world.GetOrCreateLayer(0);

    auto putGround = [&](int x, int y, int level) {
        tmap::TileColumn c;
        c.tile = 1; c.baseHeightLevel = level; c.flags = 0;   // 통행 가능(솔리드 아님)
        ground.SetTile(Vec2i{x, y}, c);
    };
    auto addBridge = [&](int x, int y, int level) {
        tmap::TileColumn c;
        c.tile = 2; c.baseHeightLevel = level;
        c.flags = tmap::ColumnFlags::Bridge;   // 다리 컬럼(솔리드 아님 → 통행 가능)
        ground.AddBridge(Vec2i{x, y}, c);
    };

    // 지면 통행로 y=0, x=0..4 (level 0).
    for (int x = 0; x <= 4; ++x) putGround(x, 0, 0);
    // (2,0) 위에 다리 층(level 2) 추가 + 다리로 이어지는 통로도 level 2 로 x=1..3.
    // (다리 위 경로가 성립하려면 다리 컬럼들이 연속이어야 함)
    addBridge(1, 0, 2);
    addBridge(2, 0, 2);
    addBridge(3, 0, 2);

    nav::TilemapNavSource src(world);

    // 지면 경로: level 0 유지, (2,0,0)을 지난다.
    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = false; p.maxLevelStep = 0;
    nav::PathResult ground01 = nav::FindPath(src, {0, 0, 0}, {4, 0, 0}, p);
    MYE_EXPECT(ground01.status == nav::PathStatus::Success);
    bool onGroundMid = false;
    for (auto& pt : ground01.points)
        if (pt.node == (nav::NavNode{2, 0, 0})) onGroundMid = true;
    MYE_EXPECT(onGroundMid);

    // 다리 경로: level 2 노드만으로 (1,0,2)→(3,0,2).
    nav::PathResult bridge = nav::FindPath(src, {1, 0, 2}, {3, 0, 2}, p);
    MYE_EXPECT(bridge.status == nav::PathStatus::Success);
    for (auto& pt : bridge.points)
        MYE_EXPECT(pt.node.level == 2);   // 다리 위 경로는 전부 level 2

    // 두 경로는 다른 노드 집합(같은 셀 (2,0)이라도 level이 달라 분리).
    MYE_EXPECT(bridge.points.front().node.level != ground01.points.front().node.level);
}

// ---------------------------------------------------------------------------
// 비동기 요청: JobSystem 워커에서 계산 → Poll 완료 회수.
// ---------------------------------------------------------------------------
MYE_TEST(NavAsyncRequestPoll) {
    GridNavSource src(8, 1, std::vector<int>(8, 0));
    JobSystem jobs;
    nav::NavSystem sys(src, &jobs);
    nav::NavQueryParams p; p.allowDiagonal = false; p.smoothPath = false;
    sys.SetParams(p);

    nav::NavRequestId id = sys.RequestPath({0, 0, 0}, {7, 0, 0});
    MYE_EXPECT(id != nav::kInvalidRequest);

    // 완료 폴링(busy-wait 짧게 — 워커가 곧 완료).
    nav::PathResult out;
    bool done = false;
    for (int i = 0; i < 2000 && !done; ++i) {
        if (sys.Poll(id, out)) { done = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MYE_EXPECT(done);
    MYE_EXPECT(out.status == nav::PathStatus::Success);
    MYE_EXPECT(out.points.size() == 8);

    // 소비 후 재폴은 false(1회 소비).
    nav::PathResult again;
    MYE_EXPECT(!sys.Poll(id, again));
}

// ---------------------------------------------------------------------------
// 비동기 콜백 디스패치 — 완료 시 콜백이 메인(=DispatchCompleted 호출) 스레드에서 실행.
// ---------------------------------------------------------------------------
MYE_TEST(NavAsyncCallback) {
    GridNavSource src(4, 1, std::vector<int>(4, 0));
    JobSystem jobs;
    nav::NavSystem sys(src, &jobs);

    std::atomic<int> called{0};
    nav::PathStatus gotStatus = nav::PathStatus::Pending;
    nav::NavRequestId gotId = nav::kInvalidRequest;

    nav::NavRequestId id = sys.RequestPath({0, 0, 0}, {3, 0, 0},
        [&](nav::NavRequestId rid, const nav::PathResult& r) {
            gotId = rid; gotStatus = r.status; called.fetch_add(1);
        });

    // 완료될 때까지 대기 후 디스패치.
    for (int i = 0; i < 2000; ++i) {
        if (sys.IsComplete(id)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sys.DispatchCompleted();
    MYE_EXPECT(called.load() == 1);
    MYE_EXPECT(gotId == id);
    MYE_EXPECT(gotStatus == nav::PathStatus::Success);

    // 중복 디스패치 방지.
    sys.DispatchCompleted();
    MYE_EXPECT(called.load() == 1);
}

// ---------------------------------------------------------------------------
// 동기 폴백: JobSystem 없이 요청하면 즉시 완료.
// ---------------------------------------------------------------------------
MYE_TEST(NavSyncFallback) {
    GridNavSource src(4, 1, std::vector<int>(4, 0));
    nav::NavSystem sys(src, nullptr);
    nav::NavRequestId id = sys.RequestPath({0, 0, 0}, {3, 0, 0});
    MYE_EXPECT(sys.IsComplete(id));
    nav::PathResult out;
    MYE_EXPECT(sys.Poll(id, out));
    MYE_EXPECT(out.status == nav::PathStatus::Success);
}

// ---------------------------------------------------------------------------
// ecs::World::CreateWithId — 파괴 후 같은 핸들 복원.
// ---------------------------------------------------------------------------
struct NavTestTag { MYE_COMPONENT(NavTestTag); int value = 0; };

MYE_TEST(WorldCreateWithIdRestore) {
    World w;
    Entity a = w.Create();
    Entity b = w.Create();
    w.Add<NavTestTag>(b).value = 42;
    const uint32_t bIdx = b.index;
    const uint32_t bGen = b.generation;

    // b 파괴 → 핸들 무효.
    w.Destroy(b);
    MYE_EXPECT(!w.Valid(b));

    // 원래 핸들 그대로 복원.
    Entity restored = w.CreateWithId(bIdx, bGen);
    MYE_EXPECT(restored.index == bIdx);
    MYE_EXPECT(restored.generation == bGen);
    MYE_EXPECT(restored == b);          // 기존 EntityRef(b)가 다시 유효
    MYE_EXPECT(w.Valid(b));
    MYE_EXPECT(w.Valid(restored));

    // 컴포넌트는 파괴 시 제거됐으므로 새로 붙일 수 있고 값 정합.
    MYE_EXPECT(!w.Has<NavTestTag>(restored));
    w.Add<NavTestTag>(restored).value = 7;
    MYE_EXPECT(w.TryGet<NavTestTag>(restored)->value == 7);

    // a 는 영향 없음.
    MYE_EXPECT(w.Valid(a));
}

// 파괴 → 슬롯 재사용 → 재파괴 → 옛 핸들 복원 시도. 재사용으로 세대가 전진했으므로 옛 세대로의
//   되감기는 거부되고(단조성 보존) 서로 다른 논리 엔티티가 한 핸들에서 충돌하지 않아야 한다.
MYE_TEST(WorldCreateWithIdMonotonicAfterReuse) {
    World w;
    Entity b = w.Create();
    const uint32_t bIdx = b.index;
    const uint32_t bGen = b.generation;

    w.Destroy(b);                      // 슬롯 freelist 로, 세대 +1
    Entity reused = w.Create();        // 같은 슬롯 재사용(세대 유지) — 다른 논리 엔티티
    MYE_EXPECT(reused.index == bIdx);
    MYE_EXPECT(reused.generation != bGen);
    w.Destroy(reused);                 // 다시 파괴, 세대 또 +1

    // 옛 핸들(bGen) 복원 시도 → 되감기 거부. 반환 핸들은 옛 핸들과 달라야 한다(폴백).
    Entity restored = w.CreateWithId(bIdx, bGen);
    MYE_EXPECT(restored.generation != bGen);       // 되감지 않음(단조성)
    MYE_EXPECT(!w.Valid(b));                        // 옛 핸들은 여전히 무효(충돌 없음)
    MYE_EXPECT(w.Valid(restored));

    // 이후 Create 가 옛 핸들 b 와 같은 (index,gen) 을 재발급하지 않는다(충돌 방지).
    w.Destroy(restored);
    Entity next = w.Create();
    MYE_EXPECT(!(next == b));
}

// ---------------------------------------------------------------------------
// CreateWithId 중복 방지 — 살아있는 핸들 재생성 실패.
// ---------------------------------------------------------------------------
MYE_TEST(WorldCreateWithIdDuplicateRejected) {
    World w;
    Entity a = w.Create();
    // 살아있는 index로 CreateWithId → 실패(Null).
    Entity dup = w.CreateWithId(a.index, a.generation);
    MYE_EXPECT(dup.IsNull());
    MYE_EXPECT(w.Valid(a));   // 원본 여전히 유효

    // 다른 generation으로도 살아있는 슬롯은 거부.
    Entity dup2 = w.CreateWithId(a.index, a.generation + 100);
    MYE_EXPECT(dup2.IsNull());

    // generation 0(null 세대)은 거부.
    Entity nullGen = w.CreateWithId(999, 0);
    MYE_EXPECT(nullGen.IsNull());
}

// ---------------------------------------------------------------------------
// CreateWithId 세대 테이블 확장 — 범위 밖 index로 복원 시 사이 슬롯 freelist 정합.
// ---------------------------------------------------------------------------
MYE_TEST(WorldCreateWithIdExtendTable) {
    World w;
    // 아직 존재하지 않는 index 5, generation 3 로 생성.
    Entity e = w.CreateWithId(5, 3);
    MYE_EXPECT(e.index == 5);
    MYE_EXPECT(e.generation == 3);
    MYE_EXPECT(w.Valid(e));

    // 사이 슬롯(0..4)은 freelist에 등록됐어야 함 → 이후 Create가 그것들을 재사용.
    // (5개 Create가 모두 index < 5 를 반환하고, 6번째는 새 index 6.)
    std::vector<uint32_t> got;
    for (int i = 0; i < 5; ++i) got.push_back(w.Create().index);
    for (uint32_t idx : got) MYE_EXPECT(idx < 5);
    // 6번째는 새 슬롯(6, index 5는 이미 점유).
    Entity sixth = w.Create();
    MYE_EXPECT(sixth.index == 6);

    // 복원한 e 는 여전히 유효(사이 Create들이 침범하지 않음).
    MYE_EXPECT(w.Valid(e));
}

// ---------------------------------------------------------------------------
// CreateWithId 후 Destroy → generation 단조 증가(정합).
// ---------------------------------------------------------------------------
MYE_TEST(WorldCreateWithIdGenerationConsistency) {
    World w;
    Entity e = w.Create();
    const uint32_t idx = e.index;
    const uint32_t gen = e.generation;
    w.Destroy(e);
    Entity restored = w.CreateWithId(idx, gen);
    MYE_EXPECT(w.Valid(restored));
    // 다시 파괴하면 세대가 올라가고(무효화), 옛 핸들은 죽는다.
    w.Destroy(restored);
    MYE_EXPECT(!w.Valid(restored));
    MYE_EXPECT(!w.Valid(e));
    // 슬롯 재사용 시 새 세대는 restored.generation 과 다르다.
    Entity reuse = w.Create();
    if (reuse.index == idx) MYE_EXPECT(reuse.generation != gen);
}
