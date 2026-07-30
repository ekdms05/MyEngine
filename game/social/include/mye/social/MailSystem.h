// mye/social/MailSystem.h — 우편(오프라인 배송·escrow) (docs/mmorpg/03·04, M12 소셜)
//
// [게임 레이어] 오프라인 플레이어에게 골드/아이템을 첨부해 보낸다. 첨부는 발송 시 원장(ItemLedger)
// escrow 계정으로 잠기고(보존), 수령 시 수신자에게 이관된다 — 복제/유실 없음(dupe-safe).
#pragma once

#include "mye/persist/ItemLedger.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mye::social {

using MailId = uint64_t;

// 우편 첨부 아이템 한 칸.
struct MailItem {
    uint32_t itemId = 0;
    int32_t  count = 0;
};

struct Mail {
    MailId      id = 0;
    uint64_t    fromChar = 0;
    uint64_t    toChar = 0;
    std::string subject;
    std::string body;
    int64_t     gold = 0;
    std::vector<MailItem> items;
    bool        claimed = false;
    bool        HasAttachment() const { return gold > 0 || !items.empty(); }
};

class MailSystem {
public:
    explicit MailSystem(persist::ItemLedger& ledger) : m_ledger(ledger) {}

    // 우편별 escrow 원장 계정(2^62 비트 — 길드금고(2^63)·캐릭터와 분리).
    static uint64_t EscrowAccount(MailId m) { return (uint64_t{1} << 62) | m; }

    // 발송. 첨부가 있으면 발신자 잔고 검증 후 escrow 로 이관(부족 시 mailId=0, 아무것도 안 옮김).
    MailId Send(uint64_t fromChar, uint64_t toChar, std::string subject, std::string body,
                int64_t gold, const std::vector<MailItem>& items);

    // 수령: 미수령 + 수신자 본인이면 첨부를 escrow→수신자 이관 후 claimed. 실패 시 false.
    bool Claim(MailId id, uint64_t byChar);

    // 수신함(미수령 우편 id, 발송 순).
    std::vector<MailId> Inbox(uint64_t toChar) const;

    const Mail* Get(MailId id) const;
    size_t Count() const { return m_mails.size(); }

private:
    persist::ItemLedger&              m_ledger;
    std::unordered_map<MailId, Mail>  m_mails;
    std::vector<MailId>               m_order;    // 발송 순서(수신함 정렬)
    MailId m_nextId = 1;
};

} // namespace mye::social
