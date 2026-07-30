// mye/render/ReadbackQueue.h — N프레임 지연 GPU 리드백 큐 상태머신 (docs/02, M7 ID 픽킹)
//
// ID 버퍼 픽킹은 "이번 프레임에 1px 영역 리드백 요청 → N프레임 뒤 준비되면 값 폴링"이다. GPU
// 스테이징/Map 은 후속(Dx11)이지만, 프레임 인덱스 기반 준비 판정·좌표 매핑은 순수 상태머신으로
// 결정론·단위 테스트한다.
#pragma once

#include "mye/core/Math.h"

#include <cstdint>
#include <vector>

namespace mye::render {

// 스크린 픽셀 좌표 → 렌더타깃(내부 RT) 픽셀 좌표. 뷰포트 레터박스/스케일 반영.
//   screenPos: 윈도우 픽셀(좌상단 원점). viewport: 스크린 상 RT가 그려진 사각형.
//   rtW/rtH: 내부 RT 해상도. 반환 outX/outY 는 RT 픽셀(범위 밖이면 false).
inline bool ScreenToRenderTarget(Vec2 screenPos, const Rect& viewport, int rtW, int rtH,
                                 int& outX, int& outY) {
    if (viewport.w <= 0.0f || viewport.h <= 0.0f || rtW <= 0 || rtH <= 0) return false;
    const float u = (screenPos.x - viewport.x) / viewport.w;   // 0..1
    const float v = (screenPos.y - viewport.y) / viewport.h;
    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return false;
    outX = static_cast<int>(u * static_cast<float>(rtW));
    outY = static_cast<int>(v * static_cast<float>(rtH));
    if (outX >= rtW) outX = rtW - 1;
    if (outY >= rtH) outY = rtH - 1;
    return true;
}

// 지연 리드백 큐. Enqueue 로 요청(현재 프레임 기록), Advance(프레임++), 준비된 요청을 Poll.
//   latencyFrames 프레임 뒤에 준비된다(GPU 파이프라인 지연 모사). value 는 리드백된 페이로드.
template <typename T>
class ReadbackQueue {
public:
    explicit ReadbackQueue(uint32_t latencyFrames = 1) : m_latency(latencyFrames) {}

    // 요청 등록 → 핸들(고유 id). value 는 GPU 준비 시점에 채워지지만, 테스트/모사는 즉시 값을 준다.
    uint64_t Enqueue(const T& value) {
        const uint64_t id = m_nextId++;
        m_pending.push_back(Pending{ id, m_frame, value, false });
        return id;
    }

    // 프레임 전진(매 프레임 1회). latency 경과한 요청을 ready 로 표시.
    void Advance() {
        ++m_frame;
        for (Pending& p : m_pending)
            if (!p.ready && m_frame - p.frame >= m_latency) p.ready = true;
    }

    // 준비된 요청 하나를 꺼낸다(FIFO). 없으면 false.
    bool Poll(uint64_t& outId, T& outValue) {
        for (size_t i = 0; i < m_pending.size(); ++i) {
            if (m_pending[i].ready) {
                outId = m_pending[i].id;
                outValue = m_pending[i].value;
                m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    size_t PendingCount() const { return m_pending.size(); }
    uint32_t Latency() const { return m_latency; }

private:
    struct Pending { uint64_t id; uint64_t frame; T value; bool ready; };
    uint32_t m_latency;
    uint64_t m_frame = 0;
    uint64_t m_nextId = 1;
    std::vector<Pending> m_pending;
};

} // namespace mye::render
