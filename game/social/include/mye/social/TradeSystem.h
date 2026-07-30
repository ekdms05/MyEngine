// mye/social/TradeSystem.h — 2자 직접거래(escrow·확인) (docs/mmorpg/03·04, M12 경제)
//
// [게임 레이어] 두 캐릭터가 서로 골드/아이템을 내걸고 양쪽 확인 시 원자적으로 교환한다. 내건 것은
// 즉시 원장 escrow 에 잠기고(스캠·복제 방지), 제안 변경 시 양쪽 확인이 리셋된다(변경 후 몰래 성사 방지).
// 취소하면 escrow 가 되돌아온다. 모든 이동은 engine/persist ItemLedger Transfer 경유(보존).
#pragma once

#include "mye/persist/ItemLedger.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::social {

using TradeId = uint64_t;

struct TradeStack { uint32_t itemId = 0; int32_t count = 0; };

class TradeSystem {
public:
    explicit TradeSystem(persist::ItemLedger& ledger) : m_ledger(ledger) {}

    // 거래별 escrow 원장 계정(2^61 비트 — 우편(2^62)·길드(2^63)·캐릭터와 분리).
    static uint64_t EscrowAccount(TradeId t) { return (uint64_t{1} << 61) | t; }

    // 거래 시작. 둘 중 하나라도 이미 거래 중이면 0.
    TradeId Begin(uint64_t charA, uint64_t charB);

    // 내 제안 골드 설정(총액). 델타만큼 escrow 로 이동, 잔고 부족 시 실패(무변경). 성공 시 양쪽 확인 리셋.
    bool SetGold(TradeId t, uint64_t who, int64_t gold);
    // 내 제안 아이템 추가(escrow 로 이동). 잔고 부족 시 실패. 성공 시 확인 리셋.
    bool AddItem(TradeId t, uint64_t who, uint32_t itemId, int32_t count);

    // 내 확인. 양쪽 확인되면 자동 실행(원자 스왑) → 완료.
    bool Confirm(TradeId t, uint64_t who);
    // 취소(어느 쪽이든) → escrow 원소유자 반환.
    bool Cancel(TradeId t, uint64_t who);

    // ---- 조회 ----
    bool    IsActive(TradeId t) const;
    bool    IsComplete(TradeId t) const;
    int64_t OfferGold(TradeId t, uint64_t who) const;
    bool    Confirmed(TradeId t, uint64_t who) const;
    size_t  ActiveCount() const;

private:
    struct Offer {
        int64_t                 gold = 0;
        std::vector<TradeStack> items;
        bool                    confirmed = false;
    };
    struct Trade {
        TradeId  id = 0;
        uint64_t charA = 0, charB = 0;
        Offer    offerA, offerB;
        bool     active = true;
        bool     complete = false;
    };
    Trade*       Find(TradeId t);
    const Trade* Find(TradeId t) const;
    // who → (trade, myOffer, myChar). who 가 참가자 아니면 nullptr.
    Offer* OfferOf(Trade& tr, uint64_t who);

    void Execute(Trade& tr);   // 양쪽 escrow 교차 이관
    void ReturnEscrow(Trade& tr);

    persist::ItemLedger&                   m_ledger;
    std::unordered_map<TradeId, Trade>     m_trades;
    std::unordered_map<uint64_t, TradeId>  m_charTrade;   // 참가자 → 활성 거래
    TradeId m_nextId = 1;
};

} // namespace mye::social
