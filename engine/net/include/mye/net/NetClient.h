// mye/net/NetClient.h — 최소 클라이언트(UDP) (docs/mmorpg/02, M9)
//
// 서버에 접속(Connect→Accept로 clientId 수신)하고, 입력을 보내며, 스냅샷을 받아 원격 엔티티
// 상태를 보관한다. (클라 예측/보간은 후속 — 여기선 권위 스냅샷 수신·적용까지.)
#pragma once

#include "mye/net/UdpSocket.h"
#include "mye/net/Protocol.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

    void Receive();   // 소켓 드레인 + Accept/Snapshot 처리(+ 재조정)

    uint32_t Id() const { return m_id; }
    bool Connected() const { return m_connected; }
    uint32_t LastTick() const { return m_tick; }
    bool GetEntity(uint32_t netId, float& x, float& y) const;
    size_t EntityCount() const { return m_snapshot.size(); }

    // ---- 클라 예측/재조정(CSP) ----
    // 서버와 동일한 이동 속도·월드 경계로 예측/replay 해야 재조정이 수렴한다.
    void SetMoveSpeed(float s) { m_speed = s; }
    void SetWorldBounds(float minX, float minY, float maxX, float maxY) {
        m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
    }
    // 예측된 로컬 플레이어 위치(렌더에 사용). Connect 전/스폰 전이면 false.
    bool GetPredicted(float& x, float& y) const;
    size_t PendingInputs() const { return m_pending.size(); }   // 미확인 입력 수(재조정 대기)

private:
    void Reconcile();   // 스냅샷 수신 시 서버권위 위치로 리셋 후 미확인 입력 replay

    struct PendingInput { uint32_t seq = 0; float mx = 0, my = 0, dt = 0; };

    UdpSocket               m_sock;
    Endpoint                m_server{};
    uint32_t                m_id = 0;
    bool                    m_connected = false;
    std::vector<EntitySnap> m_snapshot;
    uint32_t                m_tick = 0;

    // 예측 상태.
    float  m_speed = 6.0f;
    float  m_predX = 0.0f, m_predY = 0.0f;
    bool   m_hasPred = false;
    std::vector<PendingInput> m_pending;
    float  m_minX = -512.0f, m_minY = -512.0f, m_maxX = 512.0f, m_maxY = 512.0f;
};

} // namespace mye::net
