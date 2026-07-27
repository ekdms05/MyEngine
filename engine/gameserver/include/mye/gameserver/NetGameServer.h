// mye/gameserver/NetGameServer.h — 넷↔게임플레이↔영속 통합 어댑터 (M9·M8·M10 통합)
//
// NetServer(전송·권위 이동)와 GameServer(캐릭터 세션·게임플레이·영속)를 잇는다.
// 매 틱: 소켓 수신 → 접속/퇴장 diff(계정 인증→캐릭터 세션 Join/Leave) → 권위 시뮬 →
// 세션 위치 동기 → 스냅샷 브로드캐스트. 인증된 계정만 세션을 얻는다(persist 계정 로그인).
#pragma once

#include "mye/gameserver/GameServer.h"
#include "mye/net/NetServer.h"

#include <cstdint>
#include <unordered_map>

namespace mye::gameserver {

class NetGameServer {
public:
    explicit NetGameServer(persist::PersistenceService& persist);

    bool     Start(uint16_t port);
    void     Stop() { m_net.Stop(); m_netToSession.clear(); }
    uint16_t Port() const { return m_net.Port(); }
    bool     IsRunning() const { return m_net.IsRunning(); }

    void SetMoveSpeed(float s) { m_speed = s; m_net.SetMoveSpeed(s); }

    // 한 서버 틱: 수신 → 세션 diff → 시뮬 → 위치 동기 → 브로드캐스트.
    void Tick(float dt);

    size_t     PlayerCount() const { return m_netToSession.size(); }
    SessionId  SessionOf(uint32_t netId) const;   // 0=없음
    GameServer& Game() { return m_game; }
    net::NetServer& Net() { return m_net; }

private:
    persist::PersistenceService&                  m_persist;
    net::NetServer                                m_net;
    GameServer                                    m_game;
    std::unordered_map<uint32_t, SessionId>       m_netToSession;   // netId → sessionId
    float                                         m_speed = 6.0f;
};

} // namespace mye::gameserver
