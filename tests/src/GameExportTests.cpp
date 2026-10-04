#include "TestFramework.h"
#include "mye/runtime/GameExport.h"
#include "mye/core/JsonFile.h"
#include <chrono>
#include <fstream>

using namespace mye;
namespace {
namespace fs = std::filesystem;
void ExportFixtureFile(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << value;
}
void ExportRuntimeFixture(const fs::path& root) {
    for (const auto file : {"MyGame.exe", "fonts/NanumSquareRoundR.ttf", "licenses/NanumSquareRound-LICENSE.txt",
                            "licenses/NOTICE.txt", "LICENSE"}) ExportFixtureFile(root / file, "fixture");
    ExportFixtureFile(root / "release-manifest.json", "{\"version\":\"" MYE_EXPORT_RUNTIME_VERSION
        "\",\"platform\":\"windows-x64\",\"configuration\":\"Release\"}");
}
}

MYE_TEST(GameExportPublishesRuntimeAssetsAndPreservesOccupiedOutput) {
    const auto root = Utf8Path(MYE_TEST_DATA_DIR) / "game-export" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto project = root / "source", distribution = root / "runtime", output = root / "game";
    ExportFixtureFile(project / "project.myeproj", R"({"version":1,"name":"Game","mainScene":"assets/main.scene","credentials":"PRIVATE"})");
    ExportFixtureFile(project / "assets/main.scene", "{}");
    ExportFixtureFile(project / "assets/hero.png", "image");
    ExportFixtureFile(project / "assets/hero.png.meta", "guid");
    ExportFixtureFile(project / "assets/uppercase.PNG", "image");
    ExportFixtureFile(project / "assets/uppercase.PNG.META", "guid");
    ExportFixtureFile(project / "assets/notes.md", "PRIVATE");
    ExportFixtureFile(project / "docs/private.txt", "PRIVATE");
    ExportRuntimeFixture(distribution);
    MYE_EXPECT(runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(output), Utf8String(distribution)));
    MYE_EXPECT(fs::exists(output / "assets/hero.png.meta") && fs::exists(output / "Play.cmd"));
    MYE_EXPECT(fs::exists(output / "assets/uppercase.PNG") && fs::exists(output / "assets/uppercase.PNG.META"));
    MYE_EXPECT(!fs::exists(output / "assets/notes.md") && !fs::exists(output / "docs"));
    auto manifest = ReadJsonFile(output / "game.myeproj");
    MYE_EXPECT(manifest && !manifest.Value().Find("credentials"));
    ExportFixtureFile(output / "sentinel.txt", "kept");
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(output), Utf8String(distribution)));
    MYE_EXPECT(fs::exists(output / "sentinel.txt"));
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(project / "export"), Utf8String(distribution)));
    fs::remove(distribution / "licenses/NOTICE.txt");
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(root / "refused"), Utf8String(distribution)));
    MYE_EXPECT(!fs::exists(root / "refused"));
    ExportRuntimeFixture(distribution);
    ExportFixtureFile(distribution / "release-manifest.json", R"({"version":"0.0.0","platform":"windows-x64","configuration":"Release"})");
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(root / "wrong-version"), Utf8String(distribution)));
    MYE_EXPECT(!fs::exists(root / "wrong-version"));
    const std::string invalidUtf8("\xC3\x28", 2);
    MYE_EXPECT(!runtime::ExportGameProject(invalidUtf8, Utf8String(root / "invalid-path"), Utf8String(distribution)));
    ExportRuntimeFixture(distribution);
    fs::rename(project / "assets", root / "external-assets");
    std::error_code linkError;
    fs::create_directory_symlink(root / "external-assets", project / "assets", linkError);
    if (!linkError) {
        MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(root / "linked"), Utf8String(distribution)));
        MYE_EXPECT(!fs::exists(root / "linked"));
    }
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(project / "project.myeproj"), Utf8String(root / "missing-parent/game"), Utf8String(distribution)));
    fs::remove_all(root);
}

MYE_TEST(GameExportFailedCopyDoesNotPublishOrLeaveStaging) {
    const auto root = Utf8Path(MYE_TEST_DATA_DIR) / "game-export-failure" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ExportFixtureFile(root / "project/project.myeproj", R"({"version":1,"name":"Game","mainScene":"assets/main.scene"})");
    ExportFixtureFile(root / "project/assets/main.scene", "{}");
    ExportFixtureFile(root / "project/assets/.private/key.lua", "PRIVATE");
    ExportRuntimeFixture(root / "runtime");
    MYE_EXPECT(!runtime::ExportGameProject(Utf8String(root / "project/project.myeproj"), Utf8String(root / "game"), Utf8String(root / "runtime")));
    MYE_EXPECT(!fs::exists(root / "game"));
    for (const auto& entry : fs::directory_iterator(root)) MYE_EXPECT(!Utf8String(entry.path().filename()).starts_with("game.tmp-"));
    fs::remove_all(root);
}
