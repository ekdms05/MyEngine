// mye/social/GuildBank.h — 길드 은행(공유 금고)·직급 권한·dupe-safe (docs/mmorpg/03·04, M12 소셜)
//
// [게임 레이어] 길드 공유 금고. 예치/인출은 engine/persist ItemLedger 의 원자적 Transfer 를 경유해
// 총량 보존(복제 불가)한다. 길드 금고는 원장의 별도 계정(BankAccount) — 캐릭터 id 공간과 분리.
// 인출은 직급(Officer+) 권한 검사. GuildSystem.RankOf 로 직급을 받아 넘긴다.
#pragma once

#include "mye/social/GuildSystem.h"   // GuildId, GuildRank
#include "mye/persist/ItemLedger.h"

#include <cstdint>

namespace mye::social {

class GuildBank {
public:
    explicit GuildBank(persist::ItemLedger& ledger, GuildRank minWithdrawRank = GuildRank::Officer)
        : m_ledger(ledger), m_minWithdraw(minWithdrawRank) {}

    // 길드 금고의 원장 계정 id(최상위 비트 세팅 → 실제 캐릭터 id(작은 카운터)와 충돌 없음).
    static uint64_t BankAccount(GuildId g) { return (uint64_t{1} << 63) | g; }

    // ---- 예치(멤버 → 금고) : 원장 Transfer(char → bank). 잔고 부족 시 실패(원장 검증). ----
    bool DepositGold(GuildId g, uint64_t charId, int64_t amount);
    bool DepositItem(GuildId g, uint64_t charId, uint32_t itemId, int32_t count);

    // ---- 인출(금고 → 멤버) : 직급 권한(minWithdrawRank) 검사 후 Transfer(bank → char). ----
    bool WithdrawGold(GuildId g, uint64_t charId, GuildRank rank, int64_t amount);
    bool WithdrawItem(GuildId g, uint64_t charId, GuildRank rank, uint32_t itemId, int32_t count);

    // ---- 잔고 조회(원장 재구성) ----
    int64_t Gold(GuildId g) const { return m_ledger.GoldBalance(BankAccount(g)); }
    int64_t ItemBalance(GuildId g, uint32_t itemId) const { return m_ledger.ItemBalance(BankAccount(g), itemId); }

    void SetMinWithdrawRank(GuildRank r) { m_minWithdraw = r; }

private:
    persist::ItemLedger& m_ledger;   // 비소유
    GuildRank            m_minWithdraw;
};

} // namespace mye::social
