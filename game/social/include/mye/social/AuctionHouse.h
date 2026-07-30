// mye/social/AuctionHouse.h — 경매장(즉시구매 마켓) (docs/mmorpg/03·04, M12 경제)
//
// [게임 레이어] 판매자가 아이템을 가격에 등록(escrow 잠금), 구매자가 골드를 지불하면 아이템↔골드가
// 원자적으로 교환된다. 모든 이동은 engine/persist ItemLedger Transfer 경유(보존·복제불가).
#pragma once

#include "mye/persist/ItemLedger.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::social {

using ListingId = uint64_t;

struct Listing {
    ListingId id = 0;
    uint64_t  seller = 0;    // char
    uint32_t  itemId = 0;
    int32_t   count = 0;
    int64_t   price = 0;     // 즉시구매 골드
    bool      sold = false;
    bool      cancelled = false;
    uint64_t  buyer = 0;     // 판매 시 구매자
    bool      Active() const { return !sold && !cancelled; }
};

class AuctionHouse {
public:
    explicit AuctionHouse(persist::ItemLedger& ledger) : m_ledger(ledger) {}

    // 등록물 escrow 원장 계정(2^60 비트 — 거래(2^61)·우편(2^62)·길드(2^63)·캐릭터와 분리).
    static uint64_t EscrowAccount(ListingId l) { return (uint64_t{1} << 60) | l; }

    // 등록: 판매자 아이템을 escrow 로 잠금. 아이템 부족/무효면 0.
    ListingId List(uint64_t seller, uint32_t itemId, int32_t count, int64_t price);

    // 즉시구매: 구매자 골드→판매자, 아이템 escrow→구매자(원자). 골드 부족/이미판매/본인구매면 false.
    bool Buy(ListingId id, uint64_t buyer);

    // 판매자 취소: escrow 아이템 반환. 판매 완료면 불가.
    bool Cancel(ListingId id, uint64_t bySeller);

    // ---- 조회 ----
    std::vector<ListingId> Active() const;
    std::vector<ListingId> ActiveByItem(uint32_t itemId) const;
    const Listing* Get(ListingId id) const;
    size_t Count() const { return m_listings.size(); }

private:
    persist::ItemLedger&                     m_ledger;
    std::unordered_map<ListingId, Listing>   m_listings;
    std::vector<ListingId>                    m_order;   // 등록 순서
    ListingId m_nextId = 1;
};

} // namespace mye::social
