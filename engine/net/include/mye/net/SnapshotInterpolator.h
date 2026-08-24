// mye/net/SnapshotInterpolator.h — 원격 엔티티 스냅샷 보간 (docs/mmorpg/02, M9)
//
// 서버 스냅샷(이산·티케이트)을 렌더 프레임(연속)으로 부드럽게: 수신 스냅샷을 (수신 로컬
// 시각, 내용) 쌍으로 쌓아 두고, 표시 시각(now − 보간지연) 기준 앞뒤 두 스냅샷 사이를
// 선형 보간해 원격 엔티티 위치를 낸다. 보간지연(기본 100ms)만큼 과거를 보여주는 대신
// 지터 없는 원격 움직임을 얻는 표준 MMO 기법. 자기 엔티티는 클라 예측(CSP)이 담당하므로
// Sample 에서 제외한다. 순수 로직(소켓·시계 무관 — 시각은 호출자가 주입) → 결정론 테스트 가능.
#pragma once

#include "mye/net/Protocol.h"

#include <cstdint>
#include <vector>

namespace mye::net {

class SnapshotInterpolator {
public:
    explicit SnapshotInterpolator(uint32_t capacity = 32) : m_capacity(capacity ? capacity : 1) {}

    // 스냅샷 수신 기록. nowMs 는 호출자의 단조 증가 로컬 시각(밀리초). 이전 최신보다
    // 오래된/같은 시각의 프레임은 무시(UDP 재순서·중복 방어 — 같은 tick 재푸시는 호출자가
    // 걸러도 되지만 여기서도 안전).
    void Push(uint64_t nowMs, const std::vector<EntitySnap>& snap);

    // 표시 시각(renderTimeMs = now − InterpolationDelayMs)의 원격 엔티티 상태를 out 에
    // 채운다. skipNetId(자기 id)는 제외. 버퍼 경계 밖(첫 프레임 이전·마지막 이후)은 가장
    // 가까운 프레임을 홀드(외삽 없음 — 스냅샷 지연 시 정지가 튀는 것보다 낫다).
    void Sample(uint64_t renderTimeMs, uint32_t skipNetId, std::vector<EntitySnap>& out) const;

    // 보간 지연(버퍼 깊이). 스냅샷 송신 간격보다 크게(서버 20Hz=50ms → 100ms 기본).
    void     SetInterpolationDelayMs(uint64_t ms) { m_delayMs = ms; }
    uint64_t InterpolationDelayMs() const { return m_delayMs; }

    size_t    BufferedCount() const { return m_buffer.size(); }   // 테스트/디버그
    uint64_t  NewestTimeMs() const { return m_buffer.empty() ? 0 : m_buffer.back().timeMs; }

private:
    struct Frame {
        uint64_t                 timeMs = 0;
        std::vector<EntitySnap>  ents;
    };

    std::vector<Frame> m_buffer;   // timeMs 오름차순
    uint32_t           m_capacity = 32;
    uint64_t           m_delayMs = 100;
};

} // namespace mye::net
