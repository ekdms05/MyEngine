// mye/liveops/GachaTable.h — 확률형 아이템(가챠) 확률공개·서버 롤·천장 (docs/mmorpg/09, M11)
//
// 한국 게임산업진흥법의 확률 공개 의무를 코드로 지원한다:
//   (1) 확률표를 데이터로 보유(가중치/희귀도), (2) 서버가 결정론 시드로 롤(재현·감사 가능),
//   (3) 모든 롤을 감사 로그에 남김(분쟁 대응), (4) 공개용 확률 JSON 생성(공개 API/게임 내 표기),
//   (5) 천장(pity): N회 내 희귀 보장(과몰입 보호·법적 요구 대응).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mye::liveops {

struct GachaEntry {
    uint32_t    itemId = 0;
    std::string name;
    uint32_t    weight = 1;    // 상대 가중치(확률 = weight/총가중치)
    uint8_t     rarity = 0;    // 0=일반 … 클수록 희귀
};

// 감사 로그 한 줄(롤 재현·분쟁 대응).
struct GachaRoll {
    uint64_t seq = 0;
    uint32_t itemId = 0;
    uint8_t  rarity = 0;
    bool     pity = false;     // 천장으로 보장된 결과인가
};

class GachaTable {
public:
    void AddEntry(const GachaEntry& e);

    // 천장 설정: 마지막 희귀 이후 threshold 회 롤 안에 rarity>=pityRarity 를 보장. threshold=0=끄기.
    void SetPity(uint32_t threshold, uint8_t pityRarity) { m_pityThreshold = threshold; m_pityRarity = pityRarity; }

    uint64_t TotalWeight() const { return m_totalWeight; }
    size_t   EntryCount() const { return m_entries.size(); }

    // 공개 확률(itemId 의 확률 0..1). 없으면 0.
    double Probability(uint32_t itemId) const;

    // 결정론 롤: rngState 를 진행시키며 한 번 뽑는다(서버권위). 결과 itemId 반환.
    //   같은 초기 시드 → 같은 시퀀스(재현·감사). rngState 0 은 내부에서 보정.
    uint32_t Roll(uint64_t& rngState);

    // 천장까지 남은 롤 수(0=다음 롤이 천장 보장). threshold=0 이면 UINT32_MAX.
    uint32_t PullsUntilPity() const;

    // 공개 확률표 JSON(게임법 준수 표기·공개 API). 각 항목 rate_percent 포함 + 천장 정보.
    std::string DisclosureJson() const;

    const std::vector<GachaRoll>& Audit() const { return m_audit; }
    void ClearAudit() { m_audit.clear(); }

private:
    uint64_t RareWeight() const;   // rarity>=pityRarity 항목 가중치 합

    std::vector<GachaEntry> m_entries;
    std::vector<GachaRoll>  m_audit;
    uint64_t m_totalWeight = 0;
    uint64_t m_rollSeq = 1;
    uint32_t m_pityThreshold = 0;
    uint8_t  m_pityRarity = 0;
    uint32_t m_sinceRare = 0;   // 마지막 희귀 이후 롤 수
};

} // namespace mye::liveops
