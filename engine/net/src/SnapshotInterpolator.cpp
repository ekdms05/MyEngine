// mye/net/SnapshotInterpolator.cpp — 원격 엔티티 보간 구현 (SnapshotInterpolator.h 참조)
#include "mye/net/SnapshotInterpolator.h"

namespace mye::net {

void SnapshotInterpolator::Push(uint64_t nowMs, const std::vector<EntitySnap>& snap) {
    if (!m_buffer.empty() && nowMs <= m_buffer.back().timeMs) return;   // 재순서·중복 폭탄
    m_buffer.push_back(Frame{nowMs, snap});
    if (m_buffer.size() > m_capacity)
        m_buffer.erase(m_buffer.begin());
}

void SnapshotInterpolator::Sample(uint64_t renderTimeMs, uint32_t skipNetId,
                                  std::vector<EntitySnap>& out) const {
    out.clear();
    if (m_buffer.empty()) return;

    // 표시 시각을 감싸는 앞(b)·뒤(a) 프레임 탐색: a.timeMs ≤ renderTime < 다음.
    size_t ai = 0;
    while (ai + 1 < m_buffer.size() && m_buffer[ai + 1].timeMs <= renderTimeMs) ++ai;
    const Frame& a = m_buffer[ai];
    const Frame* b = (ai + 1 < m_buffer.size()) ? &m_buffer[ai + 1] : nullptr;

    // 렌더 시각이 a 이전(버퍼 선두 이전)이면 alpha 0 유지 → a 홀드. (uint64 뺄셈
    // 언더플로 방지 — 조건으로 가드하지 않으면 0-100 이 2^64 근처가 되어 역방향 폭주.)
    float alpha = 0.0f;
    if (b && b->timeMs > a.timeMs && renderTimeMs >= a.timeMs)
        alpha = static_cast<float>(renderTimeMs - a.timeMs) / static_cast<float>(b->timeMs - a.timeMs);
    if (alpha > 1.0f) alpha = 1.0f;   // (b 선택 로직상 발생 않함 — 방어)

    // 기준 집합 = 최신쪽 프레임(b 없으면 a). b 기준으로 먼저 채우고(새로 등장 포함),
    // a에만 있는 엔티티는 사라지는 중으로 a 값을 홀드(윈도우에서 밀리면 자연 소멸).
    const Frame& base = b ? *b : a;
    for (const EntitySnap& e : base.ents) {
        if (e.netId == skipNetId) continue;
        EntitySnap r = e;
        if (b) {
            for (const EntitySnap& pa : a.ents) {
                if (pa.netId != e.netId) continue;
                r.x = pa.x + (e.x - pa.x) * alpha;
                r.y = pa.y + (e.y - pa.y) * alpha;
                break;
            }
        }
        out.push_back(r);
    }
    if (b) {
        for (const EntitySnap& pa : a.ents) {
            if (pa.netId == skipNetId) continue;
            bool inB = false;
            for (const EntitySnap& e : b->ents) if (e.netId == pa.netId) { inB = true; break; }
            if (!inB) out.push_back(pa);
        }
    }
}

} // namespace mye::net
