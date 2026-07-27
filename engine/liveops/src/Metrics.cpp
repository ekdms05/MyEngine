// mye/liveops/Metrics.cpp — 메트릭 수집기 구현 (Metrics.h 참조)
#include "mye/liveops/Metrics.h"

#include "mye/core/Json.h"

namespace mye::liveops {

void MetricsRegistry::Counter(std::string_view name, int64_t delta) {
    auto it = m_counters.find(name);
    if (it == m_counters.end()) m_counters.emplace(std::string(name), delta);
    else it->second += delta;
}

int64_t MetricsRegistry::GetCounter(std::string_view name) const {
    auto it = m_counters.find(name);
    return it == m_counters.end() ? 0 : it->second;
}

void MetricsRegistry::Gauge(std::string_view name, double value) {
    auto it = m_gauges.find(name);
    if (it == m_gauges.end()) m_gauges.emplace(std::string(name), value);
    else it->second = value;
}

double MetricsRegistry::GetGauge(std::string_view name) const {
    auto it = m_gauges.find(name);
    return it == m_gauges.end() ? 0.0 : it->second;
}

void MetricsRegistry::Observe(std::string_view name, double sample) {
    auto it = m_timings.find(name);
    if (it == m_timings.end()) {
        TimingStat s;
        s.count = 1; s.sum = sample; s.min = sample; s.max = sample;
        m_timings.emplace(std::string(name), s);
    } else {
        TimingStat& s = it->second;
        ++s.count;
        s.sum += sample;
        if (sample < s.min) s.min = sample;
        if (sample > s.max) s.max = sample;
    }
}

TimingStat MetricsRegistry::GetStat(std::string_view name) const {
    auto it = m_timings.find(name);
    return it == m_timings.end() ? TimingStat{} : it->second;
}

std::string MetricsRegistry::SnapshotJson() const {
    json::Value::Object counters;
    for (const auto& [k, v] : m_counters) counters[k] = json::Value(static_cast<std::int64_t>(v));
    json::Value::Object gauges;
    for (const auto& [k, v] : m_gauges) gauges[k] = json::Value(v);
    json::Value::Object timings;
    for (const auto& [k, s] : m_timings) {
        json::Value::Object t;
        t["count"] = json::Value(static_cast<std::int64_t>(s.count));
        t["avg"]   = json::Value(s.Avg());
        t["min"]   = json::Value(s.min);
        t["max"]   = json::Value(s.max);
        timings[k] = json::Value(std::move(t));
    }
    json::Value::Object root;
    root["counters"] = json::Value(std::move(counters));
    root["gauges"]   = json::Value(std::move(gauges));
    root["timings"]  = json::Value(std::move(timings));
    return json::Stringify(json::Value(std::move(root)));
}

void MetricsRegistry::Reset() {
    m_counters.clear();
    m_gauges.clear();
    m_timings.clear();
}

} // namespace mye::liveops
