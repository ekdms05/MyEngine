// mye/runtime/SaveSystem.h — 세이브/로드(슬롯·버전 헤더·ISaveParticipant) (docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). 파일 = 헤더{매직·버전·타임스탬프·플레이시간·(썸네일 후속)} + 페이로드
//   (refl/ser JsonArchive 로 섹션별 기록). "게임이 무엇을 저장할지는 ISaveParticipant 등록으로
//   결정하고 엔진은 배관만 제공"(docs/06). 쓰기는 user:// temp → rename 원자적 교체.
//   버전 마이그레이션은 ISaveMigration 체인(구버전 세이브 단계적 승격).
//
// settings 슬롯(kSettingsSlot): 볼륨·키바인딩·언어. Config(User 스코프)·InputBindings 와
//   중복되지 않게 — settings 는 참여자(AudioSettingsParticipant 등)가 Config 를 읽어 기록/복원한다.
//
// 확장 포인트(docs/06 확장 §6): ISaveParticipant 는 Lua 에서도 등록 가능(테이블 save/load) —
//   05 바인딩이 LuaSaveParticipant 어댑터로 감싼다(그 어댑터는 05/구현 에이전트 몫).
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::ser  { class IArchive; }
namespace mye::asset { class VirtualFileSystem; }

namespace mye::runtime {

// 세이브 파일 헤더(빠른 목록 조회용 — 페이로드 없이 헤더만 읽기).
struct SaveHeader {
    uint32_t    magic   = 0x4D594553u;  // 'MYES'
    uint32_t    version = 1;            // 세이브 포맷 버전(마이그레이션 트리거)
    int64_t     timestampUnix = 0;      // 저장 시각(UTC epoch 초)
    double      playTimeSec = 0.0;      // 누적 플레이 시간
    std::string title;                  // 표시용(맵 이름·챕터 등, 게임이 채움)
    std::string thumbnailVpath;         // 썸네일 PNG vpath(후속 — 빈값 허용)
};

// 슬롯 목록 항목(enumerate — 헤더만).
struct SlotInfo {
    SlotId     slot;
    bool       exists = false;
    SaveHeader header;
};

// ---------------------------------------------------------------------------
// ISaveParticipant — 게임/Lua 가 등록하는 저장 참여자(docs/06 §7).
//   sectionId 로 페이로드 내 자기 섹션을 소유. Save/Load 는 refl/ser IArchive 로 왕복.
//   엔진은 스키마를 모른다 — 참여자가 자기 데이터를 직렬화한다.
// ---------------------------------------------------------------------------
class ISaveParticipant {
public:
    virtual ~ISaveParticipant() = default;

    // 페이로드 내 섹션 키(유일). 예: "player", "inventory", "quests", "settings".
    virtual std::string_view SectionId() const = 0;

    // 대칭 직렬화: ar.IsReading() 으로 방향 분기. 쓰기=내보내기, 읽기=채우기.
    //   섹션이 없는(구세이브) 읽기 상황은 SaveSystem 이 건너뛴다(참여자는 미호출).
    virtual void Serialize(ser::IArchive& ar) = 0;
};

// ---------------------------------------------------------------------------
// ISaveMigration — 세이브 포맷 버전 마이그레이션(구버전 → 현재). 체인 등록.
//   FromVersion 데이터를 다음 버전 레이아웃으로 끌어올린다. 페이로드 JSON 트리 레벨에서 조작.
// ---------------------------------------------------------------------------
class ISaveMigration {
public:
    virtual ~ISaveMigration() = default;
    virtual uint32_t FromVersion() const = 0;   // 이 마이그레이션이 처리하는 시작 버전
    // payloadJson: 세이브 페이로드 JSON 문자열(in/out). 성공 시 다음 버전 스키마로 변형.
    virtual Expected<void, Error> Migrate(std::string& payloadJson) = 0;
};

// ---------------------------------------------------------------------------
// SaveSystem — 슬롯 관리·원자적 쓰기·참여자 수집·마이그레이션.
//   EngineContext 서비스. Lua Save.* 바인딩(05)의 백엔드.
// ---------------------------------------------------------------------------
class SaveSystem {
public:
    MYE_SERVICE(mye::runtime::SaveSystem);

    // 현재 코드가 쓰는 세이브 포맷 버전. 마이그레이션 체인의 종점.
    static constexpr uint32_t kCurrentVersion = 1;

    SaveSystem();
    ~SaveSystem();
    SaveSystem(const SaveSystem&) = delete;
    SaveSystem& operator=(const SaveSystem&) = delete;

    // vfs 로 user:// 읽기/쓰기(비소유, 수명 > SaveSystem). saveDir: user:// 하위 디렉터리("saves").
    void Initialize(asset::VirtualFileSystem* vfs, std::string saveDir = "saves");
    void Shutdown();

    // user:// 의 OS 루트 디렉터리 주입(원자적 쓰기·삭제 대상). VFS 는 읽기 전용이라 쓰기 경로는
    //   OS 파일로 직접 수행한다 — 이 루트/saveDir 아래에 슬롯 파일을 쓴다. 미설정 시 기본 = cwd/user.
    //   부트스트랩(RuntimeModule)이 user:// 마운트의 OS 경로와 동일하게 맞춰 주입한다(읽기·쓰기 일치).
    void SetUserRootDir(std::string osDir);

    // ---- 참여자·마이그레이션 등록 ----
    void RegisterParticipant(ISaveParticipant* participant);   // 비소유
    void UnregisterParticipant(ISaveParticipant* participant);
    void RegisterMigration(std::unique_ptr<ISaveMigration> migration);

    // ---- 슬롯 I/O ----
    // 모든 참여자를 순회해 페이로드를 만들고 헤더와 함께 슬롯에 원자적 기록(temp→rename).
    Expected<void, Error> WriteSlot(SlotId slot, const SaveHeader& header);
    // 슬롯을 읽어 버전 마이그레이션 적용 후 참여자에게 Load 디스패치.
    Expected<void, Error> ReadSlot(SlotId slot);

    // 헤더만 읽기(목록 UI — 빠른 조회). 존재하지 않으면 exists=false.
    Expected<SlotInfo, Error>   ReadHeader(SlotId slot) const;
    std::vector<SlotInfo>       EnumerateSlots(int32_t maxGameSlots = 16) const;

    bool Exists(SlotId slot) const;
    Expected<void, Error> DeleteSlot(SlotId slot);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
