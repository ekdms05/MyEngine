// mye/persist/PersistenceService.h — 영속 파사드(계정·캐릭터·원장 묶음) (docs/mmorpg/03, M10)
//
// 서버가 하나의 세이브 디렉터리로 계정·캐릭터·거래원장을 함께 로드/저장하도록 묶는 파사드.
// 서버 부팅 시 LoadAll, 종료·주기 저장 시 SaveAll. 캐릭터 스냅샷(인벤토리/골드)이 원장 잔고와
// 일치하는지 대조(Reconcile)해 dupe/유실을 감사한다 — MMO 경제 무결성의 서버측 관문.
#pragma once

#include "mye/persist/AccountStore.h"
#include "mye/persist/CharacterStore.h"
#include "mye/persist/ItemLedger.h"

#include <string>
#include <string_view>

namespace mye::persist {

// 캐릭터 스냅샷 ↔ 원장 잔고 대조 결과.
struct ReconcileReport {
    bool     matches = true;   // 모든 아이템·골드가 원장과 일치?
    int64_t  goldSnapshot = 0;
    int64_t  goldLedger = 0;
    uint32_t firstMismatchItem = 0;   // 불일치 첫 아이템(0=골드 또는 없음)
    int64_t  snapshotCount = 0;       // 해당 아이템 스냅샷 수량
    int64_t  ledgerCount = 0;         // 해당 아이템 원장 수량
};

class PersistenceService {
public:
    // 디렉터리에서 accounts.json / characters.json / ledger.json 로드.
    //   파일이 없으면 빈 상태로 시작(첫 부팅 허용). 손상 파싱만 오류.
    Expected<void, Error> LoadAll(std::string_view dir);

    // 세 파일을 디렉터리에 원자적으로 저장(디렉터리 없으면 생성).
    Expected<void, Error> SaveAll(std::string_view dir) const;

    // 저장 전 현재 디스크 상태를 백업으로 회전한 뒤 저장(롤백/PIT 복구용).
    //   백업은 <dir>/backups/backup_NNNN/ 에 보관, 최근 maxBackups 개만 유지(오래된 것 삭제).
    Expected<void, Error> SaveAllWithBackup(std::string_view dir, int maxBackups = 5) const;

    // 사용 가능한 백업 인덱스 목록(오름차순).
    std::vector<int> ListBackups(std::string_view dir) const;

    // 지정 백업을 디렉터리로 복원하고 메모리에 다시 로드(롤백).
    Expected<void, Error> RestoreFromBackup(std::string_view dir, int backupIndex);

    AccountStore&        Accounts()       { return m_accounts; }
    const AccountStore&  Accounts() const  { return m_accounts; }
    CharacterStore&      Characters()      { return m_characters; }
    const CharacterStore& Characters() const { return m_characters; }
    ItemLedger&          Ledger()         { return m_ledger; }
    const ItemLedger&    Ledger() const    { return m_ledger; }

    // 캐릭터 스냅샷(CharacterRecord.items/gold)이 원장 잔고와 일치하는지 대조.
    //   불일치는 dupe·유실·버그 신호 → 서버가 롤백/차단 결정에 사용. 캐릭터 없으면 matches=false.
    ReconcileReport Reconcile(CharacterId charId) const;

private:
    AccountStore   m_accounts;
    CharacterStore m_characters;
    ItemLedger     m_ledger;
};

} // namespace mye::persist
