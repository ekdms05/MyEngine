// mye/net/NetClient.cpp — 클라이언트 구현 (NetClient.h 참조)
#include "mye/net/NetClient.h"
#include <Windows.h>
#include <bcrypt.h>

namespace mye::net {

void NetClient::Connect(const Endpoint& server, std::string_view username, std::string_view password) {
    if (!m_connected) {
        m_id = m_tick = 0;
        m_predX = m_predY = 0;
        m_hasPred = m_hasSnapshot = false;
        m_pending.clear();
        m_snapshot.clear();
        m_pending3D.clear();
        m_snapshot3D.clear();
        m_token = 0;
        m_inputSeq = 0;
        m_failure.clear();
    }
    m_server = server;
    BitWriter w;
    if (m_physics3D) {
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&m_nonce), sizeof(m_nonce),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
            !m_nonce) {
            m_failure = "Connection nonce generation failed";
            return;
        }
        WriteHeader(w, MsgType::Connect3D);
        WriteString(w, username);
        WriteString(w, password);
        WriteU64(w, m_sceneHash);
        WriteU64(w, m_characterId);
        WriteU64(w, m_nonce);
    } else WriteConnect(w, username, password);
    const auto& bytes = w.Finish();
    const int sent = m_sock.SendTo(m_server, bytes.data(), bytes.size());
    if (m_physics3D && sent != static_cast<int>(bytes.size())) {
        m_failure = "3D connection send failed";
        return;
    }
    if (m_physics3D) {
        m_handshake = bytes;
        m_lastHandshake = std::chrono::steady_clock::now();
    }
}

namespace {
void ClampTo(float& x, float& y, float minX, float minY, float maxX, float maxY) {
    if (x < minX) x = minX; else if (x > maxX) x = maxX;
    if (y < minY) y = minY; else if (y > maxY) y = maxY;
}
}

void NetClient::SendInput(uint32_t seq, float moveX, float moveY, float dt) {
    if (!m_connected || !std::isfinite(dt) || dt <= 0 || dt > 1) return;
    NormalizeMove(moveX, moveY);
    BitWriter w;
    WriteInput(w, seq, moveX, moveY);
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());

    // 클라 예측: 서버 응답을 기다리지 않고 로컬 위치를 즉시 이동(입력 지연 은폐).
    moveX = DequantizeFloat(QuantizeFloat(moveX, -1, 1, 12), -1, 1, 12);
    moveY = DequantizeFloat(QuantizeFloat(moveY, -1, 1, 12), -1, 1, 12);
    NormalizeMove(moveX, moveY);
    m_predX += moveX * m_speed * dt;
    m_predY += moveY * m_speed * dt;
    ClampTo(m_predX, m_predY, m_minX, m_minY, m_maxX, m_maxY);
    m_hasPred = true;

    // 미확인 입력 버퍼에 기록(재조정 replay 용). 폭주 방어 상한.
    if (m_pending.size() < 4096) m_pending.push_back(PendingInput{ seq, moveX, moveY, dt });
}

void NetClient::Disconnect() {
    m_handshake.clear();
    BitWriter w;
    WriteHeader(w, m_physics3D ? MsgType::Disconnect3D : MsgType::Disconnect);
    if (m_physics3D) {
        WriteU64(w, m_token);
        WriteU64(w, m_nonce);
    }
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
    m_connected = false;
}

void NetClient::Receive() {
    if (m_physics3D && !m_connected && m_failure.empty() && !m_handshake.empty() &&
        std::chrono::steady_clock::now() - m_lastHandshake > std::chrono::milliseconds(300)) {
        if (m_sock.SendTo(m_server, m_handshake.data(), m_handshake.size()) !=
            static_cast<int>(m_handshake.size())) {
            m_failure = "3D connection retry send failed";
            std::fill(m_handshake.begin(), m_handshake.end(), uint8_t{0});
            m_handshake.clear();
        }
        m_lastHandshake = std::chrono::steady_clock::now();
    }
    uint8_t buf[1400];
    Endpoint from{};
    for (int guard = 0; guard < 1024; ++guard) {
        const int n = m_sock.RecvFrom(from, buf, sizeof(buf));
        if (n <= 0) break;
        if (from != m_server) continue;

        BitReader r(buf, static_cast<size_t>(n));
        MsgType type;
        if (!ReadHeader(r, type)) continue;
        if (m_physics3D) {
            Receive3D(type, r, static_cast<size_t>(n));
            continue;
        }

        switch (type) {
        case MsgType::Accept: {
            const uint32_t id = ReadAccept(r);
            if (id != 0 && r.Ok()) { m_id = id; m_connected = true; }
            break;
        }
        case MsgType::Snapshot: {
            uint32_t tick = 0;
            std::vector<EntitySnap> snap;
            if (m_connected && ReadSnapshot(r, tick, snap) &&
                (!m_hasSnapshot || SequenceNewer(tick, m_tick))) {
                m_tick = tick; m_snapshot = std::move(snap); m_hasSnapshot = true; Reconcile();
            }
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
    const auto unconfirmed = std::find_if(m_pending.begin(), m_pending.end(),
        [&](const auto& input) { return SequenceNewer(input.seq, mine->lastInputSeq); });
    m_pending.erase(m_pending.begin(), unconfirmed);

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

Expected<void, Error> NetClient::Configure3D(const phys::PhysicsWorld3D& physics,
                                             const phys::MotionSettings3D& settings, uint64_t hash,
                                             uint64_t character) {
    if (m_connected || hash == 0) return Error{"Configure 3D before connecting with a scene hash", 1};
    if (auto valid = phys::ValidateMotionSettings3D(settings); !valid) return valid.GetError();
    m_physics3D = &physics;
    m_settings3D = settings;
    m_sceneHash = hash;
    m_characterId = character;
    return {};
}
Expected<void, Error> NetClient::SendInput3D(Vec2 movement, bool jump) {
    if (!m_physics3D || !m_connected || !m_hasPred)
        return Error{"3D input requires an authoritative spawn", 1};
    if (!std::isfinite(movement.x) || !std::isfinite(movement.y)) return Error{"3D input must be finite", 1};
    if (m_pending3D.size() >= 240 || m_inputSeq == UINT32_MAX) {
        Disconnect();
        return Error{"3D input acknowledgement timed out or sequence exhausted", 1};
    }
    NormalizeMove(movement.x, movement.y);
    auto next = m_prediction3D;
    if (auto moved = m_physics3D->Step(next, movement, jump, kFixedDelta3D, m_settings3D); !moved)
        return moved.GetError();
    m_prediction3D = next;
    m_pending3D.push_back({++m_inputSeq, movement, jump});
    BitWriter w;
    WriteHeader(w, MsgType::Input3D);
    WriteU64(w, m_token);
    const auto count = std::min<size_t>(8, m_pending3D.size());
    w.WriteBits(static_cast<uint32_t>(count), 8);
    for (size_t i = 0; i < count; ++i)
        WriteInput3D(w, m_pending3D[i]);
    const auto& bytes = w.Finish();
    if (m_sock.SendTo(m_server, bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
        return Error{"3D input send failed", 1};
    return {};
}
void NetClient::Receive3D(MsgType type, BitReader& r, size_t size) {
    if (type == MsgType::Accept3D) {
        const auto id = r.ReadBits(32);
        const auto token = ReadU64(r), nonce = ReadU64(r);
        if (id && token && nonce == m_nonce && PacketComplete(r, size) &&
            (!m_connected || (m_id == id && m_token == token))) {
            m_id = id;
            m_token = token;
            m_connected = true;
            std::fill(m_handshake.begin(), m_handshake.end(), uint8_t{0});
            m_handshake.clear();
        }
        return;
    }
    if (type != MsgType::Snapshot3D && type != MsgType::Disconnect3D) return;
    const auto token = ReadU64(r);
    if (type == MsgType::Disconnect3D) {
        const auto nonce = ReadU64(r);
        if (nonce == m_nonce && PacketComplete(r, size) &&
            ((m_connected && token == m_token) || (!m_connected && token == 0))) {
            m_connected = false;
            m_failure = "Server rejected or closed the authenticated 3D session";
            std::fill(m_handshake.begin(), m_handshake.end(), uint8_t{0});
            m_handshake.clear();
        }
        return;
    }
    if (!m_connected || token != m_token) return;
    const auto tick = r.ReadBits(32), count = r.ReadBits(8);
    if (count == 0 || count > kMaxSnapshotEntities3D || (m_hasSnapshot && !SequenceNewer(tick, m_tick)))
        return;
    std::vector<EntitySnap3D> snapshot;
    snapshot.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        EntitySnap3D entity;
        entity.netId = r.ReadBits(32);
        entity.ack = r.ReadBits(32);
        if (!entity.netId || !ReadState3D(r, entity.state) ||
            std::any_of(snapshot.begin(), snapshot.end(),
                        [&](const auto& s) { return s.netId == entity.netId; }))
            return;
        snapshot.push_back(entity);
    }
    if (!PacketComplete(r, size)) return;
    const auto mine =
        std::find_if(snapshot.begin(), snapshot.end(), [&](const auto& e) { return e.netId == m_id; });
    if (mine == snapshot.end() || SequenceNewer(mine->ack, m_inputSeq)) return;
    m_snapshot3D = std::move(snapshot);
    m_tick = tick;
    m_hasSnapshot = true;
    Reconcile3D();
}
void NetClient::Reconcile3D() {
    for (const auto& mine : m_snapshot3D)
        if (mine.netId == m_id) {
            auto prediction = mine.state;
            m_pending3D.erase(m_pending3D.begin(),
                              std::find_if(m_pending3D.begin(), m_pending3D.end(), [&](const auto& input) {
                                  return SequenceNewer(input.seq, mine.ack);
                              }));
            for (const auto& input : m_pending3D)
                if (auto moved = m_physics3D->Step(prediction, input.movement, input.jump, kFixedDelta3D,
                                                   m_settings3D);
                    !moved) {
                    m_failure = moved.GetError().message;
                    Disconnect();
                    return;
                }
            m_prediction3D = prediction;
            m_hasPred = true;
            return;
        }
}

} // namespace mye::net
