// mye/persist/CharacterStore.h — 캐릭터 영속(계정 소유·월드 상태) (docs/mmorpg/03, M10)
//
// 캐릭터는 계정(AccountId)에 속한다. 캐릭터 선택 화면(계정→캐릭터 목록)과
// 재접속 시 월드 상태 복원(마지막 씬·좌표·레벨·경험치·스탯·인벤토리)의 근거.
//
// [레이어링] 이 계층은 게임플레이 타입(Stats/Inventory/Progression)에 의존하지 않고
//   순수 POD 레코드만 다룬다 — 상위(runtime/server)가 ECS 컴포넌트 ↔ 레코드로 매핑한다.
//   덕분에 영속 계층은 게임플레이 없이 독립 테스트되고, 콘텐츠 규칙 변경에 안 흔들린다.
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::persist {

using AccountId   = uint64_t;   // AccountStore.h 와 동일 개념
using CharacterId = uint64_t;

// 인벤토리 한 칸(스택)의 영속 표현. 게임플레이 ItemStack 의 미러.
struct ItemStackRecord {
    uint32_t itemId = 0;
    int32_t  count  = 0;
};

// 캐릭터 한 명의 영속 상태(월드 세이브의 단위).
struct CharacterRecord {
    CharacterId id        = 0;
    AccountId   accountId = 0;   // 소유 계정
    std::string name;            // 전역 고유(캐릭터명 중복 금지)

    // 월드 위치(재접속 복원).
    std::string sceneId;         // 마지막 위치 씬/맵 식별자
    float       posX = 0.0f;
    float       posY = 0.0f;

    // 성장.
    int32_t     level = 1;
    int64_t     xp    = 0;

    // 1차 스탯(BaseAttributes 미러).
    int32_t     strength  = 10;
    int32_t     agility   = 10;
    int32_t     intellect = 10;
    int32_t     vitality  = 10;

    // 현재 자원.
    int32_t     hp = 0;
    int32_t     mp = 0;

    // 인벤토리.
    int64_t                      gold = 0;
    std::vector<ItemStackRecord> items;
};

// 캐릭터 저장소. 계정↔캐릭터 인덱스 + 파일 영속.
class CharacterStore {
public:
    // 캐릭터 생성. 이름 전역 중복·빈 값·유효하지 않은 계정이면 실패.
    Expected<CharacterId, Error> Create(AccountId accountId, std::string_view name);

    // 조회.
    const CharacterRecord* Get(CharacterId id) const;
    CharacterRecord*       GetMutable(CharacterId id);
    const CharacterRecord* FindByName(std::string_view name) const;
    // 계정 소유 캐릭터 id 목록(캐릭터 선택 화면).
    std::vector<CharacterId> ListByAccount(AccountId accountId) const;

    // 월드 상태 저장(업서트). 존재하면 갱신, 없고 id!=0 이면 그대로 삽입.
    //   보통 Create 로 얻은 id 의 GetMutable 을 편집한 뒤 파일 세이브만 하면 되지만,
    //   외부(runtime)가 만든 레코드를 통째로 밀어넣을 때 사용.
    Expected<void, Error> Upsert(const CharacterRecord& rec);

    // 캐릭터 삭제(이름 인덱스도 해제).
    bool Delete(CharacterId id);

    size_t Count() const { return m_chars.size(); }

    // ---- 영속화 ----
    Expected<void, Error> SaveToFile(std::string_view path) const;
    Expected<void, Error> LoadFromFile(std::string_view path);

private:
    void RebuildIndex();

    std::vector<CharacterRecord>                   m_chars;
    std::unordered_map<std::string, CharacterId>   m_byName;   // name → id
    CharacterId m_nextId = 1;
};

} // namespace mye::persist
