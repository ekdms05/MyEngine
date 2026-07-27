// MyServer — 헤드리스 게임 서버 (docs/mmorpg/02·03, M9·M10)
//
// 윈도우 콘솔 서버: 고정 틱으로 NetServer 를 돌리며 클라 입력을 서버권위로 시뮬하고 스냅샷을
// 브로드캐스트한다. 계정·캐릭터·거래원장을 세이브 디렉터리로 영속(PersistenceService)하고,
// 등록된 계정이 있으면 접속 시 자격증명 인증을 요구한다.
//
// CLI:
//   --port <n>        접속 포트(기본 27015)
//   --tickrate <hz>   서버 틱레이트(기본 20)
//   --ticks <n>       n틱 후 종료(검증용; 기본 무한)
//   --data <dir>      세이브 디렉터리(기본 server_data)
//   --autosave <sec>  자동 저장 주기 초(기본 60; 0=끄기)
//   --register u p    계정 등록 후 저장하고 종료(관리 도구)
//   --ban <user> [r]  계정 차단 후 저장하고 종료(GM), --unban <user> 차단 해제
#include "mye/net/NetServer.h"
#include "mye/net/UdpSocket.h"
#include "mye/persist/PersistenceService.h"
#include "mye/liveops/ServerConfig.h"
#include "mye/core/Log.h"

#include <filesystem>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

using namespace mye;

int main(int argc, char** argv) {
    uint16_t port = 27015;
    int tickrate = 20;
    long long maxTicks = -1;   // -1 = 무한
    std::string dataDir = "server_data";
    int autosaveSec = 60;
    std::string regUser, regPass;
    bool doRegister = false;
    std::string banUser, unbanUser, banReason;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--tickrate" && i + 1 < argc) tickrate = std::atoi(argv[++i]);
        else if (a == "--ticks" && i + 1 < argc) maxTicks = std::atoll(argv[++i]);
        else if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--autosave" && i + 1 < argc) autosaveSec = std::atoi(argv[++i]);
        else if (a == "--register" && i + 2 < argc) { regUser = argv[++i]; regPass = argv[++i]; doRegister = true; }
        else if (a == "--ban" && i + 1 < argc) { banUser = argv[++i]; if (i + 1 < argc && argv[i+1][0] != '-') banReason = argv[++i]; }
        else if (a == "--unban" && i + 1 < argc) unbanUser = argv[++i];
    }
    if (tickrate < 1) tickrate = 20;

    // ---- 영속 로드(첫 부팅이면 빈 상태) ----
    persist::PersistenceService persistence;
    if (auto r = persistence.LoadAll(dataDir); !r)
        MYE_LOG_WARN("Server", "영속 로드 경고: {}", r.GetError().message);

    // ---- 관리: 계정 등록 후 종료 ----
    if (doRegister) {
        auto reg = persistence.Accounts().Register(regUser, regPass);
        if (!reg) { MYE_LOG_ERROR("Server", "계정 등록 실패: {}", reg.GetError().message); return 3; }
        if (auto s = persistence.SaveAll(dataDir); !s) { MYE_LOG_ERROR("Server", "저장 실패: {}", s.GetError().message); return 4; }
        MYE_LOG_INFO("Server", "계정 '{}' 등록 완료(id={})", regUser, reg.Value());
        return 0;
    }

    // ---- 관리: 밴/언밴 후 종료(GM) ----
    if (!banUser.empty() || !unbanUser.empty()) {
        const std::string& target = banUser.empty() ? unbanUser : banUser;
        const persist::Account* acc = persistence.Accounts().FindByName(target);
        if (!acc) { MYE_LOG_ERROR("Server", "계정 '{}' 없음", target); return 5; }
        auto r = banUser.empty() ? persistence.Accounts().Unban(acc->id)
                                 : persistence.Accounts().Ban(acc->id, banReason);
        if (!r) { MYE_LOG_ERROR("Server", "제재 실패: {}", r.GetError().message); return 6; }
        if (auto s = persistence.SaveAll(dataDir); !s) { MYE_LOG_ERROR("Server", "저장 실패: {}", s.GetError().message); return 4; }
        MYE_LOG_INFO("Server", "계정 '{}' {} 완료", target, banUser.empty() ? "차단해제" : "차단");
        return 0;
    }

    // ---- 라이브옵스 설정 로드(config.json; 없으면 기본값) ----
    liveops::ServerConfig config;
    const std::string configPath = (std::filesystem::path(dataDir) / "config.json").string();
    if (std::filesystem::exists(configPath)) {
        if (auto r = config.LoadFromFile(configPath); !r) MYE_LOG_WARN("Server", "config 로드 경고: {}", r.GetError().message);
    }
    // CLI 기본을 CVar 로 오버라이드(있으면).
    if (config.Has("tickrate")) tickrate = static_cast<int>(config.GetInt("tickrate", tickrate));
    if (tickrate < 1) tickrate = 20;
    const float moveSpeed = static_cast<float>(config.GetFloat("move_speed", 6.0));

    net::NetSubsystem sys;
    if (!sys.ok) { MYE_LOG_ERROR("Server", "Winsock 초기화 실패"); return 1; }

    net::NetServer server;
    if (!server.Start(port)) { MYE_LOG_ERROR("Server", "port {} 바인드 실패", port); return 2; }
    server.SetMoveSpeed(moveSpeed);
    if (config.Has("max_violations")) server.SetMaxViolations(static_cast<uint32_t>(config.GetInt("max_violations", 10)));

    // 인증기: 점검모드면 전원 거부, 아니면 등록계정은 로그인 검증(익명 서버는 인증기 없음).
    const bool requireAuth = persistence.Accounts().Count() > 0;
    if (requireAuth || config.MaintenanceMode()) {
        server.SetAuthenticator([&persistence, &config, requireAuth](std::string_view u, std::string_view p) -> uint64_t {
            if (config.MaintenanceMode()) return 0;              // 점검 중 접속 차단
            if (!requireAuth) return 1;                          // 익명 허용(점검 아님)
            persist::LoginResult r = persistence.Accounts().Login(u, p);
            return r.ok ? r.accountId : 0;
        });
    }
    MYE_LOG_INFO("Server", "MyServer 시작 — port {}, tickrate {}Hz, data '{}', auth {}, maint {}",
                 server.Port(), tickrate, dataDir, requireAuth ? "on" : "off(anon)",
                 config.MaintenanceMode() ? "ON" : "off");

    const float dt = 1.0f / static_cast<float>(tickrate);
    const auto tickDuration = std::chrono::microseconds(1'000'000 / tickrate);
    const long long autosaveTicks = autosaveSec > 0 ? static_cast<long long>(autosaveSec) * tickrate : 0;
    const long long reloadTicks = static_cast<long long>(tickrate) * 5;   // 5초마다 config 핫리로드

    long long tick = 0;
    while (maxTicks < 0 || tick < maxTicks) {
        const auto start = std::chrono::steady_clock::now();

        server.Receive();
        server.Tick(dt);
        server.Broadcast();
        ++tick;

        if (tick % tickrate == 0)   // 대략 1초마다
            MYE_LOG_INFO("Server", "tick {} · clients {}", server.CurrentTick(), server.ClientCount());

        // config 핫리로드(운영 중 CVar/점검모드 조정 반영).
        if (tick % reloadTicks == 0 && std::filesystem::exists(configPath)) {
            if (auto r = config.LoadFromFile(configPath); r) {
                server.SetMoveSpeed(static_cast<float>(config.GetFloat("move_speed", 6.0)));
                if (config.Has("max_violations")) server.SetMaxViolations(static_cast<uint32_t>(config.GetInt("max_violations", 10)));
            }
        }

        if (autosaveTicks > 0 && tick % autosaveTicks == 0) {
            const int maxBk = static_cast<int>(config.GetInt("max_backups", 10));
            if (auto s = persistence.SaveAllWithBackup(dataDir, maxBk); !s) MYE_LOG_WARN("Server", "자동저장 실패: {}", s.GetError().message);
            else MYE_LOG_INFO("Server", "자동저장(백업 회전) 완료 (tick {})", tick);
        }

        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed < tickDuration) std::this_thread::sleep_for(tickDuration - elapsed);
    }

    // ---- 종료 시 저장(백업 회전) ----
    if (auto s = persistence.SaveAllWithBackup(dataDir, static_cast<int>(config.GetInt("max_backups", 10))); !s)
        MYE_LOG_WARN("Server", "종료 저장 실패: {}", s.GetError().message);
    MYE_LOG_INFO("Server", "종료 (총 {} tick, data '{}')", tick, dataDir);
    server.Stop();
    return 0;
}
