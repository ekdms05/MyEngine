// mye/net/NetClient.h — UDP 클라이언트와 2D/XYZ 권위 상태의 예측·재실행
//
// 서버에 접속(Connect→Accept로 clientId 수신)하고, 입력을 보내며, 스냅샷을 받아 원격 엔티티
// 상태를 보관하고 미확인 입력을 재적용해 클라이언트 예측을 보정한다.
#pragma once

#include "mye/net/UdpSocket.h"
#include "mye/net/Protocol.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <optional>

namespace mye::net {

class NetClient {
public:
    bool Open(uint16_t port = 0) { return m_sock.Open(port); }
    void Close() { m_sock.Close(); }

    // 자격증명으로 접속. 익명은 빈 문자열(기본) — 서버 인증기 유무에 따라 수락/거부.
    void Connect(const Endpoint& server, std::string_view username = {}, std::string_view password = {});

    // 입력 송신 + 클라 예측(CSP): 로컬 위치를 즉시 이동시켜 입력 지연을 숨긴다. dt 는 서버와 일치시킬 것.
    void SendInput(uint32_t seq, float moveX, float moveY, float dt = 1.0f / 60.0f);
    void Disconnect();
    Expected<void,Error> Configure3D(const phys::PhysicsWorld3D& physics,const phys::MotionSettings3D& settings,uint64_t sceneHash,uint64_t characterId);
    Expected<void,Error> SendInput3D(Vec2 movement,bool jump);
    bool GetPredicted3D(phys::MotionState3D& state) const { if (!m_physics3D || !m_hasPred) return false; state=m_prediction3D; return true; }
    const std::vector<EntitySnap3D>& LatestSnapshot3D() const { return m_snapshot3D; }
    Expected<void, Error> Configure2D(std::span<const phys::CollisionBody2D> colliders,
        const phys::MotionSettings2D& settings, uint64_t sceneHash, uint64_t characterId);
    Expected<void, Error> SendInput2D(Vec2 movement);
    bool GetPredicted2D(phys::MotionState2D& state) const {
        if (!m_settings2D || !m_hasPred) return false;
        state = m_prediction2D; return true;
    }
    const std::vector<EntitySnap2D>& LatestSnapshot2D() const { return m_snapshot2D; }
    Expected<void, Error> Attack2D(uint32_t target);
    bool AttackPending2D() const { return m_pendingAttack2D.has_value(); }
    const AttackResult2D& LastAttack2D() const { return m_attackResult2D; }
    const std::vector<EntityHealth2D>& LatestHealth2D() const { return m_health2D; }
    Expected<void, Error> EnterPortal2D(uint32_t portal, uint64_t destinationHash,
        std::span<const phys::CollisionBody2D> colliders, const phys::MotionSettings2D& settings);
    bool PortalPending2D() const { return m_pendingPortal2D.has_value(); }
    const PortalResult2D& LastPortal2D() const { return m_portalResult2D; }
    uint64_t SceneHash2D() const { return m_settings2D ? m_sceneHash : 0; }
    std::string_view Failure() const { return m_failure; }

    // Keep polling after the final input: drains/reconciles and retries pending authenticated inputs at 100 ms.
    void Receive();

    uint32_t Id() const { return m_id; }
    bool Connected() const { return m_connected; }
    uint32_t LastTick() const { return m_tick; }
    bool GetEntity(uint32_t netId, float& x, float& y) const;
    size_t EntityCount() const { return m_settings2D ? m_snapshot2D.size() : m_physics3D ? m_snapshot3D.size() : m_snapshot.size(); }
    // 최근 수신 스냅샷 읽기 전용 뷰(원격 엔티티 보간 렌더용). 갱신은 Receive() 만.
    const std::vector<EntitySnap>& LatestSnapshot() const { return m_snapshot; }

    // ---- 클라 예측/재조정(CSP) ----
    // 서버와 동일한 이동 속도·월드 경계로 예측/replay 해야 재조정이 수렴한다.
    void SetMoveSpeed(float s) { m_speed = s; }
    void SetWorldBounds(float minX, float minY, float maxX, float maxY) {
        m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
    }
    // 예측된 로컬 플레이어 위치(렌더에 사용). Connect 전/스폰 전이면 false.
    bool GetPredicted(float& x, float& y) const;
    size_t PendingInputs() const { return m_settings2D || m_physics3D ? m_pendingMovement.size() : m_pending.size(); }

private:
    void Reconcile3D();
    void Reconcile2D();
    void ReceiveAuthenticated(MsgType type,BitReader& reader,size_t bytes);
    Expected<void, Error> SendMovementInput(Vec2 movement, bool jump);
    Expected<void, Error> SendPendingMovement();
    const phys::PhysicsWorld3D* m_physics3D=nullptr;
    phys::MotionSettings3D m_settings3D;
    phys::MotionState3D m_prediction3D;
    uint64_t m_sceneHash=0, m_characterId=0, m_token=0, m_nonce=0;
    uint32_t m_inputSeq=0, m_lastAck=0;
    std::vector<MovementInput> m_pendingMovement;
    std::vector<EntitySnap3D> m_snapshot3D;
    std::span<const phys::CollisionBody2D> m_colliders2D; // Non-owning; scene data outlives configuration.
    std::optional<phys::MotionSettings2D> m_settings2D;
    phys::MotionState2D m_prediction2D;
    std::vector<EntitySnap2D> m_snapshot2D;
    std::string m_failure;
    Expected<void, Error> SendAttack2D();
    Expected<void, Error> SendPortal2D();
    uint32_t m_mapEpoch2D = 0, m_portalSequence2D = 0;
    std::optional<PortalRequest2D> m_pendingPortal2D;
    PortalResult2D m_portalResult2D;
    std::optional<phys::MotionSettings2D> m_preparedSettings2D;
    std::span<const phys::CollisionBody2D> m_preparedColliders2D;
    std::chrono::steady_clock::time_point m_portalStarted2D, m_portalSent2D;
    std::optional<AttackRequest2D> m_pendingAttack2D;
    AttackResult2D m_attackResult2D;
    std::vector<EntityHealth2D> m_health2D;
    uint32_t m_attackSequence2D = 0, m_healthTick2D = 0;
    bool m_hasHealth2D = false;
    std::chrono::steady_clock::time_point m_attackStarted2D, m_attackSent2D;
    std::vector<uint8_t> m_handshake;
    std::chrono::steady_clock::time_point m_lastHandshake;
    std::chrono::steady_clock::time_point m_lastMovementSend;
    void Reconcile();   // 스냅샷 수신 시 서버권위 위치로 리셋 후 미확인 입력 replay

    struct PendingInput { uint32_t seq = 0; float mx = 0, my = 0, dt = 0; };

    UdpSocket               m_sock;
    Endpoint                m_server{};
    uint32_t                m_id = 0;
    bool                    m_connected = false;
    std::vector<EntitySnap> m_snapshot;
    uint32_t                m_tick = 0;
    bool                    m_hasSnapshot = false;

    // 예측 상태.
    float  m_speed = 6.0f;
    float  m_predX = 0.0f, m_predY = 0.0f;
    bool   m_hasPred = false;
    std::vector<PendingInput> m_pending;
    float  m_minX = -512.0f, m_minY = -512.0f, m_maxX = 512.0f, m_maxY = 512.0f;
};

} // namespace mye::net
