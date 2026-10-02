// MyServer — 루프백 권위 이동·계정·저장·지표를 조합하는 콘솔 서버
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
//   --bots <n>        배회 봇 n개 내장(계정 자동 등록 — 혼자서도 원격 플레이어가 보임)
//   --register u p    계정 등록 후 저장하고 종료(관리 도구)
//   --make-char u name 계정 u 에 캐릭터 생성 후 저장하고 종료(관리 도구)
//   --ban <user> [r]  계정 차단 후 저장하고 종료(GM), --unban <user> 차단 해제
// 운영: <data>/config.json 으로 CVar/피처플래그/점검모드 핫리로드, <data>/metrics.json 관찰성.
// Ctrl+C·콘솔 닫기 시 우아한 종료(세이브·백업·메트릭 기록 후 정지).
#include "mye/gameserver/NetGameServer.h"
#include "mye/net/NetClient.h"
#include "mye/net/UdpSocket.h"
#include "mye/persist/PersistenceService.h"
#include "mye/liveops/ServerConfig.h"
#include "mye/liveops/Metrics.h"
#include "mye/core/Log.h"
#include "mye/runtime/OnlineScene.h"
#include <optional>

#include <filesystem>
#include <fstream>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>   // SetConsoleCtrlHandler (우아한 종료)

// Ctrl+C·콘솔 닫기 시 세이브 후 안전 종료를 위한 플래그(핸들러에서만 set).
static std::atomic<bool> g_stop{false};
static BOOL WINAPI ConsoleCtrlHandler(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT: case CTRL_BREAK_EVENT: case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT: case CTRL_SHUTDOWN_EVENT:
        g_stop.store(true);
        return TRUE;
    default:
        return FALSE;
    }
}

using namespace mye;

// 배회 봇(--bots) — 서버 프로세스 안의 가짜 클라. 진짜 NetClient 로 접속·인증·입력
// 송신까지 실제 클라이언트 경로를 그대로 통과한다(클라이언트 로직과 부하 검증).
// 움직임: 천천히 회전하는 단위 방향 → 원점 주변 원형 배회(봇마다 위상 차이).
struct ServerBot {
    net::NetClient cli;
    std::string user;
    std::string pass;
    double     phase = 0.0;
    uint32_t   seq = 1;
};

int main(int argc, char** argv) {
    Log::Init({});
    struct LogLifetime { ~LogLifetime() { Log::Shutdown(); } } logLifetime;
    uint16_t port = 27015;
    int tickrate = 20;
    long long maxTicks = -1;   // -1 = 무한
    std::string dataDir = "server_data";
    int autosaveSec = 60;
    std::string regUser, regPass;
    bool doRegister = false;
    std::string banUser, unbanUser, banReason;
    std::string charUser, charName;
    bool doMakeChar = false;
    int botCount = 0;
    std::string project, scene;
    bool tickrateExplicit=false;

    auto parseInteger = [](std::string_view text, long long min, long long max, long long& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
               value >= min && value <= max;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" || a == "--tickrate" || a == "--ticks" || a == "--autosave" || a == "--bots") {
            long long value = 0;
            const long long min = a == "--ticks" ? -1 : (a == "--tickrate" ? 1 : 0);
            const long long max = a == "--port" ? 65535 : a == "--tickrate" ? 120 :
                                 a == "--bots" ? net::kMaxSnapshotEntities :
                                 a == "--autosave" ? 86400 : std::numeric_limits<long long>::max();
            if (i + 1 >= argc || !parseInteger(argv[++i], min, max, value)) {
                MYE_LOG_ERROR("Server", "Invalid value for {}", a);
                return 64;
            }
            if (a == "--port") port = static_cast<uint16_t>(value);
            else if (a == "--tickrate") { tickrate = static_cast<int>(value); tickrateExplicit=true; }
            else if (a == "--ticks") maxTicks = value;
            else if (a == "--autosave") autosaveSec = static_cast<int>(value);
            else botCount = static_cast<int>(value);
        }
        else if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--project" && i + 1 < argc) project=argv[++i];
        else if (a == "--scene" && i + 1 < argc) scene=argv[++i];
        else if (a == "--register" && i + 2 < argc) { regUser = argv[++i]; regPass = argv[++i]; doRegister = true; }
        else if (a == "--make-char" && i + 2 < argc) { charUser = argv[++i]; charName = argv[++i]; doMakeChar = true; }
        else if (a == "--ban" && i + 1 < argc) { banUser = argv[++i]; if (i + 1 < argc && argv[i+1][0] != '-') banReason = argv[++i]; }
        else if (a == "--unban" && i + 1 < argc) unbanUser = argv[++i];
        else { MYE_LOG_ERROR("Server", "Unknown or incomplete option: {}", a); return 64; }
    }
    if (dataDir.empty() || static_cast<int>(doRegister) + static_cast<int>(doMakeChar) +
        static_cast<int>(!banUser.empty()) + static_cast<int>(!unbanUser.empty()) > 1) {
        MYE_LOG_ERROR("Server", "Invalid data path or conflicting administration commands");
        return 64;
    }

    // ---- 영속 로드(첫 부팅이면 빈 상태) ----
    persist::PersistenceService persistence;
    if (auto r = persistence.LoadAll(dataDir); !r) {
        MYE_LOG_ERROR("Server", "영속 로드 실패: {}", r.GetError().message);
        return 4;
    }

    // ---- 관리: 계정 등록 후 종료 ----
    if (doRegister) {
        auto reg = persistence.Accounts().Register(regUser, regPass);
        if (!reg) { MYE_LOG_ERROR("Server", "계정 등록 실패: {}", reg.GetError().message); return 3; }
        if (auto s = persistence.SaveAll(dataDir); !s) { MYE_LOG_ERROR("Server", "저장 실패: {}", s.GetError().message); return 4; }
        MYE_LOG_INFO("Server", "계정 '{}' 등록 완료(id={})", regUser, reg.Value());
        return 0;
    }

    // ---- 관리: 캐릭터 생성 후 종료 ----
    if (doMakeChar) {
        const persist::Account* acc = persistence.Accounts().FindByName(charUser);
        if (!acc) { MYE_LOG_ERROR("Server", "계정 '{}' 없음", charUser); return 5; }
        auto c = persistence.Characters().Create(acc->id, charName);
        if (!c) { MYE_LOG_ERROR("Server", "캐릭터 생성 실패: {}", c.GetError().message); return 7; }
        if (auto s = persistence.SaveAll(dataDir); !s) { MYE_LOG_ERROR("Server", "저장 실패: {}", s.GetError().message); return 4; }
        MYE_LOG_INFO("Server", "캐릭터 '{}' 생성 완료(id={}, 계정={})", charName, c.Value(), charUser);
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
    const int64_t configuredTickrate = config.GetInt("tickrate", !project.empty() && !tickrateExplicit ? 60 : tickrate);
    if (configuredTickrate < 1 || configuredTickrate > 120) {
        MYE_LOG_ERROR("Server", "config tickrate must be in [1, 120]");
        return 64;
    }
    tickrate = static_cast<int>(configuredTickrate);
    const float moveSpeed = static_cast<float>(config.GetFloat("move_speed", 6.0));
    if (!std::isfinite(moveSpeed) || moveSpeed < 0 || moveSpeed > 100) {
        MYE_LOG_ERROR("Server", "config move_speed must be in [0, 100]");
        return 64;
    }

    net::NetSubsystem sys;
    if (!sys.ok) { MYE_LOG_ERROR("Server", "Winsock 초기화 실패"); return 1; }

    // 우아한 종료: Ctrl+C·콘솔 닫기 → 루프 탈출 후 세이브.
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // 통합 게임 서버(넷↔게임플레이↔영속). 인증된 계정만 캐릭터 세션을 얻는다.
    std::optional<runtime::OnlineScene3D> online;
    gameserver::NetGameServer server(persistence);
    if (!project.empty()) {
        if (tickrate!=60 || botCount!=0) { MYE_LOG_ERROR("Server","3D project requires 60 Hz; legacy --bots is unavailable"); return 64; }
        auto loaded=runtime::LoadOnlineScene3D(project,scene);
        if (!loaded) { MYE_LOG_ERROR("Server","{}",loaded.GetError().message); return 1; }
        online=std::move(loaded).Value();
        auto configured=server.Configure3D(online->physics,online->settings,online->hash,online->sceneId,online->spawn);
        if (!configured) { MYE_LOG_ERROR("Server","{}",configured.GetError().message); return 1; }
    } else if (!scene.empty()) { MYE_LOG_ERROR("Server","--scene requires --project"); return 64; }
    if (!server.Start(port)) { MYE_LOG_ERROR("Server", "port {} 바인드 실패", port); return 2; }
    server.SetMoveSpeed(moveSpeed);
    if (config.Has("max_violations")) server.Net().SetMaxViolations(static_cast<uint32_t>(config.GetInt("max_violations", 10)));

    // 인증기 재정의: 점검모드면 전원 거부, 아니면 계정 로그인 검증.
    server.Net().SetAuthenticator([&persistence, &config](std::string_view u, std::string_view p) -> uint64_t {
        if (config.MaintenanceMode()) return 0;                  // 점검 중 접속 차단
        persist::LoginResult r = persistence.Accounts().Login(u, p);
        return r.ok ? r.accountId : 0;
    });
    MYE_LOG_INFO("Server", "MyServer 시작 — port {}, tickrate {}Hz, data '{}', 계정 {}명, maint {}",
                 server.Port(), tickrate, dataDir, persistence.Accounts().Count(),
                 config.MaintenanceMode() ? "ON" : "off");

    // ---- 배회 봇(--bots) — 계정/캐릭터 자동 등록 후 루프백 접속(실제 클라 경로) ----
    std::vector<std::unique_ptr<ServerBot>> bots;
    if (botCount > 0) {
        bool touched = false;
        for (int i = 0; i < botCount; ++i) {
            auto bot = std::make_unique<ServerBot>();
            bot->user = "bot" + std::to_string(i + 1);
            bot->pass = "botpass";
            bot->phase = i * 1.7;   // 봇마다 다른 위상 → 경로 분산
            if (!persistence.Accounts().FindByName(bot->user)) {
                if (auto r = persistence.Accounts().Register(bot->user, bot->pass); !r) {
                    MYE_LOG_ERROR("Server", "봇 계정 등록 실패: {}", r.GetError().message);
                    return 3;
                }
                touched = true;
            }
            if (const persist::Account* acc = persistence.Accounts().FindByName(bot->user)) {
                if (persistence.Characters().ListByAccount(acc->id).empty()) {
                    if (auto r = persistence.Characters().Create(acc->id, bot->user); !r) {
                        MYE_LOG_ERROR("Server", "봇 캐릭터 생성 실패: {}", r.GetError().message);
                        return 7;
                    }
                    touched = true;
                }
            }
            if (bot->cli.Open(0)) {
                bot->cli.Connect(net::Endpoint::Loopback(server.Port()), bot->user, bot->pass);
                bots.push_back(std::move(bot));
            }
        }
        if (touched) {
            if (auto r = persistence.SaveAll(dataDir); !r) {
                MYE_LOG_ERROR("Server", "봇 계정 저장 실패: {}", r.GetError().message);
                return 4;
            }
        }
        MYE_LOG_INFO("Server", "봇 {}개 기동(배회 시작)", bots.size());
    }

    const float dt = 1.0f / static_cast<float>(tickrate);
    const auto tickDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / tickrate));
    const long long autosaveTicks = autosaveSec > 0 ? static_cast<long long>(autosaveSec) * tickrate : 0;
    const long long reloadTicks = static_cast<long long>(tickrate) * 5;   // 5초마다 config 핫리로드·메트릭

    // ---- 메트릭/텔레메트리 ----
    liveops::MetricsRegistry metrics;
    const std::string metricsPath = (std::filesystem::path(dataDir) / "metrics.json").string();
    uint64_t lastKicked = 0;
    auto writeMetrics = [&]() {
        const std::string json = metrics.SnapshotJson();
        std::ofstream os(metricsPath, std::ios::binary | std::ios::trunc);
        if (os) os.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!os) MYE_LOG_WARN("Server", "metrics write failed: {}", metricsPath);
    };

    long long tick = 0;
    auto nextTick = std::chrono::steady_clock::now();
    while ((maxTicks < 0 || tick < maxTicks) && !g_stop.load()) {
        const auto start = std::chrono::steady_clock::now();

        server.Tick(dt);   // 수신→세션 diff→권위 시뮬→위치 동기→브로드캐스트
        ++tick;

        // 봇 틱 — 스냅샷 수신 + 배회 입력(회전하는 단위 방향 → 원형 경로, 재접속 재시도).
        for (auto& b : bots) {
            b->cli.Receive();
            if (!b->cli.Connected() && tick % tickrate == 0)
                b->cli.Connect(net::Endpoint::Loopback(server.Port()), b->user, b->pass);
            const double ang = b->phase + static_cast<double>(tick) * dt * 0.35;
            b->cli.SendInput(b->seq++,
                             static_cast<float>(std::cos(ang)),
                             static_cast<float>(std::sin(ang)), dt);
        }

        // 메트릭 수집: 틱 처리 시간(ms)·접속 클라·게임 세션·누적 틱·킥.
        const auto processed = std::chrono::steady_clock::now() - start;
        const double tickMs = std::chrono::duration<double, std::milli>(processed).count();
        metrics.Counter("ticks");
        metrics.Observe("tick_ms", tickMs);
        metrics.Gauge("clients", static_cast<double>(server.Net().ClientCount()));
        metrics.Gauge("players", static_cast<double>(server.PlayerCount()));
        if (server.Net().KickedCount() > lastKicked) { metrics.Counter("kicks", static_cast<int64_t>(server.Net().KickedCount() - lastKicked)); lastKicked = server.Net().KickedCount(); }

        if (tick % tickrate == 0)   // 대략 1초마다
            MYE_LOG_INFO("Server", "tick {} · clients {} · players {}", server.Net().CurrentTick(), server.Net().ClientCount(), server.PlayerCount());

        // config 핫리로드 + 메트릭 스냅샷 기록(운영 관찰성).
        if (tick % reloadTicks == 0) {
            if (std::filesystem::exists(configPath)) {
                if (auto r = config.LoadFromFile(configPath); r) {
                    const float speed = static_cast<float>(config.GetFloat("move_speed", 6.0));
                    if (std::isfinite(speed) && speed >= 0 && speed <= 100) server.SetMoveSpeed(speed);
                    else MYE_LOG_WARN("Server", "Ignored invalid move_speed");
                    if (config.Has("max_violations")) server.Net().SetMaxViolations(static_cast<uint32_t>(config.GetInt("max_violations", 10)));
                } else MYE_LOG_WARN("Server", "config reload failed: {}", r.GetError().message);
            }
            writeMetrics();
        }

        if (autosaveTicks > 0 && tick % autosaveTicks == 0) {
            const int maxBk = static_cast<int>(config.GetInt("max_backups", 10));
            if (auto s = server.Game().Save(dataDir, maxBk); !s) MYE_LOG_WARN("Server", "자동저장 실패: {}", s.GetError().message);
            else MYE_LOG_INFO("Server", "자동저장(백업 회전) 완료 (tick {})", tick);
        }

        // Absolute deadlines recover oversleep instead of compounding Windows timer granularity.
        nextTick += tickDuration;
        std::this_thread::sleep_until(nextTick);
    }

    // ---- 종료 시 저장(백업 회전) + 메트릭 스냅샷 ----
    for (auto& b : bots) b->cli.Disconnect();
    if (auto r = server.Stop(); !r) {
        MYE_LOG_ERROR("Server", "세션 종료 실패: {}", r.GetError().message);
        return 4;
    }
    if (auto s = server.Game().Save(dataDir, static_cast<int>(config.GetInt("max_backups", 10))); !s) {
        MYE_LOG_ERROR("Server", "종료 저장 실패: {}", s.GetError().message);
        return 4;
    }
    writeMetrics();
    MYE_LOG_INFO("Server", "종료{} (총 {} tick, data '{}')", g_stop.load() ? "(우아한 종료)" : "", tick, dataDir);
    return 0;
}
