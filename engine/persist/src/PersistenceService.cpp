// mye/persist/PersistenceService.cpp — 영속 파사드 구현 (PersistenceService.h 참조)
#include "mye/persist/PersistenceService.h"

#include <filesystem>
#include <system_error>

namespace mye::persist {

namespace {
std::string Join(std::string_view dir, const char* file) {
    std::filesystem::path p(dir);
    p /= file;
    return p.string();
}
} // namespace

Expected<void, Error> PersistenceService::LoadAll(std::string_view dir) {
    namespace fs = std::filesystem;
    const std::string accPath = Join(dir, "accounts.json");
    const std::string chrPath = Join(dir, "characters.json");
    const std::string ledPath = Join(dir, "ledger.json");

    // 파일이 없으면 빈 상태 유지(첫 부팅). 존재하는데 파싱 실패면 오류 전파.
    if (fs::exists(accPath)) { auto r = m_accounts.LoadFromFile(accPath);   if (!r) return r.GetError(); }
    if (fs::exists(chrPath)) { auto r = m_characters.LoadFromFile(chrPath); if (!r) return r.GetError(); }
    if (fs::exists(ledPath)) { auto r = m_ledger.LoadFromFile(ledPath);     if (!r) return r.GetError(); }
    return {};
}

Expected<void, Error> PersistenceService::SaveAll(std::string_view dir) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return Error{"SaveAll: 디렉터리 생성 실패 '" + std::string(dir) + "'", 1};

    if (auto r = m_accounts.SaveToFile(Join(dir, "accounts.json"));   !r) return r.GetError();
    if (auto r = m_characters.SaveToFile(Join(dir, "characters.json")); !r) return r.GetError();
    if (auto r = m_ledger.SaveToFile(Join(dir, "ledger.json"));       !r) return r.GetError();
    return {};
}

ReconcileReport PersistenceService::Reconcile(CharacterId charId) const {
    ReconcileReport rep;
    const CharacterRecord* c = m_characters.Get(charId);
    if (!c) { rep.matches = false; return rep; }

    // 골드 대조.
    rep.goldSnapshot = c->gold;
    rep.goldLedger = m_ledger.GoldBalance(charId);
    if (rep.goldSnapshot != rep.goldLedger) {
        rep.matches = false;
        return rep;   // 골드 불일치 우선 보고(firstMismatchItem=0)
    }

    // 아이템 대조: 스냅샷 각 스택 수량 == 원장 잔고.
    for (const ItemStackRecord& s : c->items) {
        const int64_t led = m_ledger.ItemBalance(charId, s.itemId);
        if (led != s.count) {
            rep.matches = false;
            rep.firstMismatchItem = s.itemId;
            rep.snapshotCount = s.count;
            rep.ledgerCount = led;
            return rep;
        }
    }
    return rep;
}

} // namespace mye::persist
