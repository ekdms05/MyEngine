// mye/persist/ItemLedger.cpp — 거래 원장 구현 (ItemLedger.h 참조)
#include "mye/persist/ItemLedger.h"

#include "JsonFile.h"


namespace mye::persist {

namespace {
bool CanAdjustBalance(int64_t balance, int64_t delta) {
    return balance >= 0 && (delta < 0 ? delta >= -balance : delta <= INT64_MAX - balance);
}
}

uint64_t ItemLedger::Append(CharacterId who, LedgerOp op, uint32_t itemId, int32_t itemDelta,
                            int64_t goldDelta, uint64_t txnId, std::string_view reason) {
    LedgerEntry e;
    e.seq       = m_nextSeq++;
    e.character = who;
    e.op        = op;
    e.itemId    = itemId;
    e.itemDelta = itemDelta;
    e.goldDelta = goldDelta;
    e.txnId     = txnId;
    e.reason    = std::string(reason);
    m_entries.push_back(std::move(e));
    return m_entries.back().seq;
}

Expected<uint64_t, Error> ItemLedger::Grant(CharacterId who, uint32_t itemId, int32_t count, std::string_view reason) {
    if (who == 0)     return Error{"Grant: 유효하지 않은 캐릭터", 1};
    if (itemId == 0)  return Error{"Grant: itemId=0", 2};
    if (count <= 0)   return Error{"Grant: count 는 양수여야 함", 3};
    if (!CanAdjustBalance(ItemBalance(who, itemId), count))
        return Error{"Grant: item balance overflow", 4};
    return Append(who, LedgerOp::Grant, itemId, count, 0, 0, reason);
}

Expected<uint64_t, Error> ItemLedger::Consume(CharacterId who, uint32_t itemId, int32_t count, std::string_view reason) {
    if (who == 0)     return Error{"Consume: 유효하지 않은 캐릭터", 1};
    if (itemId == 0)  return Error{"Consume: itemId=0", 2};
    if (count <= 0)   return Error{"Consume: count 는 양수여야 함", 3};
    if (ItemBalance(who, itemId) < count)
        return Error{"Consume: 보유량 부족(잔고 음수 방지)", 4};
    return Append(who, LedgerOp::Consume, itemId, -count, 0, 0, reason);
}

Expected<uint64_t, Error> ItemLedger::AdjustGold(CharacterId who, int64_t delta, std::string_view reason) {
    if (who == 0)   return Error{"AdjustGold: 유효하지 않은 캐릭터", 1};
    if (delta == 0) return Error{"AdjustGold: delta=0", 2};
    if (!CanAdjustBalance(GoldBalance(who), delta))
        return Error{"AdjustGold: insufficient balance or overflow", 3};
    return Append(who, LedgerOp::GoldOnly, 0, 0, delta, 0, reason);
}

Expected<uint64_t, Error> ItemLedger::Transfer(CharacterId from, CharacterId to,
                                               uint32_t itemId, int32_t count, int64_t gold,
                                               std::string_view reason) {
    if (from == 0 || to == 0) return Error{"Transfer: 유효하지 않은 캐릭터", 1};
    if (from == to)           return Error{"Transfer: 동일 캐릭터 간 이동 불가", 2};
    if (count < 0 || gold < 0) return Error{"Transfer: 음수 수량/골드", 3};
    if (count == 0 && gold == 0) return Error{"Transfer: 이동할 것이 없음", 4};
    if (count > 0 && itemId == 0) return Error{"Transfer: itemId=0 인데 count>0", 5};

    // 사전 검증: from 의 아이템·골드 잔고가 충분한가(원자성 보장 — 실패 시 아무것도 기록 안 함).
    if (count > 0 && ItemBalance(from, itemId) < count)
        return Error{"Transfer: 송신자 아이템 부족", 6};
    if (gold > 0 && GoldBalance(from) < gold)
        return Error{"Transfer: 송신자 골드 부족", 7};
    if (!CanAdjustBalance(GoldBalance(to), gold) ||
        (count > 0 && !CanAdjustBalance(ItemBalance(to, itemId), count)))
        return Error{"Transfer: recipient balance overflow", 8};

    // 원자적: 같은 txnId 로 대칭 2건을 함께 기록(보존 → 복제 불가).
    const uint64_t txn = m_nextTxn++;
    Append(from, LedgerOp::TradeGive, itemId, count > 0 ? -count : 0, gold > 0 ? -gold : 0, txn, reason);
    Append(to,   LedgerOp::TradeRecv, itemId, count > 0 ?  count : 0, gold > 0 ?  gold : 0, txn, reason);
    return txn;
}

LedgerBalance ItemLedger::Balance(CharacterId who) const {
    LedgerBalance bal;
    for (const LedgerEntry& e : m_entries) {
        if (e.character != who) continue;
        if (e.itemId != 0 && e.itemDelta != 0) bal.items[e.itemId] += e.itemDelta;
        bal.gold += e.goldDelta;
    }
    // 0 이하 항목 제거(깔끔한 잔고).
    for (auto it = bal.items.begin(); it != bal.items.end();) {
        if (it->second <= 0) it = bal.items.erase(it);
        else ++it;
    }
    return bal;
}

int64_t ItemLedger::ItemBalance(CharacterId who, uint32_t itemId) const {
    int64_t sum = 0;
    for (const LedgerEntry& e : m_entries)
        if (e.character == who && e.itemId == itemId) sum += e.itemDelta;
    return sum;
}

int64_t ItemLedger::GoldBalance(CharacterId who) const {
    int64_t sum = 0;
    for (const LedgerEntry& e : m_entries)
        if (e.character == who) sum += e.goldDelta;
    return sum;
}

bool ItemLedger::VerifyIntegrity(uint64_t* outFirstBadSeq) const {
    // 기록 순서(seq)대로 리플레이하며 running 잔고가 음수로 내려가는 지점을 탐지.
    std::unordered_map<CharacterId, int64_t> gold;                                 // char → gold
    std::unordered_map<CharacterId, std::unordered_map<uint32_t, int64_t>> items;  // char → (item → count)
    for (const LedgerEntry& e : m_entries) {
        if (e.goldDelta != 0) {
            int64_t& g = gold[e.character];
            if (!CanAdjustBalance(g, e.goldDelta)) { if (outFirstBadSeq) *outFirstBadSeq = e.seq; return false; }
            g += e.goldDelta;
        }
        if (e.itemId != 0 && e.itemDelta != 0) {
            int64_t& c = items[e.character][e.itemId];
            if (!CanAdjustBalance(c, e.itemDelta)) { if (outFirstBadSeq) *outFirstBadSeq = e.seq; return false; }
            c += e.itemDelta;
        }
    }
    return true;
}

namespace {
const char* OpName(LedgerOp op) {
    switch (op) {
        case LedgerOp::Grant:     return "grant";
        case LedgerOp::Consume:   return "consume";
        case LedgerOp::TradeGive: return "give";
        case LedgerOp::TradeRecv: return "recv";
        case LedgerOp::GoldOnly:  return "gold";
    }
    return "grant";
}
LedgerOp OpFromName(std::string_view s) {
    if (s == "consume") return LedgerOp::Consume;
    if (s == "give")    return LedgerOp::TradeGive;
    if (s == "recv")    return LedgerOp::TradeRecv;
    if (s == "gold")    return LedgerOp::GoldOnly;
    return LedgerOp::Grant;
}
} // namespace

json::Value ItemLedger::ToJson() const {
    json::Value::Array arr;
    for (const LedgerEntry& e : m_entries) {
        json::Value::Object o;
        o["seq"]  = json::Value(static_cast<std::int64_t>(e.seq));
        o["char"] = json::Value(static_cast<std::int64_t>(e.character));
        o["op"]   = json::Value(std::string(OpName(e.op)));
        o["item"] = json::Value(static_cast<std::int64_t>(e.itemId));
        o["di"]   = json::Value(static_cast<std::int64_t>(e.itemDelta));
        o["dg"]   = json::Value(static_cast<std::int64_t>(e.goldDelta));
        o["txn"]  = json::Value(static_cast<std::int64_t>(e.txnId));
        if (!e.reason.empty()) o["why"] = json::Value(e.reason);
        arr.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["entries"] = json::Value(std::move(arr));
    root["nextSeq"] = json::Value(static_cast<std::int64_t>(m_nextSeq));
    root["nextTxn"] = json::Value(static_cast<std::int64_t>(m_nextTxn));
    return json::Value(std::move(root));
}

Expected<void, Error> ItemLedger::SaveToFile(std::string_view path) const {
    return detail::WriteJsonFile(detail::Utf8Path(path), ToJson());
}

Expected<void, Error> ItemLedger::LoadFromFile(std::string_view path) {
    auto parsed = detail::ReadJsonFile(detail::Utf8Path(path));
    if (!parsed) return parsed.GetError();
    return LoadJson(parsed.Value());
}

Expected<void, Error> ItemLedger::LoadJson(const json::Value& root) {
    ItemLedger candidate;
    candidate.m_entries.clear();
    const json::Value* arr = root.Find("entries");
    if (!arr || !arr->IsArray()) return Error{"ItemLedger: missing entries array", 1};
    {
        for (const json::Value& v : arr->AsArray()) {
            if (!v.IsObject()) return Error{"ItemLedger: invalid record", 1};
            LedgerEntry e;
            if (!detail::IntegerInRange(v, "seq", 1, INT64_MAX - 1) ||
                !detail::IntegerInRange(v, "char", 1, INT64_MAX - 1) ||
                !detail::IntegerInRange(v, "item", 0, UINT32_MAX) ||
                !detail::IntegerInRange(v, "di", -INT32_MAX, INT32_MAX) ||
                !detail::IntegerInRange(v, "dg", -INT64_MAX, INT64_MAX) ||
                !detail::IntegerInRange(v, "txn", 0, INT64_MAX - 1))
                return Error{"ItemLedger: invalid entry range", 1};
            const auto* op = v.Find("op");
            if (!op || (op->AsString() != "grant" && op->AsString() != "consume" &&
                        op->AsString() != "give" && op->AsString() != "recv" && op->AsString() != "gold"))
                return Error{"ItemLedger: unknown operation", 1};
            if (const auto* p = v.Find("seq"))  e.seq       = static_cast<uint64_t>(p->AsInt());
            if (const auto* p = v.Find("char")) e.character = static_cast<CharacterId>(p->AsInt());
            if (const auto* p = v.Find("op"))   e.op        = OpFromName(p->AsString());
            if (const auto* p = v.Find("item")) e.itemId    = static_cast<uint32_t>(p->AsInt());
            if (const auto* p = v.Find("di"))   e.itemDelta = static_cast<int32_t>(p->AsInt());
            if (const auto* p = v.Find("dg"))   e.goldDelta = p->AsInt();
            if (const auto* p = v.Find("txn"))  e.txnId     = static_cast<uint64_t>(p->AsInt());
            if (const auto* p = v.Find("why"))  e.reason    = std::string(p->AsString());
            if (!candidate.m_entries.empty() && candidate.m_entries.back().seq >= e.seq)
                return Error{"ItemLedger: unordered sequence", 1};
            if ((e.itemId == 0 && e.itemDelta != 0) ||
                (e.op == LedgerOp::Grant && (e.itemDelta <= 0 || e.goldDelta != 0 || e.txnId != 0)) ||
                (e.op == LedgerOp::Consume && (e.itemDelta >= 0 || e.goldDelta != 0 || e.txnId != 0)) ||
                (e.op == LedgerOp::GoldOnly && (e.itemId != 0 || e.itemDelta != 0 || e.goldDelta == 0 || e.txnId != 0)))
                return Error{"ItemLedger: operation/delta mismatch", 1};
            candidate.m_entries.push_back(std::move(e));
        }
    }
    if (const auto* p = root.Find("nextSeq")) candidate.m_nextSeq = static_cast<uint64_t>(p->AsInt());
    if (const auto* p = root.Find("nextTxn")) candidate.m_nextTxn = static_cast<uint64_t>(p->AsInt());
    if (!detail::IntegerInRange(root, "nextSeq", 1, INT64_MAX) ||
        !detail::IntegerInRange(root, "nextTxn", 1, INT64_MAX))
        return Error{"ItemLedger: invalid counters", 1};
    for (size_t i = 0; i < candidate.m_entries.size(); ++i) {
        const auto& e = candidate.m_entries[i];
        if (e.seq >= candidate.m_nextSeq || e.txnId >= candidate.m_nextTxn)
            return Error{"ItemLedger: reused sequence/transaction", 1};
        if (e.op == LedgerOp::TradeGive) {
            if (++i == candidate.m_entries.size()) return Error{"ItemLedger: incomplete transfer", 1};
            const auto& peer = candidate.m_entries[i];
            if (e.txnId == 0 || peer.op != LedgerOp::TradeRecv || peer.txnId != e.txnId ||
                peer.character == e.character || peer.itemId != e.itemId ||
                peer.itemDelta != -e.itemDelta || peer.goldDelta != -e.goldDelta ||
                e.itemDelta > 0 || e.goldDelta > 0 || (e.itemDelta == 0 && e.goldDelta == 0) ||
                peer.seq >= candidate.m_nextSeq)
                return Error{"ItemLedger: asymmetric transfer", 1};
        } else if (e.op == LedgerOp::TradeRecv) return Error{"ItemLedger: orphan transfer", 1};
    }
    if (!candidate.VerifyIntegrity()) return Error{"ItemLedger: invalid running balance", 1};
    *this = std::move(candidate);
    return {};
}

} // namespace mye::persist
