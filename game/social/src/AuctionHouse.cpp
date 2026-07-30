// mye/social/AuctionHouse.cpp — 경매장 구현 (AuctionHouse.h 참조)
#include "mye/social/AuctionHouse.h"

namespace mye::social {

ListingId AuctionHouse::List(uint64_t seller, uint32_t itemId, int32_t count, int64_t price) {
    if (seller == 0 || itemId == 0 || count <= 0 || price < 0) return 0;
    if (m_ledger.ItemBalance(seller, itemId) < count) return 0;   // 아이템 부족

    const ListingId id = m_nextId++;
    (void)m_ledger.Transfer(seller, EscrowAccount(id), itemId, count, 0, "auction_list");   // escrow 잠금

    Listing l;
    l.id = id; l.seller = seller; l.itemId = itemId; l.count = count; l.price = price;
    m_listings.emplace(id, l);
    m_order.push_back(id);
    return id;
}

bool AuctionHouse::Buy(ListingId id, uint64_t buyer) {
    auto it = m_listings.find(id);
    if (it == m_listings.end()) return false;
    Listing& l = it->second;
    if (!l.Active() || buyer == 0 || buyer == l.seller) return false;
    if (m_ledger.GoldBalance(buyer) < l.price) return false;   // 골드 부족

    // 원자: 구매자 골드→판매자, 아이템 escrow→구매자.
    if (l.price > 0) (void)m_ledger.Transfer(buyer, l.seller, 0, 0, l.price, "auction_pay");
    (void)m_ledger.Transfer(EscrowAccount(id), buyer, l.itemId, l.count, 0, "auction_deliver");

    l.sold = true;
    l.buyer = buyer;
    return true;
}

bool AuctionHouse::Cancel(ListingId id, uint64_t bySeller) {
    auto it = m_listings.find(id);
    if (it == m_listings.end()) return false;
    Listing& l = it->second;
    if (!l.Active() || l.seller != bySeller) return false;
    (void)m_ledger.Transfer(EscrowAccount(id), l.seller, l.itemId, l.count, 0, "auction_cancel");
    l.cancelled = true;
    return true;
}

std::vector<ListingId> AuctionHouse::Active() const {
    std::vector<ListingId> out;
    for (ListingId id : m_order) {
        auto it = m_listings.find(id);
        if (it != m_listings.end() && it->second.Active()) out.push_back(id);
    }
    return out;
}

std::vector<ListingId> AuctionHouse::ActiveByItem(uint32_t itemId) const {
    std::vector<ListingId> out;
    for (ListingId id : m_order) {
        auto it = m_listings.find(id);
        if (it != m_listings.end() && it->second.Active() && it->second.itemId == itemId) out.push_back(id);
    }
    return out;
}

const Listing* AuctionHouse::Get(ListingId id) const {
    auto it = m_listings.find(id);
    return it == m_listings.end() ? nullptr : &it->second;
}

} // namespace mye::social
