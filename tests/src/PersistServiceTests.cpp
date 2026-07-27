// PersistServiceTests.cpp — 영속 파사드: 디렉터리 로드/저장 + 원장 정합성 대조 (M10)
#include "TestFramework.h"

#include "mye/persist/PersistenceService.h"

#include <filesystem>
#include <string>

using namespace mye;
using namespace mye::persist;

namespace {
std::string MakeTempDir(const char* tag) {
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path() / (std::string("mye_persvc_") + tag)).string();
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}
} // namespace

MYE_TEST(ServiceLoadAllOnEmptyDirStartsFresh) {
    namespace fs = std::filesystem;
    const std::string dir = MakeTempDir("empty");
    PersistenceService svc;
    // 파일이 없어도 첫 부팅으로 허용(빈 상태).
    MYE_EXPECT(static_cast<bool>(svc.LoadAll(dir)));
    MYE_EXPECT(svc.Accounts().Count() == 0);
    MYE_EXPECT(svc.Characters().Count() == 0);
    MYE_EXPECT(svc.Ledger().Count() == 0);
    std::error_code ec; fs::remove_all(dir, ec);
}

MYE_TEST(ServiceSaveLoadRoundtrip) {
    namespace fs = std::filesystem;
    const std::string dir = MakeTempDir("roundtrip");

    AccountId acc = 0;
    CharacterId chr = 0;
    {
        PersistenceService svc;
        acc = svc.Accounts().Register("hero", "pw").Value();
        chr = svc.Characters().Create(acc, "Valkyr").Value();
        CharacterRecord* c = svc.Characters().GetMutable(chr);
        c->level = 15; c->gold = 300; c->items = {{100, 10}};
        (void)svc.Ledger().Grant(chr, 100, 10, "seed");
        (void)svc.Ledger().AdjustGold(chr, 300, "seed");
        MYE_EXPECT(static_cast<bool>(svc.SaveAll(dir)));   // 디렉터리 자동 생성
    }
    {
        PersistenceService svc;
        MYE_EXPECT(static_cast<bool>(svc.LoadAll(dir)));
        MYE_EXPECT(svc.Accounts().Count() == 1);
        MYE_EXPECT(svc.Accounts().FindByName("hero") != nullptr);
        const CharacterRecord* c = svc.Characters().Get(chr);
        MYE_EXPECT(c != nullptr && c->name == "Valkyr" && c->level == 15);
        MYE_EXPECT(svc.Ledger().ItemBalance(chr, 100) == 10);
        MYE_EXPECT(svc.Ledger().GoldBalance(chr) == 300);
        // 로그인이 로드 후에도 동작(계정 통합).
        MYE_EXPECT(svc.Accounts().Login("hero", "pw").ok);
    }
    std::error_code ec; fs::remove_all(dir, ec);
}

MYE_TEST(ServiceBackupRotationAndRestore) {
    namespace fs = std::filesystem;
    const std::string dir = MakeTempDir("backup");

    // v1 저장(백업 없음: 최초라 이전 상태 없음).
    AccountId acc = 0; CharacterId chr = 0;
    {
        PersistenceService svc;
        acc = svc.Accounts().Register("king", "pw").Value();
        chr = svc.Characters().Create(acc, "Arthur").Value();
        svc.Characters().GetMutable(chr)->gold = 100;
        MYE_EXPECT(static_cast<bool>(svc.SaveAllWithBackup(dir, 3)));
        MYE_EXPECT(svc.ListBackups(dir).empty());   // 최초 저장은 백업 안 함
    }
    // v2 저장(v1 을 backup_0001 로 회전).
    {
        PersistenceService svc;
        MYE_EXPECT(static_cast<bool>(svc.LoadAll(dir)));
        svc.Characters().GetMutable(chr)->gold = 200;
        MYE_EXPECT(static_cast<bool>(svc.SaveAllWithBackup(dir, 3)));
        const auto backups = svc.ListBackups(dir);
        MYE_EXPECT(backups.size() == 1 && backups[0] == 1);
    }
    // 현재는 v2(gold 200).
    {
        PersistenceService svc;
        MYE_EXPECT(static_cast<bool>(svc.LoadAll(dir)));
        MYE_EXPECT(svc.Characters().Get(chr)->gold == 200);
        // 롤백: backup_0001(v1, gold 100) 복원.
        MYE_EXPECT(static_cast<bool>(svc.RestoreFromBackup(dir, 1)));
        MYE_EXPECT(svc.Characters().Get(chr)->gold == 100);
        // 없는 백업 복원은 실패.
        MYE_EXPECT(!svc.RestoreFromBackup(dir, 999));
    }
    std::error_code ec; fs::remove_all(dir, ec);
}

MYE_TEST(ServiceBackupPrunesToMax) {
    namespace fs = std::filesystem;
    const std::string dir = MakeTempDir("backup_prune");

    PersistenceService svc;
    (void)svc.Accounts().Register("u", "p");
    // 최초 저장 + 이후 5회 저장 → 백업 5개 생성되지만 maxBackups=2 로 정리.
    MYE_EXPECT(static_cast<bool>(svc.SaveAllWithBackup(dir, 2)));
    for (int i = 0; i < 5; ++i)
        MYE_EXPECT(static_cast<bool>(svc.SaveAllWithBackup(dir, 2)));

    const auto backups = svc.ListBackups(dir);
    MYE_EXPECT(backups.size() == 2);          // 최근 2개만 유지
    MYE_EXPECT(backups[0] == 4 && backups[1] == 5);   // 오래된 1~3 삭제됨

    std::error_code ec; fs::remove_all(dir, ec);
}

MYE_TEST(ServiceReconcileDetectsMismatch) {
    PersistenceService svc;
    const AccountId acc = svc.Accounts().Register("gm", "pw").Value();
    const CharacterId chr = svc.Characters().Create(acc, "Auditor").Value();

    // 원장에 정상 거래를 반영하고 스냅샷도 동일하게 맞춤 → 일치.
    (void)svc.Ledger().Grant(chr, 5, 7, "loot");
    (void)svc.Ledger().AdjustGold(chr, 1000, "quest");
    CharacterRecord* c = svc.Characters().GetMutable(chr);
    c->items = {{5, 7}};
    c->gold = 1000;

    ReconcileReport ok = svc.Reconcile(chr);
    MYE_EXPECT(ok.matches);

    // 스냅샷을 조작(아이템 부풀리기=dupe 흉내) → 원장과 불일치 탐지.
    c->items = {{5, 99}};
    ReconcileReport bad = svc.Reconcile(chr);
    MYE_EXPECT(!bad.matches);
    MYE_EXPECT(bad.firstMismatchItem == 5);
    MYE_EXPECT(bad.snapshotCount == 99 && bad.ledgerCount == 7);

    // 골드 조작도 탐지.
    c->items = {{5, 7}};
    c->gold = 999999;
    ReconcileReport badGold = svc.Reconcile(chr);
    MYE_EXPECT(!badGold.matches);
    MYE_EXPECT(badGold.firstMismatchItem == 0);
    MYE_EXPECT(badGold.goldSnapshot == 999999 && badGold.goldLedger == 1000);

    // 존재하지 않는 캐릭터 → 불일치.
    MYE_EXPECT(!svc.Reconcile(9999).matches);
}
