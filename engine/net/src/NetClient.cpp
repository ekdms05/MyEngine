// mye/net/NetClient.cpp — 클라이언트 구현 (NetClient.h 참조)
#include "mye/net/NetClient.h"

namespace mye::net {

void NetClient::Connect(const Endpoint& server) {
    m_server = server;
    BitWriter w;
    WriteHeader(w, MsgType::Connect);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
}

void NetClient::SendInput(uint32_t seq, float moveX, float moveY) {
    BitWriter w;
    WriteInput(w, seq, moveX, moveY);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
}

void NetClient::Disconnect() {
    BitWriter w;
    WriteHeader(w, MsgType::Disconnect);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
    m_connected = false;
}

void NetClient::Receive() {
    uint8_t buf[1400];
    Endpoint from{};
    for (int guard = 0; guard < 1024; ++guard) {
        const int n = m_sock.RecvFrom(from, buf, sizeof(buf));
        if (n <= 0) break;

        BitReader r(buf, static_cast<size_t>(n));
        MsgType type;
        if (!ReadHeader(r, type)) continue;

        switch (type) {
        case MsgType::Accept:
            m_id = ReadAccept(r);
            m_connected = true;
            break;
        case MsgType::Snapshot: {
            uint32_t tick = 0;
            std::vector<EntitySnap> snap;
            if (ReadSnapshot(r, tick, snap)) { m_tick = tick; m_snapshot = std::move(snap); }
            break;
        }
        default: break;
        }
    }
}

bool NetClient::GetEntity(uint32_t netId, float& x, float& y) const {
    for (const EntitySnap& e : m_snapshot)
        if (e.netId == netId) { x = e.x; y = e.y; return true; }
    return false;
}

} // namespace mye::net
