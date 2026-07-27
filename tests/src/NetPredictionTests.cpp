// NetPredictionTests.cpp — 클라 예측(CSP) + 서버 재조정 루프백 (docs/mmorpg/02, M9)
//
// 클라가 입력을 즉시 로컬 예측으로 반영(지연 은폐)하고, 서버 스냅샷의 lastInputSeq 로
// 미확인 입력만 replay 해 권위 위치와 정합시킨다. 실제 UDP(127.0.0.1). 소켓 불가 시 스킵.
#include "TestFramework.h"

#include "mye/net/NetServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace mye;
using namespace mye::net;

namespace { void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

MYE_TEST(NetClientPredictionAndReconciliation) {
    NetSubsystem net;
    if (!net.ok) return;

    NetServer server;
    NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;

    const float speed = 6.0f;
    const float dt = 1.0f / 60.0f;
    server.SetMoveSpeed(speed);
    client.SetMoveSpeed(speed);   // 서버와 동일해야 재조정 수렴

    const Endpoint sep = Endpoint::Loopback(server.Port());
    client.Connect(sep);
    for (int i = 0; i < 500 && !client.Connected(); ++i) { server.Receive(); client.Receive(); SleepMs(1); }
    MYE_EXPECT(client.Connected());
    MYE_EXPECT(client.Id() == 1);

    // 즉시 예측: 서버가 처리하기 전에 로컬 위치가 먼저 움직인다.
    client.SendInput(1, 1.0f, 0.0f, dt);
    float px0 = 0, py0 = 0;
    MYE_EXPECT(client.GetPredicted(px0, py0));
    MYE_EXPECT(px0 > 0.09f && px0 < 0.11f);   // 6*(1/60) ≈ 0.1
    MYE_EXPECT(client.PendingInputs() == 1);  // 아직 서버 미확인

    // 이후 입력을 보내며 서버 시뮬 + 스냅샷 재조정.
    for (int i = 2; i <= 60; ++i) {
        client.SendInput(static_cast<uint32_t>(i), 1.0f, 0.0f, dt);
        SleepMs(1);
        server.Receive();
        server.Tick(dt);
        server.Broadcast();
        SleepMs(1);
        client.Receive();   // 스냅샷 → Reconcile
    }

    // 서버권위 위치.
    float sx = 0, sy = 0;
    MYE_EXPECT(server.GetEntity(client.Id(), sx, sy));
    MYE_EXPECT(sx > 1.0f);

    // 예측이 권위와 정합(재조정 수렴). 모든 입력이 확인되어 미확인 버퍼가 비었다.
    float px = 0, py = 0;
    MYE_EXPECT(client.GetPredicted(px, py));
    MYE_EXPECT(client.PendingInputs() == 0);
    MYE_EXPECT(std::fabs(px - sx) <= 0.05f);   // 양자화 오차 내
    MYE_EXPECT(std::fabs(py - sy) <= 0.05f);
}

// 미확인 입력이 있을 때, 재조정은 권위 위치 위에 replay 해 예측 전진을 유지한다.
MYE_TEST(NetReconcileReplaysUnackedInputs) {
    NetSubsystem net;
    if (!net.ok) return;

    NetServer server;
    NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;
    const float dt = 1.0f / 60.0f;
    server.SetMoveSpeed(6.0f);
    client.SetMoveSpeed(6.0f);

    const Endpoint sep = Endpoint::Loopback(server.Port());
    client.Connect(sep);
    for (int i = 0; i < 500 && !client.Connected(); ++i) { server.Receive(); client.Receive(); SleepMs(1); }
    MYE_EXPECT(client.Connected());

    // 서버가 seq1 만 처리하도록: 입력 1 전송 후 서버 1틱, 그다음 입력 2,3 은 서버가 아직 처리 안 함.
    client.SendInput(1, 1.0f, 0.0f, dt);
    SleepMs(2); server.Receive(); server.Tick(dt); server.Broadcast(); SleepMs(2);

    // 추가 미확인 입력 2개(서버로 전송되지만 이번엔 서버 Receive/Tick 안 함).
    client.SendInput(2, 1.0f, 0.0f, dt);
    client.SendInput(3, 1.0f, 0.0f, dt);
    const float predBefore = [&]{ float x, y; client.GetPredicted(x, y); return x; }();

    // 스냅샷 수신 → 재조정. seq1 은 확인, 2·3 은 미확인이라 replay 유지.
    client.Receive();
    float px = 0, py = 0; MYE_EXPECT(client.GetPredicted(px, py));
    MYE_EXPECT(client.PendingInputs() == 2);       // 2,3 미확인
    // 재조정 후에도 예측이 뒤로 튀지 않음(권위+replay ≈ 이전 예측).
    MYE_EXPECT(std::fabs(px - predBefore) <= 0.05f);
    MYE_EXPECT(px > 0.0f);
}
