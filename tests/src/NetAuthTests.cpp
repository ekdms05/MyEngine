// NetAuthTests.cpp — 서버 인증 커넥트: 계정 검증 후 수락/거부 루프백 (docs/mmorpg/02·03, M10)
//
// AccountStore 를 NetServer 인증기로 주입 → 올바른 자격증명은 수락(AccountOf 매핑),
// 틀린/없는 자격증명은 거부(admit 안 됨). 실제 UDP(127.0.0.1). 소켓 불가 환경은 스킵.
#include "TestFramework.h"

#include "mye/net/NetServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"
#include "mye/persist/AccountStore.h"

#include <chrono>
#include <cmath>
#include <thread>

using namespace mye;
using namespace mye::net;

namespace {
void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}

MYE_TEST(NetAuthenticatedConnectAcceptsAndRejects) {
    NetSubsystem net;
    if (!net.ok) return;

    // 계정 저장소 + 인증기(자격증명 → accountId, 0=거부).
    persist::AccountStore accounts;
    const auto heroId = accounts.Register("hero", "s3cret").Value();

    NetServer server;
    NetClient good, bad;
    if (!server.Start(0) || !good.Open(0) || !bad.Open(0)) return;   // 소켓 불가 → 스킵

    server.SetAuthenticator([&accounts](std::string_view u, std::string_view p) -> uint64_t {
        persist::LoginResult r = accounts.Login(u, p);
        return r.ok ? r.accountId : 0;
    });

    const Endpoint sep = Endpoint::Loopback(server.Port());
    good.Connect(sep, "hero", "s3cret");   // 올바른 자격증명
    bad.Connect(sep, "hero", "wrong");     // 틀린 비밀번호

    // 핸드셰이크 펌프.
    for (int i = 0; i < 500 && !good.Connected(); ++i) {
        server.Receive();
        good.Receive();
        bad.Receive();
        SleepMs(1);
    }

    // 올바른 클라만 수락 — 서버에 클라 1명, AccountOf 가 hero 계정.
    MYE_EXPECT(good.Connected());
    MYE_EXPECT(server.ClientCount() == 1);
    MYE_EXPECT(server.AccountOf(good.Id()) == heroId);

    // 틀린 클라는 admit 안 됨(거부 카운트 증가, 미접속).
    MYE_EXPECT(!bad.Connected());
    MYE_EXPECT(server.RejectedCount() >= 1);
}

// 인증기가 없으면 익명 수락(하위 호환).
MYE_TEST(NetAnonymousConnectWithoutAuthenticator) {
    NetSubsystem net;
    if (!net.ok) return;

    NetServer server;
    NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;

    const Endpoint sep = Endpoint::Loopback(server.Port());
    client.Connect(sep);   // 자격증명 없음

    for (int i = 0; i < 500 && !client.Connected(); ++i) {
        server.Receive();
        client.Receive();
        SleepMs(1);
    }
    MYE_EXPECT(client.Connected());
    MYE_EXPECT(server.ClientCount() == 1);
    MYE_EXPECT(server.AccountOf(client.Id()) == 0);   // 익명
    MYE_EXPECT(server.RejectedCount() == 0);
}

// 안티치트: 월드 경계 밖으로는 못 나감(좌표 sanity). 정상 플레이는 위반/킥 0.
MYE_TEST(NetAntiCheatWorldBoundsClamp) {
    NetSubsystem net;
    if (!net.ok) return;

    NetServer server;
    NetClient client;
    if (!server.Start(0) || !client.Open(0)) return;
    server.SetMoveSpeed(6.0f);
    server.SetWorldBounds(-5.0f, -5.0f, 5.0f, 5.0f);   // 좁은 경계

    const Endpoint sep = Endpoint::Loopback(server.Port());
    client.Connect(sep);
    for (int i = 0; i < 500 && !client.Connected(); ++i) { server.Receive(); client.Receive(); SleepMs(1); }
    MYE_EXPECT(client.Connected());

    // +X로 오래 달려도(누적 이동이 경계 훨씬 초과) 경계에서 멈춘다.
    for (int i = 0; i < 200; ++i) {
        client.SendInput(static_cast<uint32_t>(i + 1), 1.0f, 0.0f);
        SleepMs(1);
        server.Receive();
        server.Tick(1.0f / 60.0f);
    }
    float sx = 0, sy = 0;
    MYE_EXPECT(server.GetEntity(client.Id(), sx, sy));
    MYE_EXPECT(sx <= 5.0f + 1e-4f);   // 경계 밖으로 못 감
    MYE_EXPECT(sx > 4.9f);            // 경계까지는 도달

    // 정상(범위 내) 입력은 위반/킥을 유발하지 않음(오탐 없음).
    MYE_EXPECT(server.ViolationsOf(client.Id()) == 0);
    MYE_EXPECT(server.KickedCount() == 0);
    MYE_EXPECT(server.ClientCount() == 1);
}

MYE_TEST(NetRejectsTruncatedAndStaleInputAndLimitsDiagonalSpeed) {
    NetSubsystem subsystem;
    NetServer server;
    UdpSocket client;
    const bool ready = subsystem.ok && server.Start(0) && client.Open(0);
    MYE_EXPECT(ready);
    if (!ready) return;
    const auto endpoint = Endpoint::Loopback(server.Port());
    auto send = [&](BitWriter& writer) {
        const auto& bytes = writer.Finish();
        MYE_EXPECT(client.SendTo(endpoint, bytes.data(), bytes.size()) == static_cast<int>(bytes.size()));
        SleepMs(2);
        server.Receive();
    };
    BitWriter connect;
    WriteConnect(connect, {}, {});
    send(connect);
    MYE_EXPECT(server.ClientCount() == 1);
    if (server.ClientCount() != 1) return;
    const auto id = server.ClientIds().front();
    BitWriter valid;
    WriteInput(valid, 10, 1, 1);
    send(valid);
    server.Tick(1);
    float x = 0, y = 0;
    MYE_EXPECT(server.GetEntity(id, x, y));
    MYE_EXPECT(std::hypot(x, y) <= 6.001f);
    const float firstX = x, firstY = y;
    BitWriter old;
    WriteInput(old, 9, -1, -1);
    send(old);
    BitWriter cut;
    WriteHeader(cut, MsgType::Input);
    cut.WriteVarUint(11);
    send(cut);
    server.Tick(1);
    MYE_EXPECT(server.GetEntity(id, x, y));
    MYE_EXPECT(x > firstX && y > firstY);
}
