// mye/net/NetServer.h — UDP 권위 서버와 인증된 2D/XYZ 고정 틱 이동
//
// 클라 접속을 수락하고 클라별 엔티티를 서버권위로 시뮬(입력 적용)한 뒤, 매 tick 스냅샷을 전원에게
// 브로드캐스트한다. 이동은 서버가 계산 → 클라는 결과만 본다(스피드핵 방지의 기본). 순수 UDP 로직.
#pragma once

#include "mye/net/UdpSocket.h"
#include "mye/net/Protocol.h"
#include <deque>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace mye::net {

class NetServer {
public:
    // 자격증명 → 계정 id(0=거부). 서버 앱이 AccountStore 로 구현해 주입(net 은 persist 비의존).
    using Authenticator = std::function<uint64_t(std::string_view user, std::string_view pass)>;

    bool Start(uint16_t port);
    void Stop() { m_sock.Close(); m_clients.clear(); }
    uint16_t Port() const { return m_sock.LocalPort(); }
    bool IsRunning() const { return m_sock.IsOpen(); }

    // 인증기 주입(선택). 없으면 모든 Connect 를 익명 수락(하위 호환).
    void SetAuthenticator(Authenticator fn) { m_auth = std::move(fn); }

    // ---- 안티치트 이동 검증(서버권위 강화) ----
    // 월드 경계(이 밖으로는 못 나감; 좌표 sanity). 기본 [kWorldMin, kWorldMax].
    void SetWorldBounds(float minX, float minY, float maxX, float maxY) {
        m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
    }
    // 위반(범위초과 입력) 누적이 이 값에 도달하면 자동 킥. 0=끄기(기본 10).
    void SetMaxViolations(uint32_t n) { m_maxViolations = n; }
    uint32_t ViolationsOf(uint32_t netId) const;
    uint64_t KickedCount() const { return m_kicked; }

    void Receive();          // 소켓 드레인 + Connect/Input/Disconnect 처리
    void Tick(float dt);     // 서버 시뮬: 클라 입력을 각 엔티티에 적용
    void Broadcast();        // 스냅샷을 전 클라에 송신
    void DisconnectClient(uint32_t netId); // Server policy rejection after network authentication.

    // ---- 조회(테스트/디버그) ----
    size_t ClientCount() const { return m_clients.size(); }
    uint32_t CurrentTick() const { return m_tick; }
    bool GetEntity(uint32_t netId, float& x, float& y) const;
    void SetEntity(uint32_t netId, float x, float y);   // 서버권위 위치 주입(상위 통합용)
    uint64_t AccountOf(uint32_t netId) const;   // 클라의 인증 계정 id(0=익명/없음)
    std::vector<uint32_t> ClientIds() const;    // 현재 접속 클라 netId 목록(세션 diff용)
    uint64_t RejectedCount() const { return m_rejected; }

    void SetMoveSpeed(float s) { m_speed = s; }
    using Admission3D=std::function<Expected<phys::MotionState3D,Error>(uint32_t,uint64_t,uint64_t)>;
    Expected<void,Error> Configure3D(const phys::PhysicsWorld3D& physics,const phys::MotionSettings3D& settings,
        uint64_t sceneHash,Admission3D admission);
    bool GetEntity3D(uint32_t id,phys::MotionState3D& state) const;
    bool Is3D() const { return m_physics3D!=nullptr; }
    using Admission2D = std::function<Expected<phys::MotionState2D, Error>(uint32_t, uint64_t, uint64_t)>;
    Expected<void, Error> Configure2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t sceneHash, Admission2D admission);
    bool GetEntity2D(uint32_t id, phys::MotionState2D& state) const;
    bool Is2D() const { return m_settings2D.has_value(); }
    Expected<void, Error> RegisterMap2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t hash, Admission2D admission);
    Expected<void, Error> TransferMap2D(uint32_t id, uint64_t hash, Vec2 spawn);
    uint64_t MapHash2D(uint32_t id) const;
    using PortalHandler2D = std::function<Expected<void, Error>(uint32_t, const PortalRequest2D&)>;
    void SetPortalHandler2D(PortalHandler2D handler) { m_portal2D = std::move(handler); }
    using AttackHandler2D = std::function<AttackResult2D(uint32_t, const AttackRequest2D&)>;
    using HealthProvider2D = std::function<std::optional<EntityHealth2D>(uint32_t)>;
    void SetCombat2D(AttackHandler2D attack, HealthProvider2D health) {
        m_attack2D = std::move(attack); m_health2D = std::move(health);
    }
    void SetMovementEnabled2D(uint32_t id, bool enabled);
    Expected<bool, Error> HasLineOfSight2D(uint32_t from, uint32_t to) const;

private:
    struct Client {
        size_t mapIndex2D = 0;
        uint32_t mapEpoch2D = 0;
        bool mapsProtocol2D = false;
        std::optional<PortalRequest2D> portal2D;
        PortalRequest2D completedPortal2D;
        PortalResult2D portalResult2D;
        std::optional<AttackRequest2D> attack2D;
        AttackRequest2D completedAttack2D;
        AttackResult2D attackResult2D;
        bool movementEnabled2D = true;
        uint64_t token=0, characterId=0, nonce=0;
        phys::MotionState3D state;
        phys::MotionState2D state2D;
        std::deque<MovementInput> inputs;
        uint32_t receivedSeq=0, idleTicks=0;
        Endpoint ep;
        uint32_t id = 0;
        float    x = 0.0f, y = 0.0f;   // 서버권위 위치
        float    inX = 0.0f, inY = 0.0f;  // 최근 입력
        uint32_t lastInputSeq = 0;     // 마지막 처리 입력(클라 재조정용)
        uint64_t accountId = 0;        // 인증 계정(0=익명)
        uint32_t violations = 0;       // 안티치트 위반 누적(범위초과 입력)
    };
    Client* Find(const Endpoint& ep);
    void ReceiveAuthenticated(MsgType type,BitReader& reader,size_t bytes,const Endpoint& from);
    void KickIndex(size_t i);          // 인덱스 클라 제거(+ Disconnect 회신)

    void SendAttackResult2D(const Client& client);
    void SendPortalResult2D(const Client& client);
    UdpSocket           m_sock;
    std::vector<Client> m_clients;
    Authenticator       m_auth;
    const phys::PhysicsWorld3D* m_physics3D=nullptr; // Non-owning, configuration outlives server.
    phys::MotionSettings3D m_settings3D;
    uint64_t m_sceneHash=0;
    Admission3D m_admission3D;
    std::span<const phys::CollisionBody2D> m_colliders2D; // Non-owning; scene data outlives configuration.
    std::optional<phys::MotionSettings2D> m_settings2D;
    Admission2D m_admission2D;
    struct Map2D {
        uint64_t hash;
        phys::MotionSettings2D settings;
        std::span<const phys::CollisionBody2D> colliders;
        Admission2D admission;
    };
    std::vector<Map2D> m_maps2D;
    PortalHandler2D m_portal2D;
    AttackHandler2D m_attack2D;
    HealthProvider2D m_health2D;
    uint32_t            m_nextId = 1;
    uint32_t            m_tick = 0;
    float               m_speed = 6.0f;
    uint64_t            m_rejected = 0;
    uint64_t            m_kicked = 0;
    uint32_t            m_maxViolations = 10;
    // 월드 경계 기본값 = Protocol kWorldMin/kWorldMax(±512). SetWorldBounds 로 조정.
    float m_minX = -512.0f, m_minY = -512.0f, m_maxX = 512.0f, m_maxY = 512.0f;
};

} // namespace mye::net
