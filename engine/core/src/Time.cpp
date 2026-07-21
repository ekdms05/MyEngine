// Time.cpp — Clock: QueryPerformanceCounter 기반, 정수 나노초 누적 (docs/01 시간 시스템).
#include "mye/core/Time.h"

#include <Windows.h>

namespace mye {

namespace {

uint64_t QpcNow() {
    LARGE_INTEGER counter;
    ::QueryPerformanceCounter(&counter);            // XP 이후 항상 성공
    return static_cast<uint64_t>(counter.QuadPart);
}

uint64_t QpcFrequency() {
    LARGE_INTEGER frequency;
    ::QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(frequency.QuadPart);
}

// 틱 → 나노초. 초/나머지 분리 곱으로 64bit 오버플로 회피.
// (remainder < frequency, frequency는 보통 10MHz — remainder*1e9 < 1e16 << 2^63)
uint64_t TicksToNanoseconds(uint64_t ticks, uint64_t frequency) {
    const uint64_t seconds   = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    return seconds * 1'000'000'000ull + (remainder * 1'000'000'000ull) / frequency;
}

} // namespace

Clock::Clock() { Reset(); }

void Clock::Reset() {
    m_frequency  = QpcFrequency();
    m_startTicks = QpcNow();
    m_lastTicks  = m_startTicks;
}

uint64_t Clock::TickNanoseconds() {
    const uint64_t now   = QpcNow();
    const uint64_t delta = now - m_lastTicks;
    m_lastTicks = now;
    return TicksToNanoseconds(delta, m_frequency);
}

uint64_t Clock::ElapsedNanoseconds() const {
    return TicksToNanoseconds(QpcNow() - m_startTicks, m_frequency);
}

// ---- TimeSystem ----

void TimeSystem::SetTimeScale(double scale) { m_timeScale = scale < 0.0 ? 0.0 : scale; }

void TimeSystem::SetFixedRate(double hz) {
    if (hz > 0.0) m_fixedHz = hz;
}

void TimeSystem::AdvanceFrame(double unscaledDeltaSeconds) {
    m_step.unscaledDeltaSeconds = unscaledDeltaSeconds;
    m_step.deltaSeconds = unscaledDeltaSeconds * m_timeScale;
    m_step.realTimeSeconds += unscaledDeltaSeconds;
    m_step.gameTimeSeconds += m_step.deltaSeconds;
    ++m_step.frameIndex;
}

void TimeSystem::AdvanceFixedStep() {
    ++m_step.fixedStepIndex;
}

void TimeSystem::SetInterpolationAlpha(double alpha) { m_alpha = alpha; }

} // namespace mye
