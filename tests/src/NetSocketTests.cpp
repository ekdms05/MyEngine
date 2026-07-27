// NetSocketTests.cpp — Winsock UDP 루프백 송수신 (docs/mmorpg/02, M9)
//
// 127.0.0.1 로 실제 UDP 왕복. 소켓을 못 여는 환경(CI 제한)에서는 우아하게 스킵.
#include "TestFramework.h"

#include "mye/net/UdpSocket.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace mye;
using namespace mye::net;

MYE_TEST(UdpLoopbackSendRecv) {
    NetSubsystem net;
    if (!net.ok) return;   // 소켓 불가 환경 → 스킵

    UdpSocket server, client;
    if (!server.Open(0) || !client.Open(0)) return;   // 바인드 실패 → 스킵

    const uint16_t port = server.LocalPort();
    MYE_EXPECT(port != 0);

    const char msg[] = "hello-net";
    const Endpoint dst = Endpoint::Loopback(port);
    MYE_EXPECT(client.SendTo(dst, msg, sizeof(msg)) == static_cast<int>(sizeof(msg)));

    // 논블로킹 수신 재시도(루프백은 보통 즉시 도착).
    char buf[64] = {};
    Endpoint from{};
    int got = 0;
    for (int i = 0; i < 300 && got <= 0; ++i) {
        got = server.RecvFrom(from, buf, sizeof(buf));
        if (got <= 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MYE_EXPECT(got == static_cast<int>(sizeof(msg)));
    MYE_EXPECT(std::string(buf) == "hello-net");
    MYE_EXPECT(from.port == client.LocalPort());   // 송신자 포트 일치
}

MYE_TEST(UdpEndpointParseAndFormat) {
    auto e = Endpoint::Parse("192.168.0.10", 9000);
    MYE_EXPECT(static_cast<bool>(e));
    MYE_EXPECT(e.Value().addr == 0xC0A8000Au);   // 192.168.0.10
    MYE_EXPECT(e.Value().port == 9000);
    MYE_EXPECT(e.Value().ToString() == "192.168.0.10:9000");
    MYE_EXPECT(Endpoint::Loopback(1234).ToString() == "127.0.0.1:1234");
    MYE_EXPECT(!Endpoint::Parse("not-an-ip", 1));
}
