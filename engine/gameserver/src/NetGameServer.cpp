// mye/gameserver/NetGameServer.cpp — 넷↔게임 통합 어댑터 구현 (NetGameServer.h 참조)
#include "mye/gameserver/NetGameServer.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <Windows.h>
#include <bcrypt.h>
#include <cmath>

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
    const phys::MotionSettings2D& settings, uint64_t hash, std::string scene, Vec2 spawn,
    std::span<const MapPortal2D> portals, std::span<const MapSpawn2D> spawns) {
    return BindMap2D(colliders, settings, hash, std::move(scene), spawn, false, portals, spawns);
}
Expected<void, Error> NetGameServer::RegisterMap2D(std::span<const phys::CollisionBody2D> colliders,
    const phys::MotionSettings2D& settings, uint64_t hash, std::string scene, Vec2 spawn,
    std::span<const MapPortal2D> portals, std::span<const MapSpawn2D> spawns) {
    return BindMap2D(colliders, settings, hash, std::move(scene), spawn, true, portals, spawns);
}
Expected<void, Error> NetGameServer::BindMap2D(std::span<const phys::CollisionBody2D> colliders,
    const phys::MotionSettings2D& settings, uint64_t hash, std::string scene, Vec2 spawn,
    bool additional, std::span<const MapPortal2D> portals, std::span<const MapSpawn2D> spawns) {
    if (scene.empty()) return Error{"Online 2D requires a scene identity", 1};
    if (additional && std::any_of(m_maps2D.begin(), m_maps2D.end(), [&](const auto& value) { return value.sceneId == scene; }))
        return Error{"Online map scene identity must be unique", 1};
    for (size_t i = 0; i < portals.size(); ++i) {
        const auto& portal = portals[i];
        if (!portal.id || portal.sceneId.empty() || portal.spawnName.empty() || !std::isfinite(portal.radius) ||
            portal.radius <= 0 || portal.radius > 100 || !std::isfinite(portal.position.x) || !std::isfinite(portal.position.y) ||
            portal.floorLevel < 0 || portal.floorLevel > 7 ||
            std::any_of(portals.begin(), portals.begin() + i, [&](const auto& value) { return value.id == portal.id; }))
            return Error{"Online portal requires a unique id, finite position/radius, floor and authored destination", 1};
    }
    for (size_t i = 0; i < spawns.size(); ++i) {
        const auto& value = spawns[i];
        if (value.name.empty() || !std::isfinite(value.position.x) || !std::isfinite(value.position.y) ||
            value.floorLevel < 0 || value.floorLevel > 7 ||
            std::any_of(spawns.begin(), spawns.begin() + i, [&](const auto& previous) { return previous.name == value.name; }))
            return Error{"Online spawns require unique names, finite positions and floors", 1};
    }
    phys::MotionState2D initial;
    initial.position = spawn; initial.floorLevel = settings.body.floorLevel;
    if (!net::ValidState2D(initial)) return Error{"2D spawn is outside protocol state bounds", 1};
    auto body = settings.body;
    body.pos = spawn + settings.offset;
    if (auto valid = phys::ValidateSpawn2D(body, colliders); !valid) return valid.GetError();
    const auto identity = scene;
    net::NetServer::Admission2D admission = [this, colliders, settings, scene = std::move(scene), spawn](
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
        };
    auto configured = additional ? m_net.RegisterMap2D(colliders, settings, hash, std::move(admission)) :
                                  m_net.Configure2D(colliders, settings, hash, std::move(admission));
    if (!configured) return configured.GetError();
    if (!additional) m_maps2D.clear();
    m_maps2D.push_back({hash, identity, settings.body.floorLevel,
        std::vector<MapPortal2D>(portals.begin(), portals.end()), std::vector<MapSpawn2D>(spawns.begin(), spawns.end())});
    m_net.SetPortalHandler2D([this](uint32_t id, const auto& request) { return EnterPortal2D(id, request); });
    return {};
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

    if (m_net.Is2D()) for (const auto& [netId, sid] : m_netToSession)
        if (const auto* session = m_game.Get(sid)) m_net.SetMovementEnabled2D(netId, session->stats.hp > 0);
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

Expected<void, Error> NetGameServer::ConfigureCombat2D(const CombatPolicy2D& policy) {
    if (IsRunning() || !m_net.Is2D() || !std::isfinite(policy.range) || policy.range <= 0 || policy.range > 20 ||
        !std::isfinite(policy.power) || policy.power <= 0 || policy.power > 10 ||
        !policy.cooldownTicks || policy.cooldownTicks > 3600)
        return Error{"Configure 2D combat before starting: range (0,20], power (0,10], cooldown 1..3600 ticks", 1};
    uint64_t seed = 0;
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&seed), sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        return Error{"Combat random seed initialization failed", 1};
    m_combatPolicy = policy; m_combatRng = gameplay::RngState(seed);
    m_net.SetCombat2D([this](uint32_t id, const auto& request) { return Attack2D(id, request); },
        [this](uint32_t id) -> std::optional<net::EntityHealth2D> {
            const auto* player = m_game.Get(SessionOf(id));
            if (!player || player->stats.derived.maxHp <= 0 || player->stats.derived.maxHp > 1000000000 ||
                player->stats.hp < 0 || player->stats.hp > player->stats.derived.maxHp) return std::nullopt;
            return net::EntityHealth2D{id, player->stats.hp, player->stats.derived.maxHp};
        });
    return {};
}
net::AttackResult2D NetGameServer::Attack2D(uint32_t attacker, const net::AttackRequest2D& request) {
    const auto refused = [&](std::string reason) {
        return net::AttackResult2D{request.sequence, request.target, 0, 0, false, std::move(reason)};
    };
    auto* source = m_game.Get(SessionOf(attacker));
    auto* target = m_game.Get(SessionOf(request.target));
    if (!source || !target || source == target || source->sceneId != target->sceneId ||
        source->world3D || target->world3D || source->floorLevel != target->floorLevel)
        return refused("Target is not another player in this map and floor");
    if (source->stats.hp <= 0 || target->stats.hp <= 0) return refused("Dead players cannot attack or be attacked");
    const auto validStats = [](const gameplay::Stats& value) {
        return value.derived.attack >= 0 && value.derived.attack <= 1000000 && value.derived.defense >= 0 &&
            value.derived.maxHp > 0 && value.derived.maxHp <= 1000000000 && value.hp <= value.derived.maxHp &&
            std::isfinite(value.derived.critChance) && value.derived.critChance >= 0 && value.derived.critChance <= 1;
    };
    if (!validStats(source->stats) || !validStats(target->stats)) return refused("Combat stats are outside supported bounds");
    phys::MotionState2D a, b;
    if (!m_net.GetEntity2D(attacker, a) || !m_net.GetEntity2D(request.target, b) ||
        (b.position - a.position).Length() > m_combatPolicy.range) return refused("Target is outside attack range");
    const auto tick = m_net.CurrentTick();
    if (source->hasAttacked2D && tick - source->lastAttackTick2D < m_combatPolicy.cooldownTicks)
        return refused("Attack is cooling down");
    auto visible = m_net.HasLineOfSight2D(attacker, request.target);
    if (!visible) return refused(visible.GetError().message);
    if (!visible.Value()) return refused("A blocking collider separates the players");
    gameplay::AttackSpec spec;
    spec.power = m_combatPolicy.power;
    const auto damage = gameplay::ComputeDamage(source->stats, target->stats, spec, m_combatRng);
    gameplay::ApplyDamage(target->stats, damage.amount);
    source->hasAttacked2D = true; source->lastAttackTick2D = tick;
    m_net.SetMovementEnabled2D(request.target, target->stats.hp > 0);
    return {request.sequence, request.target, damage.amount, target->stats.hp, true, {}};
}
Expected<void, Error> NetGameServer::EnterPortal2D(uint32_t id, const net::PortalRequest2D& request) {
    auto* session = m_game.Get(SessionOf(id));
    phys::MotionState2D state;
    if (!session || session->stats.hp <= 0 || !m_net.GetEntity2D(id, state))
        return Error{"An alive authenticated 2D player is required", 1};
    const auto map = std::find_if(m_maps2D.begin(), m_maps2D.end(), [&](const auto& value) { return value.hash == m_net.MapHash2D(id); });
    if (map == m_maps2D.end()) return Error{"Current map is unavailable", 1};
    const auto portal = std::find_if(map->portals.begin(), map->portals.end(), [&](const auto& value) { return value.id == request.portal; });
    if (portal == map->portals.end() || portal->floorLevel != state.floorLevel ||
        (state.position - portal->position).Length() > portal->radius)
        return Error{"Portal is missing, on another floor or outside interaction range", 1};
    const auto target = std::find_if(m_maps2D.begin(), m_maps2D.end(), [&](const auto& value) { return value.sceneId == portal->sceneId; });
    if (target == m_maps2D.end() || target->hash != request.destinationHash)
        return Error{"Prepared destination does not match the server map", 1};
    const auto spawn = std::find_if(target->spawns.begin(), target->spawns.end(), [&](const auto& value) { return value.name == portal->spawnName; });
    if (spawn == target->spawns.end() || spawn->floorLevel != target->floorLevel)
        return Error{"Destination spawn is missing or belongs to another floor", 1};
    auto identity = target->sceneId; // Allocate before the authoritative commit.
    auto changed = m_net.TransferMap2D(id, target->hash, spawn->position);
    if (!changed) return changed.GetError();
    session->sceneId = std::move(identity); session->floorLevel = target->floorLevel;
    session->x = spawn->position.x; session->y = spawn->position.y; session->z = 0;
    return {};
}
} // namespace mye::gameserver
