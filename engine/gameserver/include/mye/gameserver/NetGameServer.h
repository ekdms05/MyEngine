// mye/gameserver/NetGameServer.h — 넷↔게임플레이↔영속 통합 어댑터 (M9·M8·M10 통합)
//
// NetServer(전송·권위 이동)와 GameServer(캐릭터 세션·게임플레이·영속)를 잇는다.
// 매 틱: 소켓 수신 → 접속/퇴장 diff(계정 인증→캐릭터 세션 Join/Leave) → 권위 시뮬 →
// 세션 위치 동기 → 스냅샷 브로드캐스트. 인증된 계정만 세션을 얻는다(persist 계정 로그인).
#pragma once

#include "mye/gameserver/GameServer.h"
#include "mye/gameplay/Combat.h"
#include "mye/net/NetServer.h"

#include <cstdint>
#include <unordered_map>

namespace mye::gameserver {
struct CombatPolicy2D {
    float range = 1.5f, power = 1;
    uint32_t cooldownTicks = 30;
};
struct MapPortal2D {
    uint32_t id = 0;
    Vec2 position{};
    float radius = 0;
    int8_t floorLevel = 0;
    std::string sceneId, spawnName;
};
struct MapSpawn2D { std::string name; Vec2 position{}; int8_t floorLevel = 0; };

class NetGameServer {
public:
    Expected<void, Error> ConfigureCombat2D(const CombatPolicy2D& policy);
    explicit NetGameServer(persist::PersistenceService& persist);

    bool     Start(uint16_t port);
    Expected<void, Error> Stop();
    uint16_t Port() const { return m_net.Port(); }
    bool     IsRunning() const { return m_net.IsRunning(); }

    void SetMoveSpeed(float s) { m_speed = s; m_net.SetMoveSpeed(s); }
    Expected<void,Error> Configure3D(const phys::PhysicsWorld3D& physics,const phys::MotionSettings3D& settings,
        uint64_t sceneHash,std::string sceneId,Vec3 spawn);
    Expected<void, Error> Configure2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t sceneHash, std::string sceneId, Vec2 spawn,
        std::span<const MapPortal2D> portals = {}, std::span<const MapSpawn2D> spawns = {});
    Expected<void, Error> RegisterMap2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t sceneHash, std::string sceneId, Vec2 spawn,
        std::span<const MapPortal2D> portals = {}, std::span<const MapSpawn2D> spawns = {});

    // 한 서버 틱: 수신 → 세션 diff → 시뮬 → 위치 동기 → 브로드캐스트.
    void Tick(float dt);

    size_t     PlayerCount() const { return m_netToSession.size(); }
    SessionId  SessionOf(uint32_t netId) const;   // 0=없음
    GameServer& Game() { return m_game; }
    net::NetServer& Net() { return m_net; }

private:
    struct MapBinding2D {
        uint64_t hash;
        std::string sceneId;
        int8_t floorLevel;
        std::vector<MapPortal2D> portals;
        std::vector<MapSpawn2D> spawns;
    };
    std::vector<MapBinding2D> m_maps2D;
    Expected<void, Error> BindMap2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t hash, std::string scene, Vec2 spawn,
        bool additional, std::span<const MapPortal2D> portals, std::span<const MapSpawn2D> spawns);
    Expected<void, Error> EnterPortal2D(uint32_t id, const net::PortalRequest2D& request);
    net::AttackResult2D Attack2D(uint32_t attacker, const net::AttackRequest2D& request);
    CombatPolicy2D m_combatPolicy;
    gameplay::RngState m_combatRng;
    persist::PersistenceService&                  m_persist;
    net::NetServer                                m_net;
    GameServer                                    m_game;
    std::unordered_map<uint32_t, SessionId>       m_netToSession;   // netId → sessionId
    float                                         m_speed = 6.0f;
};

} // namespace mye::gameserver
