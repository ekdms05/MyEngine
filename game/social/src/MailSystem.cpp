// mye/social/MailSystem.cpp — 우편 시스템 구현 (MailSystem.h 참조)
#include "mye/social/MailSystem.h"

namespace mye::social {

MailId MailSystem::Send(uint64_t fromChar, uint64_t toChar, std::string subject, std::string body,
                        int64_t gold, const std::vector<MailItem>& items) {
    if (toChar == 0) return 0;
    if (gold < 0) return 0;

    // 첨부 사전 검증(부분 이관 방지 — 전부 가능해야 발송).
    if (gold > 0 && m_ledger.GoldBalance(fromChar) < gold) return 0;
    for (const MailItem& it : items) {
        if (it.count <= 0 || it.itemId == 0) return 0;
        if (m_ledger.ItemBalance(fromChar, it.itemId) < it.count) return 0;
    }

    const MailId id = m_nextId++;

    // 검증 통과 → escrow 로 이관(단일 스레드 → 실패 없음).
    const uint64_t escrow = EscrowAccount(id);
    if (gold > 0) (void)m_ledger.Transfer(fromChar, escrow, 0, 0, gold, "mail_send_gold");
    for (const MailItem& it : items)
        (void)m_ledger.Transfer(fromChar, escrow, it.itemId, it.count, 0, "mail_send_item");

    Mail m;
    m.id = id; m.fromChar = fromChar; m.toChar = toChar;
    m.subject = std::move(subject); m.body = std::move(body);
    m.gold = gold; m.items = items;
    m_mails.emplace(id, std::move(m));
    m_order.push_back(id);
    return id;
}

bool MailSystem::Claim(MailId id, uint64_t byChar) {
    auto it = m_mails.find(id);
    if (it == m_mails.end()) return false;
    Mail& m = it->second;
    if (m.claimed || m.toChar != byChar) return false;

    // 첨부를 escrow → 수신자 이관.
    const uint64_t escrow = EscrowAccount(id);
    if (m.gold > 0) (void)m_ledger.Transfer(escrow, byChar, 0, 0, m.gold, "mail_claim_gold");
    for (const MailItem& att : m.items)
        (void)m_ledger.Transfer(escrow, byChar, att.itemId, att.count, 0, "mail_claim_item");

    m.claimed = true;
    return true;
}

std::vector<MailId> MailSystem::Inbox(uint64_t toChar) const {
    std::vector<MailId> out;
    for (MailId id : m_order) {
        auto it = m_mails.find(id);
        if (it != m_mails.end() && it->second.toChar == toChar && !it->second.claimed)
            out.push_back(id);
    }
    return out;
}

const Mail* MailSystem::Get(MailId id) const {
    auto it = m_mails.find(id);
    return it == m_mails.end() ? nullptr : &it->second;
}

} // namespace mye::social
