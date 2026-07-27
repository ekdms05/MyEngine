// MyServer — 헤드리스 게임 서버 (docs/mmorpg/02, M9)
//
// 윈도우 콘솔 서버: 고정 틱으로 NetServer 를 돌리며 클라 입력을 서버권위로 시뮬하고 스냅샷을
// 브로드캐스트한다. (ECS 월드·게임플레이 통합은 후속 — 여기선 넷코드 루프 골격.)
//
// CLI: --port <n>(기본 27015) · --tickrate <hz>(기본 20) · --ticks <n>(n틱 후 종료; 검증용).
#include "mye/net/NetServer.h"
#include "mye/net/UdpSocket.h"
#include "mye/core/Log.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace mye;

int main(int argc, char** argv) {
    uint16_t port = 27015;
    int tickrate = 20;
    long long maxTicks = -1;   // -1 = 무한

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--tickrate" && i + 1 < argc) tickrate = std::atoi(argv[++i]);
        else if (a == "--ticks" && i + 1 < argc) maxTicks = std::atoll(argv[++i]);
    }
    if (tickrate < 1) tickrate = 20;

    net::NetSubsystem sys;
    if (!sys.ok) { MYE_LOG_ERROR("Server", "Winsock 초기화 실패"); return 1; }

    net::NetServer server;
    if (!server.Start(port)) { MYE_LOG_ERROR("Server", "port {} 바인드 실패", port); return 2; }
    MYE_LOG_INFO("Server", "MyServer 시작 — port {}, tickrate {}Hz", server.Port(), tickrate);

    const float dt = 1.0f / static_cast<float>(tickrate);
    const auto tickDuration = std::chrono::microseconds(1'000'000 / tickrate);

    long long tick = 0;
    while (maxTicks < 0 || tick < maxTicks) {
        const auto start = std::chrono::steady_clock::now();

        server.Receive();
        server.Tick(dt);
        server.Broadcast();
        ++tick;

        if (tick % tickrate == 0)   // 대략 1초마다
            MYE_LOG_INFO("Server", "tick {} · clients {}", server.CurrentTick(), server.ClientCount());

        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < tickDuration) std::this_thread::sleep_for(tickDuration - elapsed);
    }

    MYE_LOG_INFO("Server", "종료 (총 {} tick)", tick);
    server.Stop();
    return 0;
}
