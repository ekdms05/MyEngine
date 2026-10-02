// mye/net/NetServer.cpp — 권위 서버 구현 (NetServer.h 참조)
#include "mye/net/NetServer.h"
#include "mye/net/Protocol.h"

#include "mye/core/Log.h"
#include <Windows.h>
#include <bcrypt.h>

namespace mye::net {

bool NetServer::Start(uint16_t port) {
    // Credentials are not encrypted by this development transport.
    if (!m_sock.Open(port, Endpoint::Loopback(port).addr)) return false;
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
        if (m_physics3D || Is2D()) {
            ReceiveAuthenticated(type, r, static_cast<size_t>(n), from);
            continue;
        }

        switch (type) {
        case MsgType::Connect: {
            std::string user, pass;
            if (!ReadConnect(r, user, pass)) break;   // 손상 패킷 방어

            Client* c = Find(from);
            if (!c) {
                if (m_clients.size() >= kMaxSnapshotEntities) {
                    ++m_rejected;
                    BitWriter rejection;
                    WriteHeader(rejection, MsgType::Disconnect);
                    const auto& bytes = rejection.Finish();
                    m_sock.SendTo(from, bytes.data(), bytes.size());
                    break;
                }
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
            if (!ReadInput(r, seq, mx, my)) break;
            if (Client* c = Find(from)) {
                if (!SequenceNewer(seq, c->lastInputSeq)) break;
                // 안티치트: 이동 입력은 단위벡터 성분(±1) 범위. 초과는 조작 → 위반 누적 후 클램프.
                if (mx < -1.001f || mx > 1.001f || my < -1.001f || my > 1.001f) {
                    ++c->violations;
                }
                NormalizeMove(mx, my);
                c->inX = mx;
                c->inY = my;
                c->lastInputSeq = seq;
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
    if (m_physics3D || Is2D()) {
        if (!std::isfinite(dt) || std::abs(dt - kFixedDelta3D) > 1e-6f) return;
        for (size_t i = m_clients.size(); i-- > 0;) {
            auto& c = m_clients[i];
            MovementInput input;
            if (!c.inputs.empty()) input = c.inputs.front();
            auto moved = Is2D() ? phys::StepMotion2D(c.state2D, input.movement, dt, *m_settings2D, m_colliders2D)
                               : m_physics3D->Step(c.state, input.movement, input.jump, dt, m_settings3D);
            if (!moved || (Is2D() ? !ValidState2D(c.state2D) : !ValidState3D(c.state)) || ++c.idleTicks > 300) {
                if (!moved) MYE_LOG_WARN("Net", "Motion failed for client {}: {}", c.id, moved.GetError().message);
                KickIndex(i);
                continue;
            }
            if (!c.inputs.empty()) {
                c.lastInputSeq = input.seq;
                c.inputs.pop_front();
            }
        }
        ++m_tick;
        return;
    }
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
    WriteHeader(w, Is2D() ? MsgType::Disconnect2D : m_physics3D ? MsgType::Disconnect3D : MsgType::Disconnect);
    if (m_physics3D || Is2D()) {
        WriteU64(w, m_clients[i].token);
        WriteU64(w, m_clients[i].nonce);
    }
    const auto& bytes = w.Finish();
    m_sock.SendTo(m_clients[i].ep, bytes.data(), bytes.size());
    m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(i));
    ++m_kicked;
}
void NetServer::DisconnectClient(uint32_t netId) {
    for (size_t i = 0; i < m_clients.size(); ++i)
        if (m_clients[i].id == netId) { KickIndex(i); return; }
}

uint32_t NetServer::ViolationsOf(uint32_t netId) const {
    for (const Client& c : m_clients)
        if (c.id == netId) return c.violations;
    return 0;
}

void NetServer::Broadcast() {
    if (m_physics3D || Is2D()) {
        for (const auto& recipient : m_clients) {
            BitWriter w;
            WriteHeader(w, Is2D() ? MsgType::Snapshot2D : MsgType::Snapshot3D);
            WriteU64(w, recipient.token);
            w.WriteBits(m_tick, 32);
            w.WriteBits(static_cast<uint32_t>(m_clients.size()), 8);
            for (const auto& c : m_clients) {
                w.WriteBits(c.id, 32);
                w.WriteBits(c.lastInputSeq, 32);
                if (Is2D()) WriteState2D(w, c.state2D);
                else WriteState3D(w, c.state);
            }
            const auto& bytes = w.Finish();
            if (bytes.size() <= 1400) m_sock.SendTo(recipient.ep, bytes.data(), bytes.size());
        }
        return;
    }
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

Expected<void, Error> NetServer::Configure3D(const phys::PhysicsWorld3D& physics,
                                             const phys::MotionSettings3D& settings, uint64_t hash,
                                             Admission3D admission) {
    if (IsRunning() || !admission || hash == 0)
        return Error{"Configure 3D before starting with a scene hash and admission callback", 1};
    if (auto valid = phys::ValidateMotionSettings3D(settings); !valid) return valid.GetError();
    m_physics3D = &physics;
    m_settings2D.reset();
    m_admission2D = {};
    m_settings3D = settings;
    m_sceneHash = hash;
    m_admission3D = std::move(admission);
    return {};
}
Expected<void, Error> NetServer::Configure2D(std::span<const phys::CollisionBody2D> colliders,
    const phys::MotionSettings2D& settings, uint64_t hash, Admission2D admission) {
    if (IsRunning() || !admission || hash == 0)
        return Error{"Configure 2D before starting with a scene hash and admission callback", 1};
    if (auto valid = phys::ValidateMotionSettings2D(settings); !valid) return valid.GetError();
    if (auto valid = phys::ValidateCollisionBodies2D(colliders); !valid) return valid.GetError();
    m_settings2D = settings;
    m_colliders2D = colliders;
    m_physics3D = nullptr;
    m_admission3D = {};
    m_sceneHash = hash;
    m_admission2D = std::move(admission);
    return {};
}
bool NetServer::GetEntity2D(uint32_t id, phys::MotionState2D& state) const {
    if (!Is2D()) return false;
    for (const auto& c : m_clients)
        if (c.id == id) { state = c.state2D; return true; }
    return false;
}
bool NetServer::GetEntity3D(uint32_t id, phys::MotionState3D& state) const {
    if (!Is3D()) return false;
    for (const auto& c : m_clients)
        if (c.id == id) {
            state = c.state;
            return true;
        }
    return false;
}
void NetServer::ReceiveAuthenticated(MsgType type, BitReader& r, size_t size, const Endpoint& from) {
    const auto connectType = Is2D() ? MsgType::Connect2D : MsgType::Connect3D;
    const auto disconnectType = Is2D() ? MsgType::Disconnect2D : MsgType::Disconnect3D;
    if (type == connectType) {
        std::string user, pass;
        if (!ReadConnect(r, user, pass)) return;
        const auto hash = ReadU64(r), character = ReadU64(r), nonce = ReadU64(r);
        if (!nonce || !PacketComplete(r, size)) return;
        const auto reject = [&] {
            ++m_rejected;
            BitWriter w;
            WriteHeader(w, disconnectType);
            WriteU64(w, 0);
            WriteU64(w, nonce);
            const auto& bytes = w.Finish();
            m_sock.SendTo(from, bytes.data(), bytes.size());
        };
        if (hash != m_sceneHash || !m_auth) {
            reject();
            return;
        }
        auto* c = Find(from);
        if (!c) {
            if (m_clients.size() >= (Is2D() ? kMaxSnapshotEntities2D : kMaxSnapshotEntities3D) || !m_nextId) {
                reject();
                return;
            }
            const auto account = m_auth(user, pass);
            if (!account) {
                reject();
                return;
            }
            Client candidate;
            candidate.ep = from;
            candidate.id = m_nextId++;
            candidate.accountId = account;
            candidate.characterId = character;
            candidate.nonce = nonce;
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&candidate.token), sizeof(candidate.token),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
                !candidate.token) {
                reject();
                return;
            }
            if (Is2D()) {
                auto admitted = m_admission2D(candidate.id, account, character);
                if (!admitted || !ValidState2D(admitted.Value()) ||
                    admitted.Value().floorLevel != m_settings2D->body.floorLevel) {
                    if (!admitted) MYE_LOG_WARN("Net", "2D admission rejected: {}", admitted.GetError().message);
                    reject();
                    return;
                }
                candidate.state2D = admitted.Value();
            } else {
                auto admitted = m_admission3D(candidate.id, account, character);
                if (!admitted) {
                    MYE_LOG_WARN("Net", "3D admission rejected: {}", admitted.GetError().message);
                    reject();
                    return;
                }
                candidate.state = admitted.Value();
            }
            m_clients.push_back(std::move(candidate));
            c = &m_clients.back();
        } else if (c->characterId != character || c->nonce != nonce || m_auth(user, pass) != c->accountId) {
            reject();
            return;
        }
        BitWriter w;
        WriteHeader(w, Is2D() ? MsgType::Accept2D : MsgType::Accept3D);
        w.WriteBits(c->id, 32);
        WriteU64(w, c->token);
        WriteU64(w, c->nonce);
        const auto& bytes = w.Finish();
        m_sock.SendTo(from, bytes.data(), bytes.size());
        return;
    }
    auto* c = Find(from);
    if (!c || (type != (Is2D() ? MsgType::Input2D : MsgType::Input3D) && type != disconnectType)) return;
    if (ReadU64(r) != c->token || !r.Ok()) return;
    if (type == disconnectType) {
        const auto nonce = ReadU64(r);
        if (nonce == c->nonce && PacketComplete(r, size))
            m_clients.erase(m_clients.begin() + (c - m_clients.data()));
        return;
    }
    const auto count = r.ReadBits(8);
    if (count == 0 || count > 8) return;
    std::vector<MovementInput> batch;
    batch.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        MovementInput input;
        if (!ReadMovementInput(r, input) || (Is2D() && input.jump)) return;
        batch.push_back(input);
    }
    if (!PacketComplete(r, size)) return;
    auto received = c->receivedSeq;
    size_t accepted = 0;
    for (const auto& input : batch) {
        if (!SequenceNewer(input.seq, received)) continue;
        // Redundant contiguous inputs recover lost datagrams; never acknowledge skipped simulation.
        if (input.seq != received + 1 || c->inputs.size() + accepted >= 240) return;
        received = input.seq;
        ++accepted;
    }
    for (const auto& input : batch) {
        if (!SequenceNewer(input.seq, c->receivedSeq)) continue;
        c->inputs.push_back(input);
        c->receivedSeq = input.seq;
    }
    c->idleTicks = 0;
}

} // namespace mye::net
