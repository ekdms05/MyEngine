// mye/net/NetServer.cpp — 권위 서버 구현 (NetServer.h 참조)
#include "mye/net/NetServer.h"
#include "mye/net/Protocol.h"

#include "mye/core/Log.h"

namespace mye::net {

bool NetServer::Start(uint16_t port) {
    if (!m_sock.Open(port)) return false;
    MYE_LOG_INFO("Net", "server 시작 port={}", m_sock.LocalPort());
    return true;
}

NetServer::Client* NetServer::Find(const Endpoint& ep) {
    for (Client& c : m_clients) if (c.ep == ep) return &c;
    return nullptr;
}

void NetServer::Receive() {
    uint8_t buf[1400];
    Endpoint from{};
    for (int guard = 0; guard < 1024; ++guard) {
        const int n = m_sock.RecvFrom(from, buf, sizeof(buf));
        if (n <= 0) break;   // 데이터 없음/오류

        BitReader r(buf, static_cast<size_t>(n));
        MsgType type;
        if (!ReadHeader(r, type)) continue;

        switch (type) {
        case MsgType::Connect: {
            Client* c = Find(from);
            if (!c) {
                m_clients.push_back(Client{from, m_nextId++, 0.0f, 0.0f, 0.0f, 0.0f});
                c = &m_clients.back();
                MYE_LOG_INFO("Net", "client {} 접속 {}", c->id, from.ToString());
            }
            BitWriter w;
            WriteAccept(w, c->id);
            const auto& bytes = w.Finish();
            m_sock.SendTo(from, bytes.data(), bytes.size());
            break;
        }
        case MsgType::Input: {
            uint32_t seq = 0; float mx = 0, my = 0;
            ReadInput(r, seq, mx, my);
            if (Client* c = Find(from)) { c->inX = mx; c->inY = my; }
            break;
        }
        case MsgType::Disconnect: {
            for (size_t i = 0; i < m_clients.size(); ++i)
                if (m_clients[i].ep == from) { m_clients.erase(m_clients.begin() + i); break; }
            break;
        }
        default: break;
        }
    }
}

void NetServer::Tick(float dt) {
    for (Client& c : m_clients) {
        c.x += c.inX * m_speed * dt;
        c.y += c.inY * m_speed * dt;
    }
    ++m_tick;
}

void NetServer::Broadcast() {
    std::vector<EntitySnap> snap;
    snap.reserve(m_clients.size());
    for (const Client& c : m_clients) snap.push_back(EntitySnap{c.id, c.x, c.y});

    BitWriter w;
    WriteSnapshot(w, m_tick, snap);
    const auto& bytes = w.Finish();
    for (const Client& c : m_clients)
        m_sock.SendTo(c.ep, bytes.data(), bytes.size());
}

bool NetServer::GetEntity(uint32_t netId, float& x, float& y) const {
    for (const Client& c : m_clients)
        if (c.id == netId) { x = c.x; y = c.y; return true; }
    return false;
}

} // namespace mye::net
