// mye/nav/Pathfinding.cpp — 그리드 A* 구현(높이·다리 인지) (docs/03 §9)
#include "mye/nav/Pathfinding.h"

#include "mye/core/Jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>

namespace mye::nav {

// ===========================================================================
// TilemapNavSource — TilemapWorld 질의 래퍼
// ===========================================================================
bool TilemapNavSource::IsWalkable(int32_t cellX, int32_t cellY, int32_t level) const {
    // 해당 셀·레벨에 컬럼이 존재하고 솔리드가 아니면 통행 가능(다리 상판은 통행 가능).
    const std::optional<tilemap::TileColumn> col =
        m_world->ColumnAtLevel(Vec2i{cellX, cellY}, level);
    if (!col) return false;
    return !col->IsSolid();
}

float TilemapNavSource::MoveCost(int32_t cellX, int32_t cellY, int32_t level) const {
    const std::optional<tilemap::TileColumn> col =
        m_world->ColumnAtLevel(Vec2i{cellX, cellY}, level);
    if (!col) return 1.0f;
    // 경사·계단은 살짝 비싸게(선호도 낮춤). 데이터 모델에 별도 moveCost 필드가 생기면 인용.
    return tilemap::SlopeRiseLevels(col->slope) > 0 ? 1.5f : 1.0f;
}

Vec2 TilemapNavSource::NodeWorld(int32_t cellX, int32_t cellY, int32_t level) const {
    // 셀 중심 XY. Y는 경사 보간 높이(SampleHeight)를 반영해 이동 시스템이 매끄럽게 따라가게 한다.
    const float wx = static_cast<float>(cellX) + 0.5f;
    const float wyBase = tilemap::CellToWorldY(cellY) + 0.5f;
    const float h = m_world->SampleHeight(Vec2{wx, wyBase}, static_cast<int8_t>(level));
    // SampleHeight는 지면 월드 Y(높이). 렌더/이동은 (평면 XY, 높이) 분리 규약이므로 여기서는
    // 평면 XY만 반환하되 Y에 높이 오프셋을 더한 값을 준다(2.5D 투영 시 화면 Y로 소비).
    return Vec2{wx, wyBase + h};
}

// ===========================================================================
// A* 코어
// ===========================================================================
namespace {

// 노드 해시 키(64bit로 압축: cellX·cellY 각 24bit, level 16bit — 실사용 범위 충분).
struct NodeKey {
    uint64_t v;
    static NodeKey Make(const NavNode& n) {
        const uint64_t x = static_cast<uint32_t>(n.cellX) & 0xFFFFFFu;
        const uint64_t y = static_cast<uint32_t>(n.cellY) & 0xFFFFFFu;
        const uint64_t l = static_cast<uint32_t>(n.level) & 0xFFFFu;
        return NodeKey{(x << 40) | (y << 16) | l};
    }
    bool operator==(const NodeKey& o) const { return v == o.v; }
};

// 노드가 NodeKey 압축 범위(cell 각 24bit 부호, level 16bit 부호) 안인지. 밖이면 서로 다른
//   노드가 한 키로 충돌해 A*가 조용히 틀린 결과를 낼 수 있으므로 탐색 시작에서 명시적으로 거른다.
bool NodeInKeyRange(const NavNode& n) {
    constexpr int32_t kCellMin = -(1 << 23), kCellMax = (1 << 23) - 1;
    constexpr int32_t kLevelMin = -(1 << 15), kLevelMax = (1 << 15) - 1;
    return n.cellX >= kCellMin && n.cellX <= kCellMax &&
           n.cellY >= kCellMin && n.cellY <= kCellMax &&
           n.level >= kLevelMin && n.level <= kLevelMax;
}
struct NodeKeyHash {
    size_t operator()(const NodeKey& k) const noexcept { return static_cast<size_t>(k.v); }
};

// 옥타일 거리 휴리스틱(대각 허용 시) / 맨해튼(직교만). level 차는 수직 비용으로 가산.
float Heuristic(const NavNode& a, const NavNode& b, bool diagonal) {
    const float dx = static_cast<float>(std::abs(a.cellX - b.cellX));
    const float dy = static_cast<float>(std::abs(a.cellY - b.cellY));
    float h;
    if (diagonal) {
        const float dmin = std::min(dx, dy);
        const float dmax = std::max(dx, dy);
        h = (dmax - dmin) + 1.41421356f * dmin;   // 옥타일
    } else {
        h = dx + dy;
    }
    h += static_cast<float>(std::abs(a.level - b.level)) * 0.5f;   // 층 전이 소폭 가산
    return h;
}

struct OpenEntry {
    float   f;
    uint32_t seq;   // 삽입 순서(동률 타이브레이크 — 결정론적)
    NavNode node;
};
struct OpenCmp {
    bool operator()(const OpenEntry& a, const OpenEntry& b) const {
        if (a.f != b.f) return a.f > b.f;       // 최소 f 우선
        return a.seq > b.seq;                    // 동률이면 먼저 삽입한 것 우선
    }
};

struct NodeRec {
    NavNode parent;
    float   g = 0.0f;
    bool    hasParent = false;
    bool    closed = false;
};

// from 노드에서 (dx, dy) 방향으로 이동 가능한 목적 노드를 찾는다.
// 층 전이: 같은 level이 통행 가능하면 그것을, 아니면 ±maxLevelStep 범위에서 통행 가능한 level을
//   찾는다(경사/계단으로 오르내림). 여러 후보 중 |level 차|가 가장 작은 것을 택한다.
bool ResolveNeighbor(const INavSource& src, const NavNode& from, int dx, int dy,
                     int maxLevelStep, NavNode& out) {
    const int nx = from.cellX + dx;
    const int ny = from.cellY + dy;
    // 우선 같은 층.
    if (src.IsWalkable(nx, ny, from.level)) {
        out = NavNode{nx, ny, from.level};
        return true;
    }
    // 층 전이 후보 탐색(가까운 층부터).
    for (int step = 1; step <= maxLevelStep; ++step) {
        for (int dir = -1; dir <= 1; dir += 2) {
            const int lv = from.level + dir * step;
            if (src.IsWalkable(nx, ny, lv)) {
                out = NavNode{nx, ny, lv};
                return true;
            }
        }
    }
    return false;
}

} // namespace

PathResult FindPath(const INavSource& source, NavNode start, NavNode goal,
                    const NavQueryParams& params, CostModifier cost) {
    PathResult result;

    // NodeKey 압축 범위 밖이면 키 충돌 위험 → 조용한 오답 대신 명시적 실패로 거른다.
    if (!NodeInKeyRange(start)) {
        result.status = PathStatus::InvalidStart;
        return result;
    }
    if (!NodeInKeyRange(goal)) {
        result.status = PathStatus::InvalidGoal;
        return result;
    }

    if (!source.IsWalkable(start.cellX, start.cellY, start.level)) {
        result.status = PathStatus::InvalidStart;
        return result;
    }
    if (!source.IsWalkable(goal.cellX, goal.cellY, goal.level)) {
        result.status = PathStatus::InvalidGoal;
        return result;
    }
    if (start == goal) {
        result.status = PathStatus::Success;
        PathPoint p;
        p.node = start;
        p.world = source.NodeWorld(start.cellX, start.cellY, start.level);
        p.level = static_cast<int8_t>(start.level);
        result.points.push_back(p);
        return result;
    }

    std::unordered_map<NodeKey, NodeRec, NodeKeyHash> nodes;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenCmp> open;

    uint32_t seq = 0;
    {
        NodeRec r; r.g = 0.0f; r.hasParent = false;
        nodes[NodeKey::Make(start)] = r;
        open.push(OpenEntry{Heuristic(start, goal, params.allowDiagonal), seq++, start});
    }

    // 이웃 방향 오프셋(직교 4 + 대각 4).
    static constexpr int kOrtho[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static constexpr int kDiag[4][2]  = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    bool found = false;
    while (!open.empty()) {
        const OpenEntry cur = open.top();
        open.pop();

        const NodeKey curKey = NodeKey::Make(cur.node);
        NodeRec& curRec = nodes[curKey];
        if (curRec.closed) continue;   // 오래된 큐 항목(감소된 g로 재삽입됨)
        curRec.closed = true;
        ++result.expandedNodes;

        if (cur.node == goal) { found = true; break; }

        if (result.expandedNodes > static_cast<uint32_t>(params.maxExpandedNodes))
            break;   // 안전 상한 초과 → NoPath

        const float curG = curRec.g;

        auto tryEdge = [&](int dx, int dy, float baseStep, bool isDiagonal) {
            NavNode nb;
            if (!ResolveNeighbor(source, cur.node, dx, dy, params.maxLevelStep, nb)) return;

            if (isDiagonal && params.preventCornerCut) {
                // 대각 이동은 양쪽 직교 셀이 통행 가능해야 허용하되, 그 층이 from 또는 nb 의 층과
                //   maxLevelStep 이내여야 한다. 다리·경사에서 두 직교 변이 서로 다른(연결 안 된)
                //   층에서만 통행 가능하면 물리적으로 이어지지 않은 모서리를 자르게 되므로 배제.
                NavNode side1, side2;
                const bool ok1 = ResolveNeighbor(source, cur.node, dx, 0,
                                                 params.maxLevelStep, side1);
                const bool ok2 = ResolveNeighbor(source, cur.node, 0, dy,
                                                 params.maxLevelStep, side2);
                if (!ok1 || !ok2) return;
                auto levelConnected = [&](const NavNode& side) {
                    return std::abs(side.level - cur.node.level) <= params.maxLevelStep ||
                           std::abs(side.level - nb.level)       <= params.maxLevelStep;
                };
                if (!levelConnected(side1) || !levelConnected(side2)) return;
            }

            float mult = source.MoveCost(nb.cellX, nb.cellY, nb.level);
            if (cost.fn) {
                const float m = cost.fn(cur.node, nb, cost.user);
                if (m <= 0.0f) return;   // 콜백이 통행 불가로 판정
                mult *= m;
            }
            // 층 전이 추가 비용(수직 이동은 살짝 비싸게).
            const float levelPenalty =
                static_cast<float>(std::abs(nb.level - cur.node.level)) * 0.5f;
            const float stepCost = (baseStep + levelPenalty) * mult;
            const float ng = curG + stepCost;

            const NodeKey nbKey = NodeKey::Make(nb);
            auto it = nodes.find(nbKey);
            if (it != nodes.end() && it->second.closed) return;
            if (it != nodes.end() && ng >= it->second.g) return;   // 개선 없음

            NodeRec& nr = nodes[nbKey];
            nr.parent = cur.node;
            nr.hasParent = true;
            nr.g = ng;
            nr.closed = false;
            const float f = ng + Heuristic(nb, goal, params.allowDiagonal);
            open.push(OpenEntry{f, seq++, nb});
        };

        for (auto& d : kOrtho) tryEdge(d[0], d[1], 1.0f, false);
        if (params.allowDiagonal)
            for (auto& d : kDiag) tryEdge(d[0], d[1], 1.41421356f, true);
    }

    if (!found) {
        result.status = PathStatus::NoPath;
        return result;
    }

    // 역추적으로 경로 복원.
    std::vector<NavNode> rev;
    NavNode n = goal;
    for (;;) {
        rev.push_back(n);
        if (n == start) break;
        const NodeRec& r = nodes[NodeKey::Make(n)];
        if (!r.hasParent) break;   // 방어(정상 경로면 start에서 종료)
        n = r.parent;
    }
    std::reverse(rev.begin(), rev.end());

    // 후처리: 문자열 당기기 간이 스무딩 — 같은 level의 직선 구간에서 중간 노드를 걷어낸다.
    if (params.smoothPath && rev.size() > 2) {
        std::vector<NavNode> out;
        out.push_back(rev.front());
        for (size_t i = 1; i + 1 < rev.size(); ++i) {
            const NavNode& a = out.back();
            const NavNode& b = rev[i];
            const NavNode& c = rev[i + 1];
            // 세 점이 같은 level이고 공선(collinear)이면 b 생략.
            const bool sameLevel = (a.level == b.level && b.level == c.level);
            const int abx = b.cellX - a.cellX, aby = b.cellY - a.cellY;
            const int bcx = c.cellX - b.cellX, bcy = c.cellY - b.cellY;
            const bool collinear = (abx * bcy - aby * bcx) == 0 &&
                                   (abx * bcx + aby * bcy) >= 0;
            if (!(sameLevel && collinear)) out.push_back(b);
        }
        out.push_back(rev.back());
        rev.swap(out);
    }

    result.status = PathStatus::Success;
    result.points.reserve(rev.size());
    for (const NavNode& nn : rev) {
        PathPoint p;
        p.node = nn;
        p.world = source.NodeWorld(nn.cellX, nn.cellY, nn.level);
        p.level = static_cast<int8_t>(nn.level);
        result.points.push_back(p);
    }
    return result;
}

// ===========================================================================
// NavSystem — 비동기 요청 허브
// ===========================================================================
struct NavSystem::Request {
    NavRequestId id = 0;
    NavNode      start;
    NavNode      goal;
    PathCallback callback;   // 선택
    PathResult   result;
    bool         complete = false;   // 계산 완료(워커가 세팅, m_mutex 보호)
    bool         dispatched = false; // 콜백 이미 호출됨

    // 워커가 읽을 계산 입력 스냅샷.
    const INavSource* source = nullptr;
    NavQueryParams    params;
    CostModifier      cost;
};

NavSystem::NavSystem(const INavSource& source, JobSystem* jobs)
    : m_source(&source), m_jobs(jobs) {}

NavSystem::~NavSystem() = default;

void NavSystem::RunSync(const std::shared_ptr<Request>& req) {
    PathResult r = FindPath(*req->source, req->start, req->goal, req->params, req->cost);
    std::lock_guard<std::mutex> lk(m_mutex);
    req->result = std::move(r);
    req->complete = true;
}

NavRequestId NavSystem::RequestPath(NavNode start, NavNode goal) {
    return RequestPath(start, goal, nullptr);
}

NavRequestId NavSystem::RequestPath(NavNode start, NavNode goal, PathCallback onDone) {
    auto req = std::make_shared<Request>();
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        req->id = m_nextId++;
        req->start = start;
        req->goal = goal;
        req->callback = std::move(onDone);
        req->source = m_source;
        req->params = m_params;
        req->cost = m_cost;
        req->result.status = PathStatus::Pending;
        m_requests.emplace(req->id, req);
    }

    if (m_jobs) {
        // 워커 스레드에서 A* 계산. shared_ptr 캡처로 수명 보장.
        auto self = this;
        m_jobs->Schedule([self, req]() { self->RunSync(req); }, QueueKind::Compute);
    } else {
        RunSync(req);   // 동기 폴백(테스트·잡 시스템 없을 때)
    }
    return req->id;
}

bool NavSystem::Poll(NavRequestId id, PathResult& out) {
    std::shared_ptr<Request> req;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_requests.find(id);
        if (it == m_requests.end()) return false;
        if (!it->second->complete) return false;
        req = it->second;
        m_requests.erase(it);   // 1회 소비
    }
    out = req->result;
    return true;
}

void NavSystem::DispatchCompleted() {
    std::vector<std::shared_ptr<Request>> ready;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [id, req] : m_requests) {
            if (req->complete && !req->dispatched && req->callback) {
                req->dispatched = true;
                ready.push_back(req);
            }
        }
    }
    // 콜백은 락 밖(메인 스레드)에서 호출. 콜백은 1회성 소비이므로 호출 후 맵에서 제거한다
    //   (제거하지 않으면 콜백 등록 요청이 dispatched=true 상태로 영구 잔존 → 매 프레임 경로를
    //   재요청하는 에이전트에서 m_requests 가 단조 증가하는 사실상 누수).
    for (auto& req : ready) req->callback(req->id, req->result);
    if (!ready.empty()) {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& req : ready) m_requests.erase(req->id);
    }
}

bool NavSystem::IsComplete(NavRequestId id) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_requests.find(id);
    return it != m_requests.end() && it->second->complete;
}

} // namespace mye::nav
