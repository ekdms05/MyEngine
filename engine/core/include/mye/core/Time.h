// mye/core/Time.h — 시계·타임스텝·시간 시스템 (docs/01)
//
// 내부 시계: QueryPerformanceCounter, 정수 나노초(uint64) 누적 — 부동소수 누적 오차 회피.
// 시간 축: RealTime(스케일 무관) / GameTime(timeScale·pause) / FixedTime(고정 스텝 누적).
#pragma once

#include "mye/core/Base.h"

namespace mye {

struct TimeStep {
    double   deltaSeconds = 0.0;         // timeScale 적용 델타 (Fixed 페이즈에선 fixedDelta 고정값)
    double   unscaledDeltaSeconds = 0.0; // 실시간 델타
    double   gameTimeSeconds = 0.0;      // 누적 게임 시간 (timeScale 반영)
    double   realTimeSeconds = 0.0;      // 누적 실시간
    uint64_t frameIndex = 0;             // 렌더 프레임 카운터
    uint64_t fixedStepIndex = 0;         // 고정 스텝 카운터 (결정성 로직용)
};

// QPC 래퍼 — 나노초 정수 누적.  구현: src/Time.cpp
class Clock {
public:
    Clock();
    void     Reset();                    // 기준점 재설정
    uint64_t TickNanoseconds();          // 직전 Tick(또는 Reset) 이후 경과 ns
    uint64_t ElapsedNanoseconds() const; // Reset 이후 총 경과 ns (Tick에 영향 없음)

private:
    uint64_t m_frequency = 0;            // QPC ticks/sec
    uint64_t m_startTicks = 0;
    uint64_t m_lastTicks = 0;
};

class TimeSystem {
public:
    MYE_SERVICE(TimeSystem);

    void   SetTimeScale(double scale);          // 0 = pause
    double GetTimeScale() const { return m_timeScale; }

    void   SetFixedRate(double hz);             // 기본 60. project.json "time.fixedHz"에서 로드
    double GetFixedRate() const { return m_fixedHz; }
    double GetFixedDeltaSeconds() const { return 1.0 / m_fixedHz; }

    double GetInterpolationAlpha() const { return m_alpha; }  // 렌더 보간 계수 [0,1)
    const TimeStep& CurrentStep() const { return m_step; }

    // ---- 엔진 메인 루프 전용 (GuardedMain 내부 루프만 호출) ----
    void AdvanceFrame(double unscaledDeltaSeconds);  // 프레임 시작: 델타·누적·frameIndex 갱신
    void AdvanceFixedStep();                         // 고정 스텝 1회: fixedStepIndex·FixedTime 갱신
    void SetInterpolationAlpha(double alpha);

private:
    TimeStep m_step{};
    double   m_timeScale = 1.0;
    double   m_fixedHz = 60.0;
    double   m_alpha = 0.0;
};

} // namespace mye
