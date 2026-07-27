// mye/persist/ItemLedger.h — 아이템·골드 거래 원장(무결성·dupe 차단) (docs/mmorpg/03, M10)
//
// MMO 경제의 진실 원천은 "잔고 스냅샷"이 아니라 "거래 원장"이다. 모든 획득/소모/거래를
// 추가-전용(append-only) 항목으로 남기면 (1) 크래시 후에도 재구성 가능하고,
// (2) 2자 거래를 보존적(생성·소멸 없음)으로 강제해 아이템 복제(dupe)를 원천 차단하며,
// (3) 리플레이로 잔고를 재구성해 스냅샷과 대조·감사할 수 있다.
//
// 핵심 불변식:
//   - Transfer 는 원자적: 두 당사자의 대칭 항목(−/＋)이 항상 함께 기록되거나 전혀 안 됨.
//   - 어떤 캐릭터의 어떤 아이템/골드 running 잔고도 음수가 될 수 없음(underflow=dupe 벡터).
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::persist {

using CharacterId = uint64_t;

// 원장 항목의 의미(감사·필터용). 잔고 계산은 delta 로만 한다.
enum class LedgerOp : uint8_t {
    Grant,     // 월드→캐릭터 생성(루트·퀘스트 보상·GM 지급)
    Consume,   // 캐릭터→월드 소멸(소모품 사용·파괴)
    TradeGive, // 거래에서 내보냄(Transfer 의 −쪽)
    TradeRecv, // 거래로 받음(Transfer 의 ＋쪽)
    GoldOnly,  // 순수 골드 변동(상점·수수료)
};

struct LedgerEntry {
    uint64_t    seq       = 0;   // 전역 단조 증가(기록 순서)
    CharacterId character = 0;
    LedgerOp    op        = LedgerOp::Grant;
    uint32_t    itemId    = 0;   // 0 = 순수 골드 항목
    int32_t     itemDelta = 0;   // +획득 / −소모
    int64_t     goldDelta = 0;   // +획득 / −지불
    uint64_t    txnId     = 0;   // 두 당사자를 잇는 거래 상관 id(0=단독)
    std::string reason;          // 감사 로그용 사유
};

// 한 캐릭터의 재구성 잔고.
struct LedgerBalance {
    std::unordered_map<uint32_t, int64_t> items;   // itemId → 수량
    int64_t gold = 0;
};

class ItemLedger {
public:
    // 월드→캐릭터 아이템 생성. count>0 필요.
    Expected<uint64_t, Error> Grant(CharacterId who, uint32_t itemId, int32_t count, std::string_view reason = {});

    // 캐릭터→월드 아이템 소멸. 보유량 부족 시 실패(잔고 음수 방지).
    Expected<uint64_t, Error> Consume(CharacterId who, uint32_t itemId, int32_t count, std::string_view reason = {});

    // 순수 골드 변동. delta<0 이고 잔고 부족이면 실패.
    Expected<uint64_t, Error> AdjustGold(CharacterId who, int64_t delta, std::string_view reason = {});

    // dupe-safe 2자 거래: from → to 로 아이템 count 개 + 골드 gold 개를 원자적으로 이동.
    //   from 의 아이템/골드 잔고를 먼저 검증 → 대칭 항목 2건을 같은 txnId 로 함께 기록.
    //   생성·소멸이 없어 총량이 보존된다(복제 불가). 반환값 = txnId.
    Expected<uint64_t, Error> Transfer(CharacterId from, CharacterId to,
                                       uint32_t itemId, int32_t count, int64_t gold,
                                       std::string_view reason = {});

    // 리플레이로 캐릭터 잔고 재구성(0 이하 항목은 제외).
    LedgerBalance Balance(CharacterId who) const;
    int64_t       ItemBalance(CharacterId who, uint32_t itemId) const;
    int64_t       GoldBalance(CharacterId who) const;

    // 무결성 감사: 기록 순서대로 리플레이하며 어떤 캐릭터·아이템·골드도
    //   running 잔고가 음수가 되지 않는지 검증(dupe/underflow 탐지). 위반 seq 를 outSeq 로.
    bool VerifyIntegrity(uint64_t* outFirstBadSeq = nullptr) const;

    size_t Count() const { return m_entries.size(); }
    const std::vector<LedgerEntry>& Entries() const { return m_entries; }

    // ---- 영속화(추가-전용 로그) ----
    Expected<void, Error> SaveToFile(std::string_view path) const;
    Expected<void, Error> LoadFromFile(std::string_view path);

private:
    uint64_t Append(CharacterId who, LedgerOp op, uint32_t itemId, int32_t itemDelta,
                    int64_t goldDelta, uint64_t txnId, std::string_view reason);

    std::vector<LedgerEntry> m_entries;
    uint64_t m_nextSeq = 1;
    uint64_t m_nextTxn = 1;
};

} // namespace mye::persist
