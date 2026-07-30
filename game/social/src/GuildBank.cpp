// mye/social/GuildBank.cpp — 길드 은행 구현 (GuildBank.h 참조)
#include "mye/social/GuildBank.h"

namespace mye::social {

bool GuildBank::DepositGold(GuildId g, uint64_t charId, int64_t amount) {
    if (amount <= 0) return false;
    return static_cast<bool>(m_ledger.Transfer(charId, BankAccount(g), 0, 0, amount, "guild_deposit_gold"));
}

bool GuildBank::DepositItem(GuildId g, uint64_t charId, uint32_t itemId, int32_t count) {
    if (itemId == 0 || count <= 0) return false;
    return static_cast<bool>(m_ledger.Transfer(charId, BankAccount(g), itemId, count, 0, "guild_deposit_item"));
}

bool GuildBank::WithdrawGold(GuildId g, uint64_t charId, GuildRank rank, int64_t amount) {
    if (amount <= 0 || rank < m_minWithdraw) return false;
    return static_cast<bool>(m_ledger.Transfer(BankAccount(g), charId, 0, 0, amount, "guild_withdraw_gold"));
}

bool GuildBank::WithdrawItem(GuildId g, uint64_t charId, GuildRank rank, uint32_t itemId, int32_t count) {
    if (itemId == 0 || count <= 0 || rank < m_minWithdraw) return false;
    return static_cast<bool>(m_ledger.Transfer(BankAccount(g), charId, itemId, count, 0, "guild_withdraw_item"));
}

} // namespace mye::social
