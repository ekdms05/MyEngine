// mye/gameserver/NetGameServer.cpp — 넷↔게임 통합 어댑터 구현 (NetGameServer.h 참조)
#include "mye/gameserver/NetGameServer.h"
#include "mye/core/Log.h"

#include <algorithm>

namespace mye::gameserver {

NetGameServer::NetGameServer(persist::PersistenceService& persist)
    : m_persist(persist), m_game(persist) {
    // 인증기: 자격증명 → accountId(0=거부). 세션은 인증된 계정만 얻는다.
    m_net.SetAuthenticator([&persist](std::string_view u, std::string_view p) -> uint64_t {
        persist::LoginResult r = persist.Accounts().Login(u, p);
        return r.ok ? r.accountId : 0;
    });
}

bool NetGameServer::Start(uint16_t port) {
    m_net.SetMoveSpeed(m_speed);
    return m_net.Start(port);
}

Expected<void, Error> NetGameServer::Stop() {
    if (auto r = m_game.FlushSessions(); !r) return r.GetError();
    for (const auto& [netId, session] : m_netToSession) {
        if (auto r = m_game.Leave(session); !r) return r.GetError();
    }
    m_net.Stop();
    m_netToSession.clear();
    return {};
}

SessionId NetGameServer::SessionOf(uint32_t netId) const {
    auto it = m_netToSession.find(netId);
    return it == m_netToSession.end() ? 0 : it->second;
}
Expected<void, Error> NetGameServer::Configure2D(std::span<const phys::CollisionBody2D> colliders,
    const phys::MotionSettings2D& settings, uint64_t hash, std::string scene, Vec2 spawn) {
    if (scene.empty()) return Error{"Online 2D requires a scene identity", 1};
    phys::MotionState2D initial;
    initial.position = spawn; initial.floorLevel = settings.body.floorLevel;
    if (!net::ValidState2D(initial)) return Error{"2D spawn is outside protocol state bounds", 1};
    auto body = settings.body;
    body.pos = spawn + settings.offset;
    if (auto valid = phys::ValidateSpawn2D(body, colliders); !valid) return valid.GetError();
    return m_net.Configure2D(colliders, settings, hash,
        [this, colliders, settings, scene = std::move(scene), spawn](
            uint32_t netId, uint64_t account, uint64_t character) -> Expected<phys::MotionState2D, Error> {
            if (!character) {
                const auto characters = m_persist.Characters().ListByAccount(account);
                if (characters.empty()) return Error{"Account has no character", 1};
                character = characters.front();
            }
            const auto* rec = m_persist.Characters().Get(character);
            if (!rec || rec->accountId != account) return Error{"Character ownership mismatch", 1};
            if (rec->world3D || rec->posZ != 0 || (!rec->sceneId.empty() &&
                (rec->sceneId != scene || rec->floorLevel != settings.body.floorLevel)))
                return Error{"Character belongs to a different scene, floor or coordinate contract", 1};
            phys::MotionState2D state;
            state.position = rec->sceneId.empty() ? spawn : Vec2{rec->posX, rec->posY};
            state.facingRadians = rec->facingRadians;
            state.floorLevel = settings.body.floorLevel;
            if (!net::ValidState2D(state)) return Error{"2D spawn is outside protocol state bounds", 1};
            auto body = settings.body;
            body.pos = state.position + settings.offset;
            if (auto valid = phys::ValidateSpawn2D(body, colliders); !valid) return valid.GetError();
            auto joined = m_game.Join(account, character);
            if (!joined) return joined.GetError();
            m_netToSession[netId] = joined.Value();
            auto* session = m_game.Get(joined.Value());
            session->sceneId = scene; session->world3D = false; session->floorLevel = state.floorLevel;
            session->x = state.position.x; session->y = state.position.y; session->z = 0;
            session->facingRadians = state.facingRadians;
            return state;
        });
}
Expected<void, Error> NetGameServer::Configure3D(const phys::PhysicsWorld3D& physics,
                                                 const phys::MotionSettings3D& settings, uint64_t hash,
                                                 std::string scene, Vec3 spawn) {
    return m_net.Configure3D(
        physics, settings, hash,
        [this, &physics, settings, scene = std::move(scene), spawn](
            uint32_t netId, uint64_t account, uint64_t character) -> Expected<phys::MotionState3D, Error> {
            if (!character) {
                const auto characters = m_persist.Characters().ListByAccount(account);
                if (characters.empty()) return Error{"Account has no character", 1};
                character = characters.front();
            }
            const auto* rec = m_persist.Characters().Get(character);
            if (!rec || rec->accountId != account) return Error{"Character ownership mismatch", 1};
            if (!rec->sceneId.empty() && (rec->sceneId != scene || !rec->world3D))
                return Error{"Character belongs to a different scene or coordinate contract", 1};
            phys::MotionState3D state;
            state.position = rec->sceneId.empty() ? spawn : Vec3{rec->posX, rec->posY, rec->posZ};
            state.facingRadians = rec->facingRadians;
            if (auto moved = physics.Step(state, {}, false, net::kFixedDelta3D, settings); !moved)
                return moved.GetError();
            if (!net::ValidState3D(state)) return Error{"3D spawn is outside protocol state bounds", 1};
            auto joined = m_game.Join(account, character);
            if (!joined) return joined.GetError();
            m_netToSession[netId] = joined.Value();
            auto* session = m_game.Get(joined.Value());
            session->sceneId = scene;
            session->world3D = true;
            session->x = state.position.x;
            session->y = state.position.y;
            session->z = state.position.z;
            session->facingRadians = state.facingRadians;
            return state;
        });
}

void NetGameServer::Tick(float dt) {
    m_net.Receive();

    const std::vector<uint32_t> ids = m_net.ClientIds();

    // 신규 접속 → 계정의 캐릭터를 세션으로 로드.
    for (uint32_t netId : ids) {
        if (m_netToSession.count(netId)) continue;
        if (m_net.Is2D() || m_net.Is3D()) continue; // Authenticated admission creates the validated session.
        const uint64_t acc = m_net.AccountOf(netId);
        if (acc == 0) continue;   // 익명은 세션(캐릭터) 없음
        const auto characters = m_persist.Characters().ListByAccount(acc);
        const auto* record = characters.empty() ? nullptr : m_persist.Characters().Get(characters.front());
        if (!record || record->world3D || !record->sceneId.empty()) {
            m_net.DisconnectClient(netId);
            continue;
        }
        auto sid = m_game.Join(acc);
        if (sid) {
            m_netToSession[netId] = sid.Value();
            // 로드된 캐릭터 위치를 넷 권위 위치로 주입(재접속 복원).
            if (const PlayerSession* s = m_game.Get(sid.Value()))
                m_net.SetEntity(netId, s->x, s->y);
        } else m_net.DisconnectClient(netId);
    }

    // 퇴장(넷에서 사라진 매핑) → 세션 저장 후 제거.
    for (auto it = m_netToSession.begin(); it != m_netToSession.end();) {
        if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) {
            if (auto r = m_game.Leave(it->second); r) it = m_netToSession.erase(it);
            else {
                MYE_LOG_ERROR("GameServer", "session flush failed: {}", r.GetError().message);
                ++it;
            }
        } else {
            ++it;
        }
    }

    m_net.Tick(dt);

    // 권위 위치를 세션에 동기(영속 대비 — 다음 저장/퇴장 시 반영).
    for (const auto& [netId, sid] : m_netToSession) {
        if (m_net.Is2D()) {
            phys::MotionState2D state;
            if (m_net.GetEntity2D(netId, state))
                if (auto* s = m_game.Get(sid)) {
                    s->x = state.position.x; s->y = state.position.y;
                    s->facingRadians = state.facingRadians; s->floorLevel = state.floorLevel;
                }
            continue;
        }
        if (m_net.Is3D()) {
            phys::MotionState3D state;
            if (m_net.GetEntity3D(netId, state))
                if (auto* s = m_game.Get(sid)) {
                    s->x = state.position.x;
                    s->y = state.position.y;
                    s->z = state.position.z;
                    s->facingRadians = state.facingRadians;
                }
            continue;
        }
        float x = 0, y = 0;
        if (m_net.GetEntity(netId, x, y)) {
            if (PlayerSession* s = m_game.Get(sid)) { s->x = x; s->y = y; }
        }
    }

    m_net.Broadcast();
}

} // namespace mye::gameserver
