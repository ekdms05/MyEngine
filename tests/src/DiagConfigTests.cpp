// DiagConfigTests.cpp — 로그 파일 싱크·어서트·설정 시스템 검증 (M0-log / M0-config)
#include "TestFramework.h"

#include "mye/core/Assert.h"
#include "mye/core/Config.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace mye;

namespace {

void WriteTempFile(const char* path, const char* text) {
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") == 0 && f) {
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }
}

std::string ReadTempFile(const char* path) {
    std::FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return {};
    std::string out;
    char buf[512];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 로그
// ---------------------------------------------------------------------------
MYE_TEST(FileSinkCreatesAndWritesLogFile) {
    const char* path = "mye_test_filesink.log";
    std::remove(path);
    {
        auto sink = CreateFileSink(path);
        MYE_EXPECT(sink != nullptr);
        sink->Write(LogMessage{LogSeverity::Info, "Test", "hello 파일 싱크", 1'234'000'000ull});
        sink->Flush();
    }   // 소멸 시 닫힘
    const std::string content = ReadTempFile(path);
    MYE_EXPECT(content.find("hello 파일 싱크") != std::string::npos);
    MYE_EXPECT(content.find("[INFO ]") != std::string::npos);
    MYE_EXPECT(content.find("[Test]") != std::string::npos);
    MYE_EXPECT(content.find("log session") != std::string::npos);   // 세션 헤더
    MYE_EXPECT(content.find("[00:00:01.234]") != std::string::npos); // 타임스탬프 포맷
    std::remove(path);
}

MYE_TEST(VerifyUsableAsExpression) {
    bool reached = false;
    if (MYE_VERIFY(2 + 2 == 4)) reached = true;
    MYE_EXPECT(reached);
    MYE_ASSERT(true, "성공 경로는 아무 일도 없어야 한다");
}

// ---------------------------------------------------------------------------
// 설정 시스템
// ---------------------------------------------------------------------------
MYE_TEST(ConfigMissingFileIsSuccess) {
    ConfigSystem config;
    auto r = config.LoadScopeFromFile(ConfigScope::Engine, "definitely_missing_dir/nope.json");
    MYE_EXPECT(r.HasValue());   // 파일 없음 = 성공(빈 스코프)
    MYE_EXPECT(config.GetInt("a.b", 77) == 77);
}

MYE_TEST(ConfigParseErrorReported) {
    const char* path = "mye_test_badcfg.json";
    WriteTempFile(path, "{ \"broken\": ");
    ConfigSystem config;
    auto r = config.LoadScopeFromFile(ConfigScope::Engine, path);
    MYE_EXPECT(!r.HasValue());
    MYE_EXPECT(r.GetError().message.find(path) != std::string::npos);      // 경로 포함
    MYE_EXPECT(r.GetError().message.find("line") != std::string::npos);    // 위치 포함
    std::remove(path);
}

MYE_TEST(ConfigLoadMergeEngineProject) {
    const char* enginePath = "mye_test_engine.json";
    const char* projectPath = "mye_test_project.json";
    WriteTempFile(enginePath, R"({
        "window": { "width": 1280, "height": 720, "vsync": true, "title": "엔진 기본" },
        "time":   { "fixedHz": 60 }
    })");
    WriteTempFile(projectPath, R"({
        "window": { "width": 1920, "title": "프로젝트" },
        "render": { "scale": 2.5, "nested": { "deep": 3 } }
    })");

    ConfigSystem config;
    MYE_EXPECT(config.LoadScopeFromFile(ConfigScope::Engine, enginePath).HasValue());
    MYE_EXPECT(config.LoadScopeFromFile(ConfigScope::Project, projectPath).HasValue());

    // Project가 Engine을 오버라이드
    MYE_EXPECT(config.GetInt("window.width", 0) == 1920);
    MYE_EXPECT(config.GetString("window.title", "") == "프로젝트");
    // Engine 값 유지 (Project에 없음)
    MYE_EXPECT(config.GetInt("window.height", 0) == 720);
    MYE_EXPECT(config.GetBool("window.vsync", false) == true);
    MYE_EXPECT(config.GetInt("time.fixedHz", 0) == 60);
    // 타입 변환: int↔double 관용
    MYE_EXPECT_NEAR(config.GetFloat("render.scale", 0.0), 2.5, 1e-12);
    MYE_EXPECT(config.GetInt("render.scale", 0) == 2);
    MYE_EXPECT_NEAR(config.GetFloat("time.fixedHz", 0.0), 60.0, 1e-12);
    // 중첩 오브젝트는 점 연결 평탄화
    MYE_EXPECT(config.GetInt("render.nested.deep", 0) == 3);
    // 미존재 키·타입 불일치 → fallback
    MYE_EXPECT(config.GetInt("no.such.key", -5) == -5);
    MYE_EXPECT(config.GetBool("window.title", false) == false);

    std::remove(enginePath);
    std::remove(projectPath);
}

MYE_TEST(ConfigSetPublishesChangedEvent) {
    ConfigSystem config;
    EventBus bus;
    config.SetEventBus(&bus);

    std::string receivedKey;
    ConfigScope receivedScope = ConfigScope::Engine;
    ScopedSubscription sub{bus, bus.Subscribe<ConfigChangedEvent>([&](const ConfigChangedEvent& e) {
        receivedKey = std::string(e.key);
        receivedScope = e.scope;
        return false;
    })};

    config.Set("audio.volume", ConfigValue{.value = int64_t{80}}, ConfigScope::Project);
    MYE_EXPECT(receivedKey == "audio.volume");
    MYE_EXPECT(receivedScope == ConfigScope::Project);
    MYE_EXPECT(config.GetInt("audio.volume", 0) == 80);
}

MYE_TEST(ConfigScopePriorityAndSave) {
    const char* projectPath = "mye_test_save_project.json";
    std::remove(projectPath);

    ConfigSystem config;
    // 파일이 아직 없어도 로드는 성공 + Save 대상 경로 기억
    MYE_EXPECT(config.LoadScopeFromFile(ConfigScope::Project, projectPath).HasValue());

    config.Set("game.name", ConfigValue{.value = std::string("모험")}, ConfigScope::Project);
    config.Set("game.level", ConfigValue{.value = int64_t{3}}, ConfigScope::Project);
    config.Set("game.level", ConfigValue{.value = int64_t{9}}, ConfigScope::RuntimeOverlay);

    // RuntimeOverlay > Project
    MYE_EXPECT(config.GetInt("game.level", 0) == 9);

    // Engine은 저장 불가, RuntimeOverlay는 무시(성공)
    MYE_EXPECT(!config.Save(ConfigScope::Engine).HasValue());
    MYE_EXPECT(config.Save(ConfigScope::RuntimeOverlay).HasValue());

    // Project 저장 → 새 인스턴스로 재로드 검증 (오버레이 값 9는 저장되지 않아야 함)
    MYE_EXPECT(config.Save(ConfigScope::Project).HasValue());
    ConfigSystem reloaded;
    MYE_EXPECT(reloaded.LoadScopeFromFile(ConfigScope::Project, projectPath).HasValue());
    MYE_EXPECT(reloaded.GetString("game.name", "") == "모험");
    MYE_EXPECT(reloaded.GetInt("game.level", 0) == 3);

    std::remove(projectPath);
}
