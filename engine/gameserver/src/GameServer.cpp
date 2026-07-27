// mye/gameserver/GameServer.cpp — 게임플레이↔영속 통합 구현 (GameServer.h 참조)
#include "mye/gameserver/GameServer.h"

#include "mye/gameplay/StatSystem.h"   // ComputeDerived

namespace mye::gameserver {

void GameServer::LoadRecordInto(const persist::CharacterRecord& rec, PlayerSession& s) {
    s.accountId   = rec.accountId;
    s.characterId = rec.id;
    s.x = rec.posX; s.y = rec.posY;
    s.sceneId = rec.sceneId;

    s.prog.level = rec.level;
    s.prog.xp    = rec.xp;

    s.stats.base.level     = rec.level;
    s.stats.base.strength  = rec.strength;
    s.stats.base.agility   = rec.agility;
    s.stats.base.intellect = rec.intellect;
    s.stats.base.vitality  = rec.vitality;
    s.stats.hp = rec.hp;
    s.stats.mp = rec.mp;
    s.stats.dirty = true;
    // hp<=0 = 신규/부활 → 최대치로 채워 스폰.
    gameplay::ComputeDerived(s.stats, rec.hp <= 0);

    s.inv.gold = rec.gold;
    s.inv.slots.clear();
    for (const persist::ItemStackRecord& it : rec.items)
        s.inv.slots.push_back(gameplay::ItemStack{ it.itemId, it.count });
}

void GameServer::WriteSessionInto(const PlayerSession& s, persist::CharacterRecord& rec) {
    rec.posX = s.x; rec.posY = s.y;
    rec.sceneId = s.sceneId;
    rec.level = s.prog.level;
    rec.xp    = s.prog.xp;
    rec.strength  = s.stats.base.strength;
    rec.agility   = s.stats.base.agility;
    rec.intellect = s.stats.base.intellect;
    rec.vitality  = s.stats.base.vitality;
    rec.hp = s.stats.hp;
    rec.mp = s.stats.mp;
    rec.gold = s.inv.gold;
    rec.items.clear();
    for (const gameplay::ItemStack& st : s.inv.slots)
        if (!st.Empty()) rec.items.push_back(persist::ItemStackRecord{ st.itemId, st.count });
}

Expected<SessionId, Error> GameServer::Join(persist::AccountId account, persist::CharacterId charId) {
    persist::CharacterId cid = charId;
    if (cid == 0) {
        const std::vector<persist::CharacterId> list = m_persist.Characters().ListByAccount(account);
        if (list.empty()) return Error{"Join: 계정에 캐릭터 없음", 1};
        cid = list.front();
    }
    const persist::CharacterRecord* rec = m_persist.Characters().Get(cid);
    if (!rec) return Error{"Join: 캐릭터 없음", 2};
    if (rec->accountId != account) return Error{"Join: 캐릭터 소유 계정 불일치", 3};
    if (FindByCharacter(cid) != 0) return Error{"Join: 이미 접속 중인 캐릭터(단일 세션)", 4};

    PlayerSession s;
    s.sessionId = m_nextSession++;
    LoadRecordInto(*rec, s);
    m_sessions.push_back(std::move(s));
    return m_sessions.back().sessionId;
}

Expected<void, Error> GameServer::Leave(SessionId id) {
    PlayerSession* s = Find(id);
    if (!s) return Error{"Leave: 세션 없음", 1};
    if (persist::CharacterRecord* rec = m_persist.Characters().GetMutable(s->characterId))
        WriteSessionInto(*s, *rec);
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it)
        if (it->sessionId == id) { m_sessions.erase(it); break; }
    return {};
}

PlayerSession* GameServer::Find(SessionId id) {
    for (PlayerSession& s : m_sessions) if (s.sessionId == id) return &s;
    return nullptr;
}
const PlayerSession* GameServer::Find(SessionId id) const {
    for (const PlayerSession& s : m_sessions) if (s.sessionId == id) return &s;
    return nullptr;
}
PlayerSession*       GameServer::Get(SessionId id)       { return Find(id); }
const PlayerSession* GameServer::Get(SessionId id) const { return Find(id); }

SessionId GameServer::FindByCharacter(persist::CharacterId charId) const {
    for (const PlayerSession& s : m_sessions) if (s.characterId == charId) return s.sessionId;
    return 0;
}

int32_t GameServer::GrantItem(SessionId id, gameplay::ItemId itemId, int32_t count, const gameplay::ItemCatalog& cat) {
    PlayerSession* s = Find(id);
    if (!s || count <= 0) return 0;
    const int32_t leftover = gameplay::AddItem(s->inv, cat, itemId, count);
    const int32_t added = count - leftover;
    if (added > 0) (void)m_persist.Ledger().Grant(s->characterId, itemId, added, "grant");
    return added;
}

int32_t GameServer::ConsumeItem(SessionId id, gameplay::ItemId itemId, int32_t count) {
    PlayerSession* s = Find(id);
    if (!s || count <= 0) return 0;
    const int32_t removed = gameplay::RemoveItem(s->inv, itemId, count);
    if (removed > 0) (void)m_persist.Ledger().Consume(s->characterId, itemId, removed, "consume");
    return removed;
}

bool GameServer::AddGold(SessionId id, int64_t delta) {
    PlayerSession* s = Find(id);
    if (!s || delta == 0) return false;
    if (delta < 0 && s->inv.gold + delta < 0) return false;
    if (auto r = m_persist.Ledger().AdjustGold(s->characterId, delta, "gold"); !r) return false;
    s->inv.gold += delta;
    return true;
}

void GameServer::ApplyMove(SessionId id, float dx, float dy) {
    if (PlayerSession* s = Find(id)) { s->x += dx; s->y += dy; }
}

void GameServer::GainXp(SessionId id, int64_t amount) {
    PlayerSession* s = Find(id);
    if (!s || amount <= 0) return;
    gameplay::GainXp(s->prog, s->stats, amount);
    s->stats.base.level = s->prog.level;         // 레벨 미러
    gameplay::ComputeDerived(s->stats, false);   // 파생 재계산 + hp/mp 클램프
}

} // namespace mye::gameserver
