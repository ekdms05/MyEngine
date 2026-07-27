// mye/net/NetClient.cpp — 클라이언트 구현 (NetClient.h 참조)
#include "mye/net/NetClient.h"

namespace mye::net {

void NetClient::Connect(const Endpoint& server, std::string_view username, std::string_view password) {
    m_server = server;
    BitWriter w;
    WriteConnect(w, username, password);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
}

namespace {
void ClampTo(float& x, float& y, float minX, float minY, float maxX, float maxY) {
    if (x < minX) x = minX; else if (x > maxX) x = maxX;
    if (y < minY) y = minY; else if (y > maxY) y = maxY;
}
}

void NetClient::SendInput(uint32_t seq, float moveX, float moveY, float dt) {
    BitWriter w;
    WriteInput(w, seq, moveX, moveY);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());

    // 클라 예측: 서버 응답을 기다리지 않고 로컬 위치를 즉시 이동(입력 지연 은폐).
    m_predX += moveX * m_speed * dt;
    m_predY += moveY * m_speed * dt;
    ClampTo(m_predX, m_predY, m_minX, m_minY, m_maxX, m_maxY);
    m_hasPred = true;

    // 미확인 입력 버퍼에 기록(재조정 replay 용). 폭주 방어 상한.
    if (m_pending.size() < 4096) m_pending.push_back(PendingInput{ seq, moveX, moveY, dt });
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
            if (ReadSnapshot(r, tick, snap)) { m_tick = tick; m_snapshot = std::move(snap); Reconcile(); }
            break;
        }
        case MsgType::Disconnect:
            m_connected = false;
            break;
        default: break;
        }
    }
}

void NetClient::Reconcile() {
    // 스냅샷에서 내 엔티티(권위 위치 + 서버가 마지막 처리한 입력 seq)를 찾는다.
    const EntitySnap* mine = nullptr;
    for (const EntitySnap& e : m_snapshot) if (e.netId == m_id) { mine = &e; break; }
    if (!mine) return;

    // 서버권위 위치로 리셋.
    m_predX = mine->x;
    m_predY = mine->y;
    m_hasPred = true;

    // 이미 서버가 처리한 입력은 확인됨 → 버린다.
    auto it = m_pending.begin();
    while (it != m_pending.end() && it->seq <= mine->lastInputSeq) it = m_pending.erase(it);

    // 아직 미확인인 입력을 권위 위치 위에 다시 적용(replay) → 예측을 서버와 정합.
    for (const PendingInput& p : m_pending) {
        m_predX += p.mx * m_speed * p.dt;
        m_predY += p.my * m_speed * p.dt;
        ClampTo(m_predX, m_predY, m_minX, m_minY, m_maxX, m_maxY);
    }
}

bool NetClient::GetPredicted(float& x, float& y) const {
    if (!m_hasPred) return false;
    x = m_predX;
    y = m_predY;
    return true;
}

bool NetClient::GetEntity(uint32_t netId, float& x, float& y) const {
    for (const EntitySnap& e : m_snapshot)
        if (e.netId == netId) { x = e.x; y = e.y; return true; }
    return false;
}

} // namespace mye::net
