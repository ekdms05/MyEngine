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
            std::string user, pass;
            if (!ReadConnect(r, user, pass)) break;   // 손상 패킷 방어

            Client* c = Find(from);
            if (!c) {
                // 인증기가 있으면 자격증명 검증 → 거부 시 admit 하지 않음.
                uint64_t accountId = 0;
                if (m_auth) {
                    accountId = m_auth(user, pass);
                    if (accountId == 0) {
                        ++m_rejected;
                        MYE_LOG_WARN("Net", "client 인증 거부 {} user='{}'", from.ToString(), user);
                        BitWriter dw;
                        WriteHeader(dw, MsgType::Disconnect);
                        const auto& db = dw.Finish();
                        m_sock.SendTo(from, db.data(), db.size());
                        break;
                    }
                }
                Client nc{};
                nc.ep = from;
                nc.id = m_nextId++;
                nc.accountId = accountId;
                m_clients.push_back(nc);
                c = &m_clients.back();
                MYE_LOG_INFO("Net", "client {} 접속 {} account={}", c->id, from.ToString(), accountId);
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
            if (Client* c = Find(from)) {
                c->inX = mx; c->inY = my;
                if (seq > c->lastInputSeq) c->lastInputSeq = seq;   // 재조정 기준
            }
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
    for (const Client& c : m_clients) snap.push_back(EntitySnap{c.id, c.x, c.y, c.lastInputSeq});

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

uint64_t NetServer::AccountOf(uint32_t netId) const {
    for (const Client& c : m_clients)
        if (c.id == netId) return c.accountId;
    return 0;
}

} // namespace mye::net
