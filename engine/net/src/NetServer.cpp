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
                // 안티치트: 이동 입력은 단위벡터 성분(±1) 범위. 초과는 조작 → 위반 누적 후 클램프.
                if (mx < -1.001f || mx > 1.001f || my < -1.001f || my > 1.001f) {
                    ++c->violations;
                }
                c->inX = mx < -1.0f ? -1.0f : (mx > 1.0f ? 1.0f : mx);
                c->inY = my < -1.0f ? -1.0f : (my > 1.0f ? 1.0f : my);
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
        // 좌표 sanity: 월드 경계 밖으로는 못 나감(서버권위 클램프).
        if (c.x < m_minX) c.x = m_minX; else if (c.x > m_maxX) c.x = m_maxX;
        if (c.y < m_minY) c.y = m_minY; else if (c.y > m_maxY) c.y = m_maxY;
    }
    // 위반 누적이 임계 도달한 클라 자동 킥(뒤에서 앞으로 안전 제거).
    if (m_maxViolations > 0) {
        for (size_t i = m_clients.size(); i-- > 0;) {
            if (m_clients[i].violations >= m_maxViolations) {
                MYE_LOG_WARN("Net", "client {} 안티치트 킥(위반 {})", m_clients[i].id, m_clients[i].violations);
                KickIndex(i);
            }
        }
    }
    ++m_tick;
}

void NetServer::KickIndex(size_t i) {
    if (i >= m_clients.size()) return;
    BitWriter w;
    WriteHeader(w, MsgType::Disconnect);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_clients[i].ep, bytes.data(), bytes.size());
    m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(i));
    ++m_kicked;
}

uint32_t NetServer::ViolationsOf(uint32_t netId) const {
    for (const Client& c : m_clients)
        if (c.id == netId) return c.violations;
    return 0;
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

void NetServer::SetEntity(uint32_t netId, float x, float y) {
    for (Client& c : m_clients)
        if (c.id == netId) { c.x = x; c.y = y; return; }
}

uint64_t NetServer::AccountOf(uint32_t netId) const {
    for (const Client& c : m_clients)
        if (c.id == netId) return c.accountId;
    return 0;
}

std::vector<uint32_t> NetServer::ClientIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(m_clients.size());
    for (const Client& c : m_clients) ids.push_back(c.id);
    return ids;
}

} // namespace mye::net
