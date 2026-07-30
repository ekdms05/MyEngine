// mye/social/TradeSystem.cpp — 2자 직접거래 구현 (TradeSystem.h 참조)
#include "mye/social/TradeSystem.h"

namespace mye::social {

TradeSystem::Trade*       TradeSystem::Find(TradeId t) {
    auto it = m_trades.find(t); return it == m_trades.end() ? nullptr : &it->second;
}
const TradeSystem::Trade* TradeSystem::Find(TradeId t) const {
    auto it = m_trades.find(t); return it == m_trades.end() ? nullptr : &it->second;
}

TradeSystem::Offer* TradeSystem::OfferOf(Trade& tr, uint64_t who) {
    if (who == tr.charA) return &tr.offerA;
    if (who == tr.charB) return &tr.offerB;
    return nullptr;
}

TradeId TradeSystem::Begin(uint64_t charA, uint64_t charB) {
    if (charA == 0 || charB == 0 || charA == charB) return 0;
    if (m_charTrade.count(charA) || m_charTrade.count(charB)) return 0;
    const TradeId id = m_nextId++;
    Trade tr;
    tr.id = id; tr.charA = charA; tr.charB = charB;
    m_trades.emplace(id, std::move(tr));
    m_charTrade[charA] = id;
    m_charTrade[charB] = id;
    return id;
}

bool TradeSystem::SetGold(TradeId t, uint64_t who, int64_t gold) {
    Trade* tr = Find(t);
    if (!tr || !tr->active || gold < 0) return false;
    Offer* off = OfferOf(*tr, who);
    if (!off) return false;
    const uint64_t escrow = EscrowAccount(t);
    const int64_t delta = gold - off->gold;
    if (delta > 0) {
        if (!m_ledger.Transfer(who, escrow, 0, 0, delta, "trade_offer_gold")) return false;   // 잔고 부족
    } else if (delta < 0) {
        (void)m_ledger.Transfer(escrow, who, 0, 0, -delta, "trade_reduce_gold");
    }
    off->gold = gold;
    tr->offerA.confirmed = false; tr->offerB.confirmed = false;   // 변경 → 재확인 필요
    return true;
}

bool TradeSystem::AddItem(TradeId t, uint64_t who, uint32_t itemId, int32_t count) {
    Trade* tr = Find(t);
    if (!tr || !tr->active || itemId == 0 || count <= 0) return false;
    Offer* off = OfferOf(*tr, who);
    if (!off) return false;
    const uint64_t escrow = EscrowAccount(t);
    if (!m_ledger.Transfer(who, escrow, itemId, count, 0, "trade_offer_item")) return false;   // 잔고 부족
    off->items.push_back(TradeStack{ itemId, count });
    tr->offerA.confirmed = false; tr->offerB.confirmed = false;
    return true;
}

bool TradeSystem::Confirm(TradeId t, uint64_t who) {
    Trade* tr = Find(t);
    if (!tr || !tr->active) return false;
    Offer* off = OfferOf(*tr, who);
    if (!off) return false;
    off->confirmed = true;
    if (tr->offerA.confirmed && tr->offerB.confirmed) Execute(*tr);
    return true;
}

bool TradeSystem::Cancel(TradeId t, uint64_t who) {
    Trade* tr = Find(t);
    if (!tr || !tr->active) return false;
    if (!OfferOf(*tr, who)) return false;
    ReturnEscrow(*tr);
    tr->active = false;
    m_charTrade.erase(tr->charA);
    m_charTrade.erase(tr->charB);
    return true;
}

void TradeSystem::Execute(Trade& tr) {
    const uint64_t escrow = EscrowAccount(tr.id);
    // A 가 내건 것 → B, B 가 내건 것 → A.
    if (tr.offerA.gold > 0) (void)m_ledger.Transfer(escrow, tr.charB, 0, 0, tr.offerA.gold, "trade_settle_gold");
    for (const TradeStack& s : tr.offerA.items) (void)m_ledger.Transfer(escrow, tr.charB, s.itemId, s.count, 0, "trade_settle_item");
    if (tr.offerB.gold > 0) (void)m_ledger.Transfer(escrow, tr.charA, 0, 0, tr.offerB.gold, "trade_settle_gold");
    for (const TradeStack& s : tr.offerB.items) (void)m_ledger.Transfer(escrow, tr.charA, s.itemId, s.count, 0, "trade_settle_item");
    tr.active = false;
    tr.complete = true;
    m_charTrade.erase(tr.charA);
    m_charTrade.erase(tr.charB);
}

void TradeSystem::ReturnEscrow(Trade& tr) {
    const uint64_t escrow = EscrowAccount(tr.id);
    if (tr.offerA.gold > 0) (void)m_ledger.Transfer(escrow, tr.charA, 0, 0, tr.offerA.gold, "trade_return_gold");
    for (const TradeStack& s : tr.offerA.items) (void)m_ledger.Transfer(escrow, tr.charA, s.itemId, s.count, 0, "trade_return_item");
    if (tr.offerB.gold > 0) (void)m_ledger.Transfer(escrow, tr.charB, 0, 0, tr.offerB.gold, "trade_return_gold");
    for (const TradeStack& s : tr.offerB.items) (void)m_ledger.Transfer(escrow, tr.charB, s.itemId, s.count, 0, "trade_return_item");
}

bool TradeSystem::IsActive(TradeId t) const { const Trade* tr = Find(t); return tr && tr->active; }
bool TradeSystem::IsComplete(TradeId t) const { const Trade* tr = Find(t); return tr && tr->complete; }

int64_t TradeSystem::OfferGold(TradeId t, uint64_t who) const {
    const Trade* tr = Find(t);
    if (!tr) return 0;
    if (who == tr->charA) return tr->offerA.gold;
    if (who == tr->charB) return tr->offerB.gold;
    return 0;
}

bool TradeSystem::Confirmed(TradeId t, uint64_t who) const {
    const Trade* tr = Find(t);
    if (!tr) return false;
    if (who == tr->charA) return tr->offerA.confirmed;
    if (who == tr->charB) return tr->offerB.confirmed;
    return false;
}

size_t TradeSystem::ActiveCount() const {
    size_t n = 0;
    for (const auto& [id, tr] : m_trades) { (void)id; if (tr.active) ++n; }
    return n;
}

} // namespace mye::social
