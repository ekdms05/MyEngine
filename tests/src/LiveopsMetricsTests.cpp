// LiveopsMetricsTests.cpp — 메트릭 카운터·게이지·타이밍·스냅샷 (docs/mmorpg/09, M11)
#include "TestFramework.h"

#include "mye/liveops/Metrics.h"

#include <cmath>
#include <string>

using namespace mye;
using namespace mye::liveops;

namespace { bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; } }

MYE_TEST(MetricsCountersAndGauges) {
    MetricsRegistry m;

    m.Counter("ticks");           // 기본 +1
    m.Counter("ticks");
    m.Counter("packets", 10);
    MYE_EXPECT(m.GetCounter("ticks") == 2);
    MYE_EXPECT(m.GetCounter("packets") == 10);
    MYE_EXPECT(m.GetCounter("none") == 0);   // 없으면 0

    m.Gauge("clients", 5);
    MYE_EXPECT(m.GetGauge("clients") == 5.0);
    m.Gauge("clients", 3);        // 순간값 대체
    MYE_EXPECT(m.GetGauge("clients") == 3.0);
    MYE_EXPECT(m.GetGauge("none") == 0.0);
}

MYE_TEST(MetricsTimingStats) {
    MetricsRegistry m;
    m.Observe("tick_ms", 10.0);
    m.Observe("tick_ms", 20.0);
    m.Observe("tick_ms", 30.0);

    const TimingStat s = m.GetStat("tick_ms");
    MYE_EXPECT(s.count == 3);
    MYE_EXPECT(Near(s.sum, 60.0));
    MYE_EXPECT(Near(s.min, 10.0));
    MYE_EXPECT(Near(s.max, 30.0));
    MYE_EXPECT(Near(s.Avg(), 20.0));

    // 없는 타이밍은 0 통계.
    MYE_EXPECT(m.GetStat("none").count == 0);
    MYE_EXPECT(m.GetStat("none").Avg() == 0.0);
}

MYE_TEST(MetricsSnapshotJsonAndReset) {
    MetricsRegistry m;
    m.Counter("ticks", 100);
    m.Gauge("clients", 42);
    m.Observe("tick_ms", 16.6);

    const std::string json = m.SnapshotJson();
    MYE_EXPECT(json.find("\"counters\"") != std::string::npos);
    MYE_EXPECT(json.find("\"gauges\"") != std::string::npos);
    MYE_EXPECT(json.find("\"timings\"") != std::string::npos);
    MYE_EXPECT(json.find("ticks") != std::string::npos);
    MYE_EXPECT(json.find("clients") != std::string::npos);
    MYE_EXPECT(json.find("tick_ms") != std::string::npos);

    MYE_EXPECT(m.CounterCount() == 1 && m.GaugeCount() == 1 && m.TimingCount() == 1);
    m.Reset();
    MYE_EXPECT(m.CounterCount() == 0 && m.GaugeCount() == 0 && m.TimingCount() == 0);
    MYE_EXPECT(m.GetCounter("ticks") == 0);
}
