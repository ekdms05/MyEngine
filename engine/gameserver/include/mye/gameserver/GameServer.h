// mye/gameserver/GameServer.h — 게임플레이↔영속 통합 세션 관리 (docs/mmorpg 통합, M8·M10)
//
// 서버측 오케스트레이션: 인증된 계정이 접속하면 캐릭터를 런타임 세션으로 로드하고
// (CharacterRecord → gameplay Stats/Progression/Inventory), 게임플레이 조작을 처리하며,
// 퇴장 시 세션 상태를 CharacterRecord 로 저장한다. 아이템/골드 변화는 ItemLedger 를 함께
// 갱신해 스냅샷↔원장 정합(dupe 방지)을 유지한다.
//
// 전송(net) 비의존 — sessionId 로 다룬다. NetServer 어댑터가 netId↔sessionId 를 잇는다(후속).
#pragma once

#include "mye/persist/PersistenceService.h"
#include "mye/gameplay/Stats.h"
#include "mye/gameplay/Progression.h"
#include "mye/gameplay/Inventory.h"
#include "mye/gameplay/Item.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::gameserver {

using SessionId = uint32_t;

// 접속 플레이어의 런타임 상태(권위). 캐릭터 1명에 대응.
struct PlayerSession {
    SessionId              sessionId = 0;
    persist::AccountId     accountId = 0;
    persist::CharacterId   characterId = 0;
    gameplay::Stats        stats;
    gameplay::Progression  prog;
    gameplay::Inventory    inv;
    float                  x = 0.0f, y = 0.0f;
    std::string            sceneId;
};

class GameServer {
public:
    explicit GameServer(persist::PersistenceService& persist) : m_persist(persist) {}

    // 캐릭터를 런타임 세션으로 로드. charId=0 이면 계정의 첫 캐릭터. 캐릭터 없으면 실패.
    Expected<SessionId, Error> Join(persist::AccountId account, persist::CharacterId charId = 0);

    // 세션 상태를 CharacterRecord 로 저장(pos/레벨/xp/스탯/인벤/골드) 후 세션 제거.
    Expected<void, Error> Leave(SessionId id);

    PlayerSession*       Get(SessionId id);
    const PlayerSession* Get(SessionId id) const;
    SessionId            FindByCharacter(persist::CharacterId charId) const;   // 0=없음
    size_t               SessionCount() const { return m_sessions.size(); }

    // ---- 게임플레이 조작(원장 일관) ----
    // 아이템 지급: 인벤에 넣고(용량/스택 규칙) 실제 들어간 수만큼 원장 Grant. 반환=실제 지급 수.
    int32_t GrantItem(SessionId id, gameplay::ItemId itemId, int32_t count, const gameplay::ItemCatalog& cat);
    // 아이템 소모: 인벤에서 빼고 실제 제거 수만큼 원장 Consume. 반환=실제 제거 수.
    int32_t ConsumeItem(SessionId id, gameplay::ItemId itemId, int32_t count);
    // 골드 증감(원장 반영). 음수 잔고면 실패(변화 없음).
    bool    AddGold(SessionId id, int64_t delta);

    // 서버권위 이동(net tick 에서 호출) — 델타 누적.
    void ApplyMove(SessionId id, float dx, float dy);

    // 경험치 획득 → 레벨업 + 스탯 파생 재계산.
    void GainXp(SessionId id, int64_t amount);

private:
    PlayerSession*       Find(SessionId id);
    const PlayerSession* Find(SessionId id) const;
    void LoadRecordInto(const persist::CharacterRecord& rec, PlayerSession& s);
    void WriteSessionInto(const PlayerSession& s, persist::CharacterRecord& rec);

    persist::PersistenceService& m_persist;
    std::vector<PlayerSession>   m_sessions;
    SessionId                    m_nextSession = 1;
};

} // namespace mye::gameserver
