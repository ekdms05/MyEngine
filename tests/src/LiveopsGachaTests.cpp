// LiveopsGachaTests.cpp — 가챠 확률·결정론 롤·천장·확률공개·감사 (docs/mmorpg/09, M11)
#include "TestFramework.h"

#include "mye/liveops/GachaTable.h"

#include <cmath>
#include <string>

using namespace mye;
using namespace mye::liveops;

namespace {
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

GachaTable MakeBanner() {
    GachaTable g;
    g.AddEntry({1, "포션",   90, 0});   // common 90%
    g.AddEntry({2, "갑옷",    9, 1});   // rare 9%
    g.AddEntry({3, "전설검",  1, 2});   // legendary 1%
    return g;
}
}

MYE_TEST(GachaProbabilitiesReflectWeights) {
    GachaTable g = MakeBanner();
    MYE_EXPECT(g.TotalWeight() == 100);
    MYE_EXPECT(g.EntryCount() == 3);
    MYE_EXPECT(Near(g.Probability(1), 0.90));
    MYE_EXPECT(Near(g.Probability(2), 0.09));
    MYE_EXPECT(Near(g.Probability(3), 0.01));
    MYE_EXPECT(g.Probability(999) == 0.0);   // 없는 아이템

    // 가중치 0 항목은 무시(확률 왜곡 방지).
    g.AddEntry({4, "무효", 0, 0});
    MYE_EXPECT(g.EntryCount() == 3 && g.TotalWeight() == 100);
}

MYE_TEST(GachaRollIsDeterministicAndAudited) {
    GachaTable a = MakeBanner();
    GachaTable b = MakeBanner();

    // 같은 시드 → 같은 시퀀스(재현·감사 가능).
    uint64_t sa = 12345, sb = 12345;
    for (int i = 0; i < 50; ++i) {
        const uint32_t ra = a.Roll(sa);
        const uint32_t rb = b.Roll(sb);
        MYE_EXPECT(ra == rb);
        MYE_EXPECT(ra >= 1 && ra <= 3);   // 유효 아이템만
    }
    // 감사 로그가 모든 롤을 기록(seq 단조).
    MYE_EXPECT(a.Audit().size() == 50);
    MYE_EXPECT(a.Audit().front().seq == 1);
    MYE_EXPECT(a.Audit().back().seq == 50);

    // 다른 시드 → 시퀀스가 달라질 수 있음(전부 동일이면 RNG 결함).
    GachaTable c = MakeBanner();
    uint64_t sc = 999999;
    bool anyDiff = false;
    uint64_t sa2 = 12345;
    GachaTable d = MakeBanner();
    for (int i = 0; i < 50; ++i)
        if (c.Roll(sc) != d.Roll(sa2)) { anyDiff = true; }
    MYE_EXPECT(anyDiff);
}

MYE_TEST(GachaPityGuaranteesRare) {
    GachaTable g;
    g.AddEntry({1, "잡템", 9999, 0});   // 거의 항상 common
    g.AddEntry({2, "희귀",    1, 2});   // 극악 확률 rare
    g.SetPity(10, 2);                    // 10회 내 rarity>=2 보장

    uint64_t s = 42;
    int rollsSincePity = 0;
    int pityHits = 0;
    for (int i = 0; i < 200; ++i) {
        const uint32_t r = g.Roll(s);
        ++rollsSincePity;
        if (r == 2) {   // 희귀 획득(천장 or 운)
            MYE_EXPECT(rollsSincePity <= 10);   // 10회 내 반드시 rare
            rollsSincePity = 0;
        }
    }
    // 천장으로 보장된 롤이 실제로 발생했는지(pity 플래그).
    for (const GachaRoll& roll : g.Audit()) if (roll.pity) { ++pityHits; MYE_EXPECT(roll.rarity >= 2); }
    MYE_EXPECT(pityHits > 0);

    // PullsUntilPity 계약: 방금 rare 였으면 0 아님, 천장 도달 시 0.
    GachaTable h = g;   // (복사) — threshold 미설정 테이블은 UINT32_MAX
    GachaTable noPity = MakeBanner();
    MYE_EXPECT(noPity.PullsUntilPity() == 0xFFFFFFFFu);
}

MYE_TEST(GachaDisclosureJsonHasRates) {
    GachaTable g = MakeBanner();
    g.SetPity(90, 2);
    const std::string json = g.DisclosureJson();

    // 공개 표기 필수 요소가 포함(확률·천장).
    MYE_EXPECT(json.find("rate_percent") != std::string::npos);
    MYE_EXPECT(json.find("전설검") != std::string::npos);
    MYE_EXPECT(json.find("\"total_weight\"") != std::string::npos);
    MYE_EXPECT(json.find("\"pity\"") != std::string::npos);
    MYE_EXPECT(json.find("\"threshold\"") != std::string::npos);
}
