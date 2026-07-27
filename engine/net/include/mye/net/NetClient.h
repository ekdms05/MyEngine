// mye/net/NetClient.h — 최소 클라이언트(UDP) (docs/mmorpg/02, M9)
//
// 서버에 접속(Connect→Accept로 clientId 수신)하고, 입력을 보내며, 스냅샷을 받아 원격 엔티티
// 상태를 보관한다. (클라 예측/보간은 후속 — 여기선 권위 스냅샷 수신·적용까지.)
#pragma once

#include "mye/net/UdpSocket.h"
#include "mye/net/Protocol.h"

#include <cstdint>
#include <vector>

namespace mye::net {

class NetClient {
public:
    bool Open(uint16_t port = 0) { return m_sock.Open(port); }
    void Close() { m_sock.Close(); }

    void Connect(const Endpoint& server);
    void SendInput(uint32_t seq, float moveX, float moveY);
    void Disconnect();

    void Receive();   // 소켓 드레인 + Accept/Snapshot 처리

    uint32_t Id() const { return m_id; }
    bool Connected() const { return m_connected; }
    uint32_t LastTick() const { return m_tick; }
    bool GetEntity(uint32_t netId, float& x, float& y) const;
    size_t EntityCount() const { return m_snapshot.size(); }

private:
    UdpSocket               m_sock;
    Endpoint                m_server{};
    uint32_t                m_id = 0;
    bool                    m_connected = false;
    std::vector<EntitySnap> m_snapshot;
    uint32_t                m_tick = 0;
};

} // namespace mye::net
