// mye/gameserver/NetGameServer.cpp — 넷↔게임 통합 어댑터 구현 (NetGameServer.h 참조)
#include "mye/gameserver/NetGameServer.h"

#include <algorithm>

namespace mye::gameserver {

NetGameServer::NetGameServer(persist::PersistenceService& persist)
    : m_persist(persist), m_game(persist) {
    // 인증기: 자격증명 → accountId(0=거부). 세션은 인증된 계정만 얻는다.
    m_net.SetAuthenticator([&persist](std::string_view u, std::string_view p) -> uint64_t {
        persist::LoginResult r = persist.Accounts().Login(u, p);
        return r.ok ? r.accountId : 0;
    });
}

bool NetGameServer::Start(uint16_t port) {
    m_net.SetMoveSpeed(m_speed);
    return m_net.Start(port);
}

SessionId NetGameServer::SessionOf(uint32_t netId) const {
    auto it = m_netToSession.find(netId);
    return it == m_netToSession.end() ? 0 : it->second;
}

void NetGameServer::Tick(float dt) {
    m_net.Receive();

    const std::vector<uint32_t> ids = m_net.ClientIds();

    // 신규 접속 → 계정의 캐릭터를 세션으로 로드.
    for (uint32_t netId : ids) {
        if (m_netToSession.count(netId)) continue;
        const uint64_t acc = m_net.AccountOf(netId);
        if (acc == 0) continue;   // 익명은 세션(캐릭터) 없음
        auto sid = m_game.Join(acc);
        if (sid) {
            m_netToSession[netId] = sid.Value();
            // 로드된 캐릭터 위치를 넷 권위 위치로 주입(재접속 복원).
            if (const PlayerSession* s = m_game.Get(sid.Value()))
                m_net.SetEntity(netId, s->x, s->y);
        }
    }

    // 퇴장(넷에서 사라진 매핑) → 세션 저장 후 제거.
    for (auto it = m_netToSession.begin(); it != m_netToSession.end();) {
        if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
            (void)m_game.Leave(it->second);
            it = m_netToSession.erase(it);
        } else {
            ++it;
        }
    }

    m_net.Tick(dt);

    // 권위 위치를 세션에 동기(영속 대비 — 다음 저장/퇴장 시 반영).
    for (const auto& [netId, sid] : m_netToSession) {
        float x = 0, y = 0;
        if (m_net.GetEntity(netId, x, y)) {
            if (PlayerSession* s = m_game.Get(sid)) { s->x = x; s->y = y; }
        }
    }

    m_net.Broadcast();
}

} // namespace mye::gameserver
