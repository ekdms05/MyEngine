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
        m_pendingMovement.clear();
        m_snapshot3D.clear();
        m_snapshot2D.clear();
        m_pendingAttack2D.reset(); m_attackResult2D = {};
        m_pendingPortal2D.reset(); m_preparedSettings2D.reset(); m_preparedColliders2D = {};
        m_portalResult2D = {}; m_mapEpoch2D = m_portalSequence2D = 0;
        m_health2D.clear(); m_hasHealth2D = false; m_attackSequence2D = m_healthTick2D = 0;
        m_token = 0;
        m_inputSeq = 0;
        m_lastAck = 0;
        m_failure.clear();
    }
    m_server = server;
    BitWriter w;
    if (m_physics3D || m_settings2D) {
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&m_nonce), sizeof(m_nonce),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
            !m_nonce) {
            m_failure = "Connection nonce generation failed";
            return;
        }
        WriteHeader(w, m_settings2D ? MsgType::ConnectMap2D : MsgType::Connect3D);
        WriteString(w, username);
        WriteString(w, password);
        WriteU64(w, m_sceneHash);
        WriteU64(w, m_characterId);
        WriteU64(w, m_nonce);
    } else WriteConnect(w, username, password);
    const auto& bytes = w.Finish();
    const int sent = m_sock.SendTo(m_server, bytes.data(), bytes.size());
    if ((m_physics3D || m_settings2D) && sent != static_cast<int>(bytes.size())) {
        m_failure = "Authenticated connection send failed";
        return;
    }
    if (m_physics3D || m_settings2D) {
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
    if (m_physics3D || m_settings2D || !m_connected || !std::isfinite(dt) || dt <= 0 || dt > 1) return;
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
    m_pendingAttack2D.reset();
    m_pendingPortal2D.reset(); m_preparedSettings2D.reset(); m_preparedColliders2D = {};
    std::fill(m_handshake.begin(), m_handshake.end(), uint8_t{0});
    m_handshake.clear();
    BitWriter w;
    WriteHeader(w, m_settings2D ? MsgType::Disconnect2D : m_physics3D ? MsgType::Disconnect3D : MsgType::Disconnect);
    if (m_physics3D || m_settings2D) {
        WriteU64(w, m_token);
        WriteU64(w, m_nonce);
    }
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_server, bytes.data(), bytes.size());
    m_connected = false;
}

void NetClient::Receive() {
    if ((m_physics3D || m_settings2D) && !m_connected && m_failure.empty() && !m_handshake.empty() &&
        std::chrono::steady_clock::now() - m_lastHandshake > std::chrono::milliseconds(300)) {
        if (m_sock.SendTo(m_server, m_handshake.data(), m_handshake.size()) !=
            static_cast<int>(m_handshake.size())) {
            m_failure = "Authenticated connection retry send failed";
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
        if (m_physics3D || m_settings2D) {
            ReceiveAuthenticated(type, r, static_cast<size_t>(n));
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
    // Producers can stop after their final input; packet loss must not strand the acknowledgment queue.
    if (m_connected && !m_pendingMovement.empty() &&
        std::chrono::steady_clock::now() - m_lastMovementSend >= std::chrono::milliseconds(100)) {
        if (auto sent = SendPendingMovement(); !sent) {
            m_failure = sent.GetError().message;
            Disconnect();
        }
    }
    if (m_connected && m_pendingAttack2D) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_attackStarted2D >= std::chrono::seconds(5)) {
            m_failure = "Attack acknowledgement timed out"; Disconnect();
        } else if (now - m_attackSent2D >= std::chrono::milliseconds(100)) {
            if (auto sent = SendAttack2D(); !sent) { m_failure = sent.GetError().message; Disconnect(); }
        }
    }
    if (m_connected && m_pendingPortal2D) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_portalStarted2D >= std::chrono::seconds(5)) {
            m_failure = "Portal acknowledgement timed out"; Disconnect();
        } else if (now - m_portalSent2D >= std::chrono::milliseconds(100)) {
            if (auto sent = SendPortal2D(); !sent) { m_failure = sent.GetError().message; Disconnect(); }
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
    if (m_physics3D || m_settings2D || !m_hasPred) return false;
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
    if (m_connected || !m_handshake.empty() || hash == 0) return Error{"Configure 3D before connecting with a scene hash", 1};
    if (auto valid = phys::ValidateMotionSettings3D(settings); !valid) return valid.GetError();
    m_physics3D = &physics;
    m_settings2D.reset();
    m_settings3D = settings;
    m_sceneHash = hash;
    m_characterId = character;
    return {};
}
Expected<void, Error> NetClient::SendInput3D(Vec2 movement, bool jump) {
    if (!m_physics3D) return Error{"3D input requires 3D configuration", 1};
    return SendMovementInput(movement, jump);
}
Expected<void, Error> NetClient::Configure2D(std::span<const phys::CollisionBody2D> colliders,
    const phys::MotionSettings2D& settings, uint64_t hash, uint64_t character) {
    if (m_connected || !m_handshake.empty() || hash == 0)
        return Error{"Configure 2D before connecting with a scene hash", 1};
    if (auto valid = phys::ValidateMotionSettings2D(settings); !valid) return valid.GetError();
    if (auto valid = phys::ValidateCollisionBodies2D(colliders); !valid) return valid.GetError();
    m_settings2D = settings;
    m_colliders2D = colliders;
    m_physics3D = nullptr;
    m_sceneHash = hash;
    m_characterId = character;
    return {};
}
Expected<void, Error> NetClient::SendInput2D(Vec2 movement) {
    if (!m_settings2D) return Error{"2D input requires 2D configuration", 1};
    if (m_pendingPortal2D) return Error{"Movement waits for the pending portal result", 1};
    for (const auto& health : m_health2D) if (health.netId == m_id && health.hp == 0) movement = {};
    return SendMovementInput(movement, false);
}
Expected<void, Error> NetClient::SendMovementInput(Vec2 movement, bool jump) {
    if (!m_connected || !m_hasPred)
        return Error{"Movement input requires an authoritative spawn", 1};
    if (!std::isfinite(movement.x) || !std::isfinite(movement.y)) return Error{"Movement input must be finite", 1};
    if (m_pendingMovement.size() >= 240 || m_inputSeq == UINT32_MAX) {
        Disconnect();
        return Error{"Movement input acknowledgement timed out or sequence exhausted", 1};
    }
    NormalizeMove(movement.x, movement.y);
    if (m_settings2D) {
        auto next = m_prediction2D;
        if (auto moved = phys::StepMotion2D(next, movement, kFixedDelta2D, *m_settings2D, m_colliders2D); !moved)
            return moved.GetError();
        m_prediction2D = next;
    } else {
        auto next = m_prediction3D;
        if (auto moved = m_physics3D->Step(next, movement, jump, kFixedDelta3D, m_settings3D); !moved)
            return moved.GetError();
        m_prediction3D = next;
    }
    m_pendingMovement.push_back({++m_inputSeq, movement, jump});
    return SendPendingMovement();
}
Expected<void, Error> NetClient::SendPendingMovement() {
    BitWriter w;
    WriteHeader(w, m_settings2D ? MsgType::InputMap2D : MsgType::Input3D);
    WriteU64(w, m_token);
    if (m_settings2D) { WriteU64(w, m_sceneHash); w.WriteBits(m_mapEpoch2D, 32); }
    const auto count = std::min<size_t>(8, m_pendingMovement.size());
    w.WriteBits(static_cast<uint32_t>(count), 8);
    for (size_t i = 0; i < count; ++i)
        WriteMovementInput(w, m_pendingMovement[i]);
    const auto& bytes = w.Finish();
    m_lastMovementSend = std::chrono::steady_clock::now();
    if (m_sock.SendTo(m_server, bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
        return Error{"Movement input send failed", 1};
    return {};
}
void NetClient::ReceiveAuthenticated(MsgType type, BitReader& r, size_t size) {
    if (m_settings2D && type == MsgType::PortalResult2D) {
        if (!m_connected || ReadU64(r) != m_token || !r.Ok() || !m_pendingPortal2D) return;
        PortalResult2D result;
        result.sequence = r.ReadBits(32); result.accepted = r.ReadBits(1) != 0;
        result.sceneHash = ReadU64(r); result.epoch = r.ReadBits(32);
        if (!ReadState2D(r, result.state) || !ReadString(r, result.reason, 120) || !PacketComplete(r, size) ||
            result.sequence != m_pendingPortal2D->sequence) return;
        if (result.accepted) {
            if (!m_preparedSettings2D || result.sceneHash != m_pendingPortal2D->destinationHash ||
                result.epoch != m_mapEpoch2D + 1 || result.state.floorLevel != m_preparedSettings2D->body.floorLevel) return;
            m_settings2D = *m_preparedSettings2D; m_colliders2D = m_preparedColliders2D;
            m_sceneHash = result.sceneHash; m_mapEpoch2D = result.epoch;
            m_prediction2D = result.state; m_hasPred = true;
            m_inputSeq = m_lastAck = 0; m_pendingMovement.clear();
            m_snapshot2D.clear(); m_health2D.clear(); m_hasSnapshot = m_hasHealth2D = false;
        } else if (result.sceneHash != m_sceneHash || result.epoch != m_mapEpoch2D) return;
        m_portalResult2D = std::move(result);
        m_pendingPortal2D.reset(); m_preparedSettings2D.reset(); m_preparedColliders2D = {};
        return;
    }
    if (m_settings2D && (type == MsgType::AttackResult2D || type == MsgType::Health2D || type == MsgType::HealthMap2D)) {
        if (!m_connected || ReadU64(r) != m_token || !r.Ok()) return;
        if (type == MsgType::HealthMap2D) {
            const auto hash = ReadU64(r); const auto epoch = r.ReadBits(32);
            if (hash != m_sceneHash || epoch != m_mapEpoch2D || !r.Ok()) return;
        } else if (type == MsgType::Health2D && m_mapEpoch2D) return;
        if (type == MsgType::AttackResult2D) {
            AttackResult2D result;
            result.sequence = r.ReadBits(32); result.target = r.ReadBits(32);
            result.accepted = r.ReadBits(1) != 0;
            const auto damage = r.ReadBits(32), hp = r.ReadBits(32);
            if (damage > 1000000000 || hp > 1000000000 || (result.accepted ? damage == 0 : damage != 0) ||
                !ReadString(r, result.reason, 120) || !PacketComplete(r, size) || !m_pendingAttack2D ||
                result.sequence != m_pendingAttack2D->sequence || result.target != m_pendingAttack2D->target) return;
            result.damage = static_cast<int32_t>(damage); result.targetHp = static_cast<int32_t>(hp);
            m_attackResult2D = std::move(result); m_pendingAttack2D.reset();
            return;
        }
        const auto tick = r.ReadBits(32), count = r.ReadBits(8);
        if (!count || count > kMaxSnapshotEntities2D || (m_hasHealth2D && !SequenceNewer(tick, m_healthTick2D))) return;
        std::vector<EntityHealth2D> health;
        for (uint32_t i = 0; i < count; ++i) {
            const auto id = r.ReadBits(32), hp = r.ReadBits(32), maximum = r.ReadBits(32);
            if (!id || !maximum || maximum > 1000000000 || hp > maximum ||
                std::any_of(health.begin(), health.end(), [&](const auto& value) { return value.netId == id; })) return;
            health.push_back({id, static_cast<int32_t>(hp), static_cast<int32_t>(maximum)});
        }
        if (!PacketComplete(r, size) ||
            std::none_of(health.begin(), health.end(), [&](const auto& value) { return value.netId == m_id; })) return;
        m_health2D = std::move(health); m_healthTick2D = tick; m_hasHealth2D = true;
        return;
    }
    const auto disconnectType = m_settings2D ? MsgType::Disconnect2D : MsgType::Disconnect3D;
    if (type == (m_settings2D ? MsgType::Accept2D : MsgType::Accept3D) || (m_settings2D && type == MsgType::AcceptMap2D)) {
        const auto id = r.ReadBits(32);
        const auto token = ReadU64(r), nonce = ReadU64(r);
        if (type == MsgType::AcceptMap2D) {
            const auto hash = ReadU64(r); const auto epoch = r.ReadBits(32);
            if (hash != m_sceneHash || epoch != m_mapEpoch2D) return;
        }
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
    if (type != (m_settings2D ? MsgType::Snapshot2D : MsgType::Snapshot3D) && type != disconnectType &&
        !(m_settings2D && type == MsgType::SnapshotMap2D)) return;
    const auto token = ReadU64(r);
    if (type == disconnectType) {
        const auto nonce = ReadU64(r);
        if (nonce == m_nonce && PacketComplete(r, size) &&
            ((m_connected && token == m_token) || (!m_connected && token == 0))) {
            m_connected = false;
            m_failure = "Server rejected or closed the authenticated session";
            std::fill(m_handshake.begin(), m_handshake.end(), uint8_t{0});
            m_handshake.clear();
        }
        return;
    }
    if (!m_connected || token != m_token) return;
    if (type == MsgType::SnapshotMap2D) {
        const auto hash = ReadU64(r); const auto epoch = r.ReadBits(32);
        if (hash != m_sceneHash || epoch != m_mapEpoch2D || !r.Ok()) return;
    } else if (type == MsgType::Snapshot2D && m_mapEpoch2D) return;
    const auto tick = r.ReadBits(32), count = r.ReadBits(8);
    if (count == 0 || count > (m_settings2D ? kMaxSnapshotEntities2D : kMaxSnapshotEntities3D) ||
        (m_hasSnapshot && !SequenceNewer(tick, m_tick)))
        return;
    if (m_settings2D) {
        std::vector<EntitySnap2D> snapshot;
        snapshot.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            EntitySnap2D entity;
            entity.netId = r.ReadBits(32); entity.ack = r.ReadBits(32);
            if (!entity.netId || !ReadState2D(r, entity.state) ||
                entity.state.floorLevel != m_settings2D->body.floorLevel ||
                std::any_of(snapshot.begin(), snapshot.end(), [&](const auto& s) { return s.netId == entity.netId; }))
                return;
            snapshot.push_back(entity);
        }
        if (!PacketComplete(r, size)) return;
        const auto mine = std::find_if(snapshot.begin(), snapshot.end(), [&](const auto& e) { return e.netId == m_id; });
        if (mine == snapshot.end() || mine->ack > m_inputSeq || mine->ack < m_lastAck) return;
        m_lastAck = mine->ack;
        m_snapshot2D = std::move(snapshot);
        m_tick = tick; m_hasSnapshot = true;
        Reconcile2D();
        return;
    }
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
    if (mine == snapshot.end() || mine->ack > m_inputSeq || mine->ack < m_lastAck) return;
    m_lastAck = mine->ack;
    m_snapshot3D = std::move(snapshot);
    m_tick = tick;
    m_hasSnapshot = true;
    Reconcile3D();
}
void NetClient::Reconcile3D() {
    for (const auto& mine : m_snapshot3D)
        if (mine.netId == m_id) {
            auto prediction = mine.state;
            m_pendingMovement.erase(m_pendingMovement.begin(),
                              std::find_if(m_pendingMovement.begin(), m_pendingMovement.end(), [&](const auto& input) {
                                  return SequenceNewer(input.seq, mine.ack);
                              }));
            for (const auto& input : m_pendingMovement)
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

void NetClient::Reconcile2D() {
    for (const auto& mine : m_snapshot2D)
        if (mine.netId == m_id) {
            auto prediction = mine.state;
            m_pendingMovement.erase(m_pendingMovement.begin(),
                std::find_if(m_pendingMovement.begin(), m_pendingMovement.end(), [&](const auto& input) {
                    return SequenceNewer(input.seq, mine.ack);
                }));
            for (const auto& input : m_pendingMovement)
                if (auto moved = phys::StepMotion2D(prediction, input.movement, kFixedDelta2D, *m_settings2D, m_colliders2D); !moved) {
                    m_failure = moved.GetError().message;
                    Disconnect(); return;
                }
            m_prediction2D = prediction; m_hasPred = true;
            return;
        }
}

Expected<void, Error> NetClient::Attack2D(uint32_t target) {
    if (!m_connected || !m_settings2D || !m_hasPred || !target || target == m_id || m_pendingAttack2D || m_pendingPortal2D ||
        m_attackSequence2D == UINT32_MAX)
        return Error{"Attack requires an admitted 2D player, another target and no pending attack", 1};
    m_pendingAttack2D = AttackRequest2D{++m_attackSequence2D, target};
    m_attackStarted2D = std::chrono::steady_clock::now();
    auto sent = SendAttack2D();
    if (!sent) { m_failure = sent.GetError().message; Disconnect(); }
    return sent;
}
Expected<void, Error> NetClient::SendAttack2D() {
    if (!m_pendingAttack2D) return Error{"No pending attack", 1};
    BitWriter w;
    WriteHeader(w, MsgType::AttackMap2D); WriteU64(w, m_token);
    WriteU64(w, m_sceneHash); w.WriteBits(m_mapEpoch2D, 32);
    w.WriteBits(m_pendingAttack2D->sequence, 32); w.WriteBits(m_pendingAttack2D->target, 32);
    const auto& bytes = w.Finish();
    m_attackSent2D = std::chrono::steady_clock::now();
    if (m_sock.SendTo(m_server, bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
        return Error{"Attack send failed", 1};
    return {};
}
Expected<void, Error> NetClient::EnterPortal2D(uint32_t portal, uint64_t hash,
    std::span<const phys::CollisionBody2D> colliders, const phys::MotionSettings2D& settings) {
    if (!m_connected || !m_settings2D || !m_hasPred || !portal || !hash || m_pendingPortal2D ||
        m_pendingAttack2D || !m_pendingMovement.empty() || m_portalSequence2D == UINT32_MAX || m_mapEpoch2D == UINT32_MAX)
        return Error{"Portal requires a prepared destination and drained 2D input/attack acknowledgements", 1};
    if (auto valid = phys::ValidateMotionSettings2D(settings); !valid) return valid.GetError();
    if (auto valid = phys::ValidateCollisionBodies2D(colliders); !valid) return valid.GetError();
    m_preparedSettings2D = settings; m_preparedColliders2D = colliders;
    m_pendingPortal2D = PortalRequest2D{++m_portalSequence2D, portal, m_mapEpoch2D, m_sceneHash, hash};
    m_portalStarted2D = std::chrono::steady_clock::now();
    auto sent = SendPortal2D();
    if (!sent) { m_failure = sent.GetError().message; Disconnect(); }
    return sent;
}
Expected<void, Error> NetClient::SendPortal2D() {
    if (!m_pendingPortal2D) return Error{"No pending portal", 1};
    const auto& request = *m_pendingPortal2D;
    BitWriter writer;
    WriteHeader(writer, MsgType::Portal2D); WriteU64(writer, m_token);
    WriteU64(writer, request.sourceHash); writer.WriteBits(request.sourceEpoch, 32);
    writer.WriteBits(request.sequence, 32); writer.WriteBits(request.portal, 32); WriteU64(writer, request.destinationHash);
    const auto& bytes = writer.Finish(); m_portalSent2D = std::chrono::steady_clock::now();
    if (m_sock.SendTo(m_server, bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
        return Error{"Portal request send failed", 1};
    return {};
}
} // namespace mye::net
