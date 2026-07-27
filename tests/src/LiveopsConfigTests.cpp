// LiveopsConfigTests.cpp — 서버 CVar·피처플래그·점검모드·핫리로드 (docs/mmorpg/09, M11)
#include "TestFramework.h"

#include "mye/liveops/ServerConfig.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace mye;
using namespace mye::liveops;

MYE_TEST(ServerConfigTypedCVarsAndFlags) {
    ServerConfig cfg;

    // 없으면 fallback.
    MYE_EXPECT(cfg.GetFloat("drop_rate_mult", 1.0) == 1.0);
    MYE_EXPECT(!cfg.Has("drop_rate_mult"));

    cfg.SetFloat("drop_rate_mult", 2.5);
    cfg.SetInt("max_players", 500);
    cfg.SetBool("pvp_enabled", true);
    cfg.SetString("motd", "환영합니다");

    MYE_EXPECT(cfg.GetFloat("drop_rate_mult", 1.0) == 2.5);
    MYE_EXPECT(cfg.GetInt("max_players", 0) == 500);
    MYE_EXPECT(cfg.GetBool("pvp_enabled", false));
    MYE_EXPECT(cfg.GetString("motd", "") == "환영합니다");
    MYE_EXPECT(cfg.Has("max_players"));

    // 피처 플래그·점검모드.
    MYE_EXPECT(!cfg.HasFlag("double_xp"));
    cfg.SetFlag("double_xp", true);
    MYE_EXPECT(cfg.HasFlag("double_xp"));
    MYE_EXPECT(!cfg.MaintenanceMode());
    cfg.SetFlag("maintenance", true);
    MYE_EXPECT(cfg.MaintenanceMode());
    cfg.SetFlag("double_xp", false);
    MYE_EXPECT(!cfg.HasFlag("double_xp"));
}

MYE_TEST(ServerConfigPersistRoundtrip) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_serverconfig.json").string();
    std::error_code ec; fs::remove(path, ec);

    {
        ServerConfig cfg;
        cfg.SetFloat("drop_rate_mult", 3.0);
        cfg.SetInt("tickrate", 30);
        cfg.SetBool("pvp_enabled", false);
        cfg.SetString("region", "kr");
        cfg.SetFlag("double_xp", true);
        cfg.SetFlag("maintenance", false);
        MYE_EXPECT(static_cast<bool>(cfg.SaveToFile(path)));
    }
    {
        ServerConfig cfg;
        MYE_EXPECT(static_cast<bool>(cfg.LoadFromFile(path)));
        MYE_EXPECT(cfg.GetFloat("drop_rate_mult", 0.0) == 3.0);
        MYE_EXPECT(cfg.GetInt("tickrate", 0) == 30);
        MYE_EXPECT(!cfg.GetBool("pvp_enabled", true));
        MYE_EXPECT(cfg.GetString("region", "") == "kr");
        MYE_EXPECT(cfg.HasFlag("double_xp"));
        MYE_EXPECT(!cfg.MaintenanceMode());
    }
    fs::remove(path, ec);
}

MYE_TEST(ServerConfigHotReloadReplacesValues) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_serverconfig_hot.json").string();
    std::error_code ec; fs::remove(path, ec);

    ServerConfig cfg;
    cfg.SetFloat("drop_rate_mult", 1.0);

    // 운영자가 파일을 직접 수정(핫리로드 시뮬): 새 값 + 점검모드 on.
    {
        std::ofstream os(path, std::ios::binary | std::ios::trunc);
        os << R"({ "cvars": { "drop_rate_mult": 5.0, "new_cvar": 42 }, "flags": { "maintenance": true } })";
    }
    MYE_EXPECT(static_cast<bool>(cfg.LoadFromFile(path)));
    // 로드가 기존 값을 대체.
    MYE_EXPECT(cfg.GetFloat("drop_rate_mult", 0.0) == 5.0);
    MYE_EXPECT(cfg.GetInt("new_cvar", 0) == 42);
    MYE_EXPECT(cfg.MaintenanceMode());

    // 잘못된 경로는 오류.
    MYE_EXPECT(!cfg.LoadFromFile((fs::temp_directory_path() / "mye_nope_zzz.json").string()));

    fs::remove(path, ec);
}
