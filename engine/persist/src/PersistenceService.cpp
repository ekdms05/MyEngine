// Account, character and ledger data share one atomic snapshot.
#include "mye/persist/PersistenceService.h"
#include "JsonFile.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <map>

namespace mye::persist {
namespace {
namespace fs = std::filesystem;
constexpr const char* kSnapshot = "state.json";
constexpr const char* kLegacyFiles[] = {"accounts.json", "characters.json", "ledger.json"};

fs::path BackupDir(std::string_view dir, int index) {
    return detail::Utf8Path(dir) / "backups" / std::format("backup_{:04d}", index);
}
} // namespace

Expected<void, Error> PersistenceService::LoadAll(std::string_view dir) {
    PersistenceService candidate;
    const fs::path root = detail::Utf8Path(dir);
    std::error_code ec;
    const bool snapshotExists = fs::exists(root / kSnapshot, ec);
    if (ec) return Error{"LoadAll: snapshot access failed", 1};
    if (snapshotExists) {
        auto parsed = detail::ReadJsonFile(root / kSnapshot);
        if (!parsed) return parsed.GetError();
        const auto& value = parsed.Value();
        const auto* accounts = value.Find("accounts");
        const auto* characters = value.Find("characters");
        const auto* ledger = value.Find("ledger");
        if (!detail::IntegerInRange(value, "version", 1, 1) || !accounts || !characters || !ledger)
            return Error{"LoadAll: invalid snapshot schema/version", 1};
        if (auto r = candidate.m_accounts.LoadJson(*accounts); !r) return r.GetError();
        if (auto r = candidate.m_characters.LoadJson(*characters); !r) return r.GetError();
        if (auto r = candidate.m_ledger.LoadJson(*ledger); !r) return r.GetError();
    } else {
        int present = 0;
        for (const char* file : kLegacyFiles) {
            if (fs::exists(root / file, ec)) ++present;
            if (ec) return Error{"LoadAll: legacy file access failed", 1};
        }
        if (present != 0 && present != 3)
            return Error{"LoadAll: incomplete legacy save; restore a complete backup", 1};
        if (present == 3) {
            if (auto r = candidate.m_accounts.LoadFromFile(detail::Utf8String((root / kLegacyFiles[0]))); !r) return r.GetError();
            if (auto r = candidate.m_characters.LoadFromFile(detail::Utf8String((root / kLegacyFiles[1]))); !r) return r.GetError();
            if (auto r = candidate.m_ledger.LoadFromFile(detail::Utf8String((root / kLegacyFiles[2]))); !r) return r.GetError();
        }
    }
    for (const auto& entry : candidate.m_ledger.Entries())
        if (!candidate.m_characters.Get(entry.character))
            return Error{"LoadAll: ledger references missing character", 1};
    *this = std::move(candidate);
    return {};
}

Expected<void, Error> PersistenceService::SaveAll(std::string_view dir) const {
    std::error_code ec;
    const fs::path root = detail::Utf8Path(dir);
    fs::create_directories(root, ec);
    if (ec) return Error{"SaveAll: directory creation failed '" + std::string(dir) + "'", 1};
    json::Value::Object snapshot;
    snapshot["version"] = json::Value(int64_t{1});
    snapshot["accounts"] = m_accounts.ToJson();
    snapshot["characters"] = m_characters.ToJson();
    snapshot["ledger"] = m_ledger.ToJson();
    return detail::WriteJsonFile(root / kSnapshot, json::Value(std::move(snapshot)));
}

std::vector<int> PersistenceService::ListBackups(std::string_view dir) const {
    std::vector<int> out;
    const fs::path root = detail::Utf8Path(dir) / "backups";
    std::error_code ec;
    fs::directory_iterator it(root, ec), end;
    while (!ec && it != end) {
        if (it->is_directory(ec)) {
            const std::string name = it->path().filename().string();
            if (name.starts_with("backup_")) {
                int index = 0;
                const auto tail = std::string_view(name).substr(7);
                const auto result = std::from_chars(tail.data(), tail.data() + tail.size(), index);
                if (result.ec == std::errc{} && result.ptr == tail.data() + tail.size() && index > 0)
                    out.push_back(index);
            }
        }
        it.increment(ec);
    }
    std::sort(out.begin(), out.end());
    return out;
}

Expected<void, Error> PersistenceService::SaveAllWithBackup(std::string_view dir, int maxBackups) const {
    std::error_code ec;
    const fs::path root = detail::Utf8Path(dir);
    bool hasPrior = fs::exists(root / kSnapshot, ec);
    if (ec) return Error{"SaveAllWithBackup: snapshot access failed", 1};
    if (!hasPrior) {
        for (const char* file : kLegacyFiles) {
            if (fs::exists(root / file, ec)) hasPrior = true;
            if (ec) return Error{"SaveAllWithBackup: legacy file access failed", 1};
        }
    }
    if (hasPrior) {
        PersistenceService prior;
        if (auto r = prior.LoadAll(dir); !r) return r.GetError();
        const auto existing = ListBackups(dir);
        if (!existing.empty() && existing.back() == INT_MAX)
            return Error{"SaveAllWithBackup: backup index exhausted", 1};
        const int next = existing.empty() ? 1 : existing.back() + 1;
        if (auto r = prior.SaveAll(detail::Utf8String(BackupDir(dir, next))); !r) return r.GetError();
    }
    if (auto r = SaveAll(dir); !r) return r.GetError();
    // Old backups are pruned only after the replacement has been published.
    const auto all = ListBackups(dir);
    if (maxBackups > 0 && all.size() > static_cast<size_t>(maxBackups)) {
        for (size_t i = 0; i < all.size() - static_cast<size_t>(maxBackups); ++i) {
            fs::remove_all(BackupDir(dir, all[i]), ec);
            if (ec) return Error{"SaveAllWithBackup: backup pruning failed", 1};
        }
    }
    return {};
}

Expected<void, Error> PersistenceService::RestoreFromBackup(std::string_view dir, int backupIndex) {
    if (backupIndex < 1) return Error{"RestoreFromBackup: invalid index", 1};
    const fs::path backup = BackupDir(dir, backupIndex);
    std::error_code ec;
    if (!fs::exists(backup, ec) || ec) return Error{"RestoreFromBackup: backup not found", 1};
    bool hasData = fs::exists(backup / kSnapshot, ec);
    if (ec) return Error{"RestoreFromBackup: snapshot access failed", 1};
    if (!hasData) {
        for (const char* file : kLegacyFiles) {
            if (fs::exists(backup / file, ec)) hasData = true;
            if (ec) return Error{"RestoreFromBackup: legacy file access failed", 1};
        }
    }
    if (!hasData) return Error{"RestoreFromBackup: empty backup", 1};
    PersistenceService candidate;
    if (auto r = candidate.LoadAll(detail::Utf8String(backup)); !r) return r.GetError();
    if (auto r = candidate.SaveAll(dir); !r) return r.GetError();
    *this = std::move(candidate);
    return {};
}

ReconcileReport PersistenceService::Reconcile(CharacterId charId) const {
    ReconcileReport rep;
    const CharacterRecord* c = m_characters.Get(charId);
    if (!c) { rep.matches = false; return rep; }
    const auto ledger = m_ledger.Balance(charId);
    rep.goldSnapshot = c->gold;
    rep.goldLedger = ledger.gold;
    if (rep.goldSnapshot != rep.goldLedger) { rep.matches = false; return rep; }

    // Combine stacks and include items present only in the ledger. Ordered keys
    // make the first discrepancy stable across runs; replay the ledger once.
    std::map<uint32_t, std::pair<int64_t, int64_t>> counts;
    for (const auto& stack : c->items) counts[stack.itemId].first += stack.count;
    for (const auto& [item, count] : ledger.items) counts[item].second = count;
    for (const auto& [item, count] : counts) {
        if (count.first == count.second) continue;
        rep.matches = false;
        rep.firstMismatchItem = item;
        rep.snapshotCount = count.first;
        rep.ledgerCount = count.second;
        break;
    }
    return rep;
}
} // namespace mye::persist
