// mye/anim/ClipPlayback.h — 클립 재생 상태·진행 로직(순수, ECS 비의존) (docs/03 §6, M3-B)
//
// AnimationClipData(M3-A)를 소비한다: frameIndices·frameDurations(병렬)·loop·direction.
// 재생 = frameIndices 시퀀스를 duration 만큼 유지하며 진행. direction(Forward/Reverse/PingPong)·
// loop 반영. AnimEventMarker 는 프레임 스킵(큰 dt) 시에도 유실 없이 경과분을 모두 발행한다.
//
// 순수 로직으로 분리(테스트 용이): Advance()가 경과한 이벤트 마커를 콜백/출력으로 보고한다.
#pragma once

#include "mye/asset/SpriteSheet.h"

#include <cstdint>
#include <vector>

namespace mye::anim {

using asset::AnimationClipData;
using asset::AnimEventMarker;

// 재생 커서 — 클립 내 "스텝 인덱스"(direction 적용 전 시퀀스 위치)와 스텝 내 경과시간.
// step 은 0..N-1(N=frameIndices.size()). PingPong 은 내부에서 0..2N-2 논리 스텝을 접는다.
struct ClipCursor {
    uint32_t step = 0;        // 현재 시퀀스 스텝(Forward 기준 논리 인덱스; 매핑은 아래 함수)
    float    timeInStep = 0.0f;
    bool     finished = false;  // 비루프 클립이 끝에 도달(마지막 프레임에 정지)
};

// direction 을 적용해 "논리 스텝 s" → 실제 frameIndices 인덱스로 매핑.
// Forward: s. Reverse: N-1-s. PingPong: 0..N-1..1 (주기 2N-2).
inline uint32_t StepToFrameSlot(const AnimationClipData& clip, uint32_t s) {
    const uint32_t n = static_cast<uint32_t>(clip.frameIndices.size());
    if (n == 0) return 0;
    switch (clip.direction) {
        case AnimationClipData::Direction::Reverse:
            return (n - 1u) - (s % n);
        case AnimationClipData::Direction::PingPong: {
            if (n == 1) return 0;
            const uint32_t period = 2u * n - 2u;
            const uint32_t p = s % period;
            return p < n ? p : (period - p);
        }
        case AnimationClipData::Direction::Forward:
        default:
            return s % n;
    }
}

// 논리 주기 스텝 수(loop 경계 판정용). Forward/Reverse=N, PingPong=2N-2(N>=2).
inline uint32_t ClipStepPeriod(const AnimationClipData& clip) {
    const uint32_t n = static_cast<uint32_t>(clip.frameIndices.size());
    if (n <= 1) return n;
    if (clip.direction == AnimationClipData::Direction::PingPong) return 2u * n - 2u;
    return n;
}

// 스텝 s 의 지속시간(초). frameDurations[frameSlot] 을 참조. 누락/음수는 0으로 클램프.
inline float StepDuration(const AnimationClipData& clip, uint32_t s) {
    const uint32_t slot = StepToFrameSlot(clip, s);
    if (slot < clip.frameDurations.size()) {
        const float d = clip.frameDurations[slot];
        return d > 0.0f ? d : 0.0f;
    }
    return 0.0f;
}

// 현재 스텝이 가리키는 실제 SpriteFrame 인덱스(= clip.frameIndices[slot]).
inline uint32_t CurrentFrameIndex(const AnimationClipData& clip, const ClipCursor& c) {
    const uint32_t n = static_cast<uint32_t>(clip.frameIndices.size());
    if (n == 0) return 0;
    const uint32_t slot = StepToFrameSlot(clip, c.step);
    return clip.frameIndices[slot];
}

// dt 만큼 커서를 진행하며, 경과한 프레임의 이벤트 마커를 onEvent(marker)로 순서대로 보고한다.
// - loop 클립: 주기를 넘어가면 감싸며 계속. 비루프 클립: 마지막 스텝에서 정지(finished=true).
// - 이벤트 유실 없음: 큰 dt로 여러 스텝을 건너뛰어도, 진입한 각 스텝의 마커를 모두 발행.
// - 마커 발행 규약: 스텝 "진입" 시점에 그 스텝(=frameSlot)에 붙은 모든 marker 발행. 최초 프레임
//   진입도 포함(EnterCurrentStepEvents 로 초기화 시 발행). Advance 는 다음 스텝 진입분만 발행.
template <typename OnEvent>
void AdvanceClip(const AnimationClipData& clip, ClipCursor& c, float dt, OnEvent&& onEvent) {
    const uint32_t n = static_cast<uint32_t>(clip.frameIndices.size());
    if (n == 0 || dt <= 0.0f) return;
    if (c.finished) return;

    const uint32_t period = ClipStepPeriod(clip);
    c.timeInStep += dt;

    // 현재 스텝의 지속을 다 채우면 다음 스텝으로. 0초 스텝(즉시 통과)도 안전하게 처리.
    // 무한 루프 방지: 한 번의 Advance에서 최대 8*period 스텝만 전진(전부 0초인 병리 케이스).
    uint32_t guard = 8u * (period + 1u);
    while (guard-- > 0) {
        const float stepDur = StepDuration(clip, c.step);
        if (stepDur > 0.0f && c.timeInStep < stepDur) break;   // 아직 현재 스텝 유지
        // 스텝 완료 → 다음 스텝 진입.
        c.timeInStep -= stepDur;

        const uint32_t nextStep = c.step + 1u;
        const bool crossPeriod = (nextStep >= period);
        if (crossPeriod && !clip.loop) {
            // 비루프: 마지막 스텝(마지막 프레임)에 정지. 남은 시간은 버린다.
            c.step = period - 1u;
            c.timeInStep = 0.0f;
            c.finished = true;
            break;
        }
        c.step = crossPeriod ? 0u : nextStep;

        // 새로 진입한 스텝의 이벤트 마커 발행(그 스텝의 frameSlot에 붙은 것).
        const uint32_t slot = StepToFrameSlot(clip, c.step);
        for (const AnimEventMarker& m : clip.events) {
            if (m.frameIndex == slot) onEvent(m);
        }

        if (stepDur <= 0.0f && c.timeInStep <= 0.0f) {
            // 0초 스텝을 통과했고 남은 시간이 없다 — 다음 루프에서 stepDur 검사로 정지.
            // (timeInStep==0, 다음 stepDur>0 이면 break)
            if (StepDuration(clip, c.step) > 0.0f) break;
        }
    }
}

// 초기 진입(재생 시작·상태 전환 시 첫 프레임) 마커 발행 — 스텝 0(또는 지정 스텝)의 마커.
template <typename OnEvent>
void EmitStepEnterEvents(const AnimationClipData& clip, uint32_t step, OnEvent&& onEvent) {
    const uint32_t slot = StepToFrameSlot(clip, step);
    for (const AnimEventMarker& m : clip.events) {
        if (m.frameIndex == slot) onEvent(m);
    }
}

} // namespace mye::anim
