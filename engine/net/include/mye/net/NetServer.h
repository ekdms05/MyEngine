// mye/net/NetServer.h — 최소 권위 서버(UDP) (docs/mmorpg/02, M9)
//
// 클라 접속을 수락하고 클라별 엔티티를 서버권위로 시뮬(입력 적용)한 뒤, 매 tick 스냅샷을 전원에게
// 브로드캐스트한다. 이동은 서버가 계산 → 클라는 결과만 본다(스피드핵 방지의 기본). 순수 UDP 로직.
#pragma once

#include "mye/net/UdpSocket.h"

#include <cstdint>
#include <vector>

namespace mye::net {

class NetServer {
public:
    bool Start(uint16_t port);
    void Stop() { m_sock.Close(); m_clients.clear(); }
    uint16_t Port() const { return m_sock.LocalPort(); }
    bool IsRunning() const { return m_sock.IsOpen(); }

    void Receive();          // 소켓 드레인 + Connect/Input/Disconnect 처리
    void Tick(float dt);     // 서버 시뮬: 클라 입력을 각 엔티티에 적용
    void Broadcast();        // 스냅샷을 전 클라에 송신

    // ---- 조회(테스트/디버그) ----
    size_t ClientCount() const { return m_clients.size(); }
    uint32_t CurrentTick() const { return m_tick; }
    bool GetEntity(uint32_t netId, float& x, float& y) const;

    void SetMoveSpeed(float s) { m_speed = s; }

private:
    struct Client {
        Endpoint ep;
        uint32_t id = 0;
        float    x = 0.0f, y = 0.0f;   // 서버권위 위치
        float    inX = 0.0f, inY = 0.0f;  // 최근 입력
        uint32_t lastInputSeq = 0;     // 마지막 처리 입력(클라 재조정용)
    };
    Client* Find(const Endpoint& ep);

    UdpSocket           m_sock;
    std::vector<Client> m_clients;
    uint32_t            m_nextId = 1;
    uint32_t            m_tick = 0;
    float               m_speed = 6.0f;
};

} // namespace mye::net
