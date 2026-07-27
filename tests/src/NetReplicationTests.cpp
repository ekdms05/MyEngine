// NetReplicationTests.cpp — 권위 서버 + 스냅샷 복제 루프백 (docs/mmorpg/02, M9)
//
// 클라 입력 → 서버권위 시뮬 → 스냅샷 브로드캐스트 → 클라가 서버 위치를 수신한다. 실제 UDP(127.0.0.1).
// 소켓 불가 환경은 우아하게 스킵.
#include "TestFramework.h"

#include "mye/net/NetServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace mye;
using namespace mye::net;

namespace {
void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}

MYE_TEST(NetAuthoritativeReplicationLoopback) {
    NetSubsystem net;
    if (!net.ok) return;

    NetServer server;
    NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;   // 소켓 불가 → 스킵
    server.SetMoveSpeed(6.0f);

    const Endpoint serverEp = Endpoint::Loopback(server.Port());
    client.Connect(serverEp);

    // 핸드셰이크(Connect → Accept).
    for (int i = 0; i < 500 && !client.Connected(); ++i) {
        server.Receive();
        client.Receive();
        SleepMs(1);
    }
    MYE_EXPECT(client.Connected());
    MYE_EXPECT(client.Id() == 1);
    MYE_EXPECT(server.ClientCount() == 1);

    // 60틱 동안 +X 입력을 보내며 서버 시뮬 + 복제.
    for (int i = 0; i < 60; ++i) {
        client.SendInput(static_cast<uint32_t>(i + 1), 1.0f, 0.0f);
        SleepMs(1);
        server.Receive();
        server.Tick(1.0f / 60.0f);
        server.Broadcast();
        SleepMs(1);
        client.Receive();
    }

    // 서버권위 위치가 +X로 이동(+Y는 거의 정지 — 입력 0의 양자화 편차만).
    float sx = 0, sy = 0;
    MYE_EXPECT(server.GetEntity(1, sx, sy));
    MYE_EXPECT(sx > 1.0f);
    MYE_EXPECT(std::fabs(sy) < 0.05f);   // 입력 12비트 양자화 중점 편차 허용

    // 클라가 서버 위치를 스냅샷으로 수신(양자화 오차 내 일치).
    float cx = 0, cy = 0;
    MYE_EXPECT(client.GetEntity(1, cx, cy));
    MYE_EXPECT(cx > 1.0f);
    MYE_EXPECT(std::fabs(cx - sx) <= 0.2f);
    MYE_EXPECT(client.LastTick() > 0);
}
