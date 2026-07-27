// mye/liveops/GachaTable.cpp — 가챠 확률·롤·천장 구현 (GachaTable.h 참조)
#include "mye/liveops/GachaTable.h"

#include "mye/core/Json.h"

#include <format>
#include <limits>

namespace mye::liveops {

namespace {
// xorshift64* — 결정론 시드 진행(전투 RNG와 동일 계열). s==0 은 호출부에서 보정.
uint64_t NextRandom(uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
}
} // namespace

void GachaTable::AddEntry(const GachaEntry& e) {
    if (e.weight == 0) return;   // 확률 0 항목은 무의미
    m_entries.push_back(e);
    m_totalWeight += e.weight;
}

double GachaTable::Probability(uint32_t itemId) const {
    if (m_totalWeight == 0) return 0.0;
    uint64_t w = 0;
    for (const GachaEntry& e : m_entries) if (e.itemId == itemId) w += e.weight;
    return static_cast<double>(w) / static_cast<double>(m_totalWeight);
}

uint64_t GachaTable::RareWeight() const {
    uint64_t w = 0;
    for (const GachaEntry& e : m_entries) if (e.rarity >= m_pityRarity) w += e.weight;
    return w;
}

uint32_t GachaTable::PullsUntilPity() const {
    if (m_pityThreshold == 0) return std::numeric_limits<uint32_t>::max();
    // threshold 번째 뽑기에 보장 → 남은 = (threshold-1) - sinceRare, 0=다음 롤이 천장.
    return m_sinceRare + 1 >= m_pityThreshold ? 0 : (m_pityThreshold - 1 - m_sinceRare);
}

uint32_t GachaTable::Roll(uint64_t& rngState) {
    if (m_entries.empty() || m_totalWeight == 0) return 0;
    if (rngState == 0) rngState = 0x9E3779B97F4A7C15ull;   // 시드 0 보정

    // 천장 발동: threshold 번째 뽑기(= 직전까지 threshold-1 연속 비희귀)에 희귀 항목으로만 롤.
    const uint64_t rareW = RareWeight();
    const bool forcePity = m_pityThreshold > 0 && m_sinceRare + 1 >= m_pityThreshold && rareW > 0;
    const uint64_t total = forcePity ? rareW : m_totalWeight;

    const uint64_t r = NextRandom(rngState) % total;
    uint32_t pickedId = 0;
    uint8_t  pickedRarity = 0;
    uint64_t acc = 0;
    for (const GachaEntry& e : m_entries) {
        if (forcePity && e.rarity < m_pityRarity) continue;
        acc += e.weight;
        if (r < acc) { pickedId = e.itemId; pickedRarity = e.rarity; break; }
    }

    const bool isRare = pickedRarity >= m_pityRarity;
    if (m_pityThreshold > 0) {
        if (isRare) m_sinceRare = 0;
        else ++m_sinceRare;
    }
    m_audit.push_back(GachaRoll{ m_rollSeq++, pickedId, pickedRarity, forcePity });
    return pickedId;
}

std::string GachaTable::DisclosureJson() const {
    json::Value::Array items;
    for (const GachaEntry& e : m_entries) {
        json::Value::Object o;
        o["item_id"]     = json::Value(static_cast<std::int64_t>(e.itemId));
        o["name"]        = json::Value(e.name);
        o["rarity"]      = json::Value(static_cast<std::int64_t>(e.rarity));
        o["weight"]      = json::Value(static_cast<std::int64_t>(e.weight));
        const double pct = m_totalWeight ? (static_cast<double>(e.weight) / static_cast<double>(m_totalWeight) * 100.0) : 0.0;
        o["rate_percent"] = json::Value(pct);
        items.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["items"]        = json::Value(std::move(items));
    root["total_weight"] = json::Value(static_cast<std::int64_t>(m_totalWeight));
    json::Value::Object pity;
    pity["enabled"]       = json::Value(m_pityThreshold > 0);
    pity["threshold"]     = json::Value(static_cast<std::int64_t>(m_pityThreshold));
    pity["min_rarity"]    = json::Value(static_cast<std::int64_t>(m_pityRarity));
    root["pity"]          = json::Value(std::move(pity));
    return json::Stringify(json::Value(std::move(root)));
}

} // namespace mye::liveops
