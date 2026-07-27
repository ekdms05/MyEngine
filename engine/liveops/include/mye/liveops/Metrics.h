// mye/liveops/Metrics.h — 서버 메트릭/텔레메트리(카운터·게이지·타이밍) (docs/mmorpg/09, M11)
//
// 운영 관찰성의 기본 수집기. 세 종류를 지원한다:
//   - Counter: 단조 증가 누적값(처리한 틱 수·전송 패킷·킥 횟수 …)
//   - Gauge:   현재 순간값(접속 클라 수·큐 길이 …)
//   - Timing:  샘플 관측 통계(틱 처리 ms·RTT …) — count/sum/min/max/avg
// SnapshotJson 으로 현재 상태를 내보내 대시보드·텔레메트리 업로드에 쓴다.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace mye::liveops {

struct TimingStat {
    uint64_t count = 0;
    double   sum = 0.0;
    double   min = 0.0;
    double   max = 0.0;
    double Avg() const { return count ? sum / static_cast<double>(count) : 0.0; }
};

class MetricsRegistry {
public:
    // ---- Counter(단조 증가) ----
    void    Counter(std::string_view name, int64_t delta = 1);
    int64_t GetCounter(std::string_view name) const;

    // ---- Gauge(순간값) ----
    void    Gauge(std::string_view name, double value);
    double  GetGauge(std::string_view name) const;

    // ---- Timing(샘플 관측) ----
    void       Observe(std::string_view name, double sample);
    TimingStat GetStat(std::string_view name) const;

    // 스냅샷(대시보드/업로드용). { counters, gauges, timings:{name:{count,avg,min,max}} }.
    std::string SnapshotJson() const;

    // 전량 초기화(주기 리셋 시).
    void Reset();

    size_t CounterCount() const { return m_counters.size(); }
    size_t GaugeCount() const { return m_gauges.size(); }
    size_t TimingCount() const { return m_timings.size(); }

private:
    // std::less<> 로 string_view heterogeneous 조회.
    std::map<std::string, int64_t, std::less<>>    m_counters;
    std::map<std::string, double, std::less<>>     m_gauges;
    std::map<std::string, TimingStat, std::less<>> m_timings;
};

} // namespace mye::liveops
