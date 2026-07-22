// mye/nav/Pathfinding.h — 그리드 A* 길찾기(높이·다리 인지) (docs/03 §9)
//
// 노드 = (cellX, cellY, level) 3튜플. 다리 위 경로와 아래 경로는 서로 다른 level을 가지므로
// 자연스럽게 분리된 노드가 된다(다리 통과 케이스). 경사·계단 셀이 레벨 간 엣지를 만든다.
//
// 통행성·높이·컬럼 질의는 tilemap::TilemapWorld(ColumnAtLevel/ColumnsAt/SampleHeight)를 통해
// 수행한다 — 본 모듈은 타일 데이터 모델을 소유하지 않고 소비만 한다(03 규약: 데이터모델은 Tilemap).
//
// 알고리즘: 그리드 A*(4/8방향, 코너 컷 금지 옵션). 비용 = 이동거리 × moveCost × 콜백 보정.
//   setCostModifier로 게임 규칙(지역 위험도·진영 통행 불가 등)을 코어 수정 없이 주입한다(확장 포인트).
//   경로 후처리로 직선 구간 스무딩(문자열 당기기 간이판, 선택).
//
// 비동기: RequestPath → 01 JobSystem 워커에서 계산 → Poll/콜백으로 결과 회수(다음 프레임).
//   요청·결과는 값 스냅샷으로 오가며(스레드 경계 안전), 계산 중 TilemapWorld는 읽기만 한다.
#pragma once

#include "mye/core/Math.h"
#include "mye/tilemap/Tilemap.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace mye { class JobSystem; }

namespace mye::nav {

// ---------------------------------------------------------------------------
// 노드·경로 포인트
// ---------------------------------------------------------------------------
// 내비 그리드 노드: 셀 좌표 + 높이 층(다리 위/아래 분리 키). tilemap의 baseHeightLevel과 정합.
struct NavNode {
    int32_t cellX = 0;
    int32_t cellY = 0;
    int32_t level = 0;    // 통행 컬럼의 baseHeightLevel(다리/지면 선택 키)
    constexpr bool operator==(const NavNode&) const = default;
};

// 경로 결과 한 점: 노드 + 월드 좌표(셀 중심·경사 보간 높이 포함, 이동 시스템·렌더가 소비).
struct PathPoint {
    NavNode node;
    Vec2    world{0, 0};   // 셀 중심의 월드 XY(worldX = cellX+0.5, worldY = CellToWorldY 규약)
    int8_t  level = 0;     // node.level 편의 복제(FloorLevel 배선용)
};

// ---------------------------------------------------------------------------
// 에이전트·비용 파라미터
// ---------------------------------------------------------------------------
// 비용 보정 콜백: from→to 이동에 곱할 배율을 반환(>0). 진입 불가는 통행성 콜백/타일 속성으로.
//   반환 <= 0 이면 "통행 불가"로 간주(엣지 제거) — 진영별 봉쇄 등에 사용.
using CostModifierFn = float (*)(const NavNode& from, const NavNode& to, void* user);

// A* 옵션.
struct NavQueryParams {
    bool  allowDiagonal   = true;    // 8방향 허용
    bool  preventCornerCut = true;   // 대각 이동 시 양쪽 직교 셀이 막혀 있으면 금지(코너 컷 방지)
    bool  smoothPath      = true;    // 경로 후처리(문자열 당기기 간이 스무딩)
    int   maxLevelStep    = 1;       // 인접 셀 간 허용 최대 층 차(경사/계단 전이). 0 = 평지만
    int   maxExpandedNodes = 200000; // 안전 상한(무한 탐색·거대 맵 방지)
};

// ---------------------------------------------------------------------------
// 통행성 소스 — 타일 통행성/이동비용 질의 표면.
// 기본 구현(TilemapNavSource)은 TilemapWorld를 감싼다. 헤드리스 테스트는 자체 소스를 주입한다.
// ---------------------------------------------------------------------------
class INavSource {
public:
    virtual ~INavSource() = default;

    // (cellX, cellY, level) 노드가 통행 가능한가(솔리드가 아니고 컬럼이 존재).
    virtual bool IsWalkable(int32_t cellX, int32_t cellY, int32_t level) const = 0;

    // 통행 가능 노드의 이동 비용 배율(>= 1 권장, 늪·모래 등). 기본 1.
    virtual float MoveCost(int32_t cellX, int32_t cellY, int32_t level) const {
        (void)cellX; (void)cellY; (void)level; return 1.0f;
    }

    // 노드의 셀 중심 월드 좌표(경사 보간 높이). 기본: 셀 중심 XY + level→worldY 오프셋.
    virtual Vec2 NodeWorld(int32_t cellX, int32_t cellY, int32_t level) const {
        return Vec2{static_cast<float>(cellX) + 0.5f,
                    tilemap::CellToWorldY(cellY) + 0.5f +
                        tilemap::HeightLevelToWorldY(level)};
    }
};

// TilemapWorld를 감싸는 기본 통행성 소스.
class TilemapNavSource final : public INavSource {
public:
    explicit TilemapNavSource(const tilemap::TilemapWorld& world) : m_world(&world) {}

    bool  IsWalkable(int32_t cellX, int32_t cellY, int32_t level) const override;
    float MoveCost(int32_t cellX, int32_t cellY, int32_t level) const override;
    Vec2  NodeWorld(int32_t cellX, int32_t cellY, int32_t level) const override;

private:
    const tilemap::TilemapWorld* m_world;
};

// ---------------------------------------------------------------------------
// 경로 계산 결과
// ---------------------------------------------------------------------------
enum class PathStatus : uint8_t {
    Pending = 0,   // 비동기 계산 중(아직 결과 없음)
    Success,       // 경로 발견
    NoPath,        // 통행 가능한 경로가 없음
    InvalidStart,  // 시작 노드 통행 불가
    InvalidGoal,   // 목표 노드 통행 불가
};

struct PathResult {
    PathStatus             status = PathStatus::NoPath;
    std::vector<PathPoint> points;   // 시작 → 목표 순서(Success일 때만 채워짐)
    uint32_t               expandedNodes = 0;   // 진단(탐색한 노드 수)
};

// ---------------------------------------------------------------------------
// 동기 A* — 헤드리스 테스트·즉시 질의용. 스레드 안전(소스만 읽는다).
// ---------------------------------------------------------------------------
struct CostModifier {
    CostModifierFn fn = nullptr;
    void*          user = nullptr;
};

PathResult FindPath(const INavSource& source, NavNode start, NavNode goal,
                    const NavQueryParams& params = {}, CostModifier cost = {});

// ---------------------------------------------------------------------------
// NavSystem — 비동기 요청 허브. JobSystem 워커에서 A*를 계산하고 Poll/콜백으로 회수한다.
// ---------------------------------------------------------------------------
using NavRequestId = uint64_t;
inline constexpr NavRequestId kInvalidRequest = 0;

using PathCallback = std::function<void(NavRequestId, const PathResult&)>;

class NavSystem {
public:
    // jobs == nullptr 이면 요청이 동기 계산된다(테스트 편의). 소스는 비소유 — 수명은 호출자 책임.
    explicit NavSystem(const INavSource& source, JobSystem* jobs = nullptr);
    ~NavSystem();
    NavSystem(const NavSystem&) = delete;
    NavSystem& operator=(const NavSystem&) = delete;

    void SetParams(const NavQueryParams& p) { m_params = p; }
    void SetCostModifier(CostModifierFn fn, void* user) { m_cost = {fn, user}; }

    // 비동기 경로 요청. JobSystem이 있으면 워커에서 계산되고, 없으면 즉시 계산돼 완료 상태가 된다.
    // 계산은 요청 시점의 소스를 읽는다(계산 중 소스 변경 금지 — 타일 리베이크는 요청 경계에서).
    NavRequestId RequestPath(NavNode start, NavNode goal);
    NavRequestId RequestPath(NavNode start, NavNode goal, PathCallback onDone);

    // 결과 폴링. 완료됐으면 out에 채우고 true. 아직 Pending이면 false(out 미변경).
    // 완료된 요청을 반환하면 내부에서 제거된다(1회 소비).
    bool Poll(NavRequestId id, PathResult& out);

    // 완료 콜백 디스패치 — 메인 스레드가 프레임마다 호출(완료된 요청의 콜백을 메인에서 실행).
    void DispatchCompleted();

    bool IsComplete(NavRequestId id) const;

private:
    struct Request;
    void RunSync(const std::shared_ptr<Request>& req);   // 소스·파라미터로 계산

    const INavSource* m_source;
    JobSystem*        m_jobs;
    NavQueryParams    m_params;
    CostModifier      m_cost;

    mutable std::mutex m_mutex;
    NavRequestId       m_nextId = 1;
    std::unordered_map<NavRequestId, std::shared_ptr<Request>> m_requests;
};

} // namespace mye::nav
