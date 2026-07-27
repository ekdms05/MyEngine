// mye/persist/PersistenceService.cpp — 영속 파사드 구현 (PersistenceService.h 참조)
#include "mye/persist/PersistenceService.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <system_error>

namespace mye::persist {

namespace {
const char* kFiles[] = { "accounts.json", "characters.json", "ledger.json" };

std::string Join(std::string_view dir, const char* file) {
    std::filesystem::path p(dir);
    p /= file;
    return p.string();
}

std::filesystem::path BackupsRoot(std::string_view dir) {
    return std::filesystem::path(dir) / "backups";
}
std::filesystem::path BackupDir(std::string_view dir, int index) {
    return BackupsRoot(dir) / std::format("backup_{:04d}", index);
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

std::vector<int> PersistenceService::ListBackups(std::string_view dir) const {
    std::vector<int> out;
    namespace fs = std::filesystem;
    const fs::path root = BackupsRoot(dir);
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        if (name.rfind("backup_", 0) != 0) continue;
        int idx = 0;
        auto tail = name.substr(7);
        if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](char c){ return c >= '0' && c <= '9'; }))
            idx = std::atoi(tail.c_str());
        if (idx > 0) out.push_back(idx);
    }
    std::sort(out.begin(), out.end());
    return out;
}

Expected<void, Error> PersistenceService::SaveAllWithBackup(std::string_view dir, int maxBackups) const {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 현재 디스크 상태(이전 저장)가 있으면 백업으로 회전.
    bool hasPrior = false;
    for (const char* f : kFiles) if (fs::exists(Join(dir, f), ec)) { hasPrior = true; break; }
    if (hasPrior) {
        const std::vector<int> existing = ListBackups(dir);
        const int nextIdx = existing.empty() ? 1 : existing.back() + 1;
        const fs::path bdir = BackupDir(dir, nextIdx);
        fs::create_directories(bdir, ec);
        if (ec) return Error{"SaveAllWithBackup: 백업 디렉터리 생성 실패", 1};
        for (const char* f : kFiles) {
            const std::string src = Join(dir, f);
            if (fs::exists(src, ec))
                fs::copy_file(src, bdir / f, fs::copy_options::overwrite_existing, ec);
        }
        // 오래된 백업 정리(최근 maxBackups 개만 유지).
        if (maxBackups > 0) {
            std::vector<int> all = ListBackups(dir);
            if (static_cast<int>(all.size()) > maxBackups) {
                const size_t remove = all.size() - static_cast<size_t>(maxBackups);
                for (size_t i = 0; i < remove; ++i)
                    fs::remove_all(BackupDir(dir, all[i]), ec);
            }
        }
    }

    return SaveAll(dir);
}

Expected<void, Error> PersistenceService::RestoreFromBackup(std::string_view dir, int backupIndex) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path bdir = BackupDir(dir, backupIndex);
    if (!fs::exists(bdir, ec)) return Error{"RestoreFromBackup: 백업 없음 index=" + std::to_string(backupIndex), 1};

    for (const char* f : kFiles) {
        const fs::path src = bdir / f;
        if (fs::exists(src, ec)) {
            fs::copy_file(src, Join(dir, f), fs::copy_options::overwrite_existing, ec);
            if (ec) return Error{std::string("RestoreFromBackup: 복사 실패 ") + f, 2};
        }
    }
    return LoadAll(dir);
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
