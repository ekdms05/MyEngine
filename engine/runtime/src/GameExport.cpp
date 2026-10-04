#include "mye/runtime/GameExport.h"
#include "mye/core/JsonFile.h"
#include "mye/runtime/GameInput.h"

#include <chrono>
#include <fstream>
#include <set>

namespace mye::runtime {
namespace {
namespace fs = std::filesystem;

bool Within(const fs::path& path, const fs::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

bool RuntimeAsset(const fs::path& path) {
    static const std::set<std::string> extensions{
        ".scene", ".anim", ".animstate", ".ui", ".png", ".bmp", ".tga", ".jpg", ".jpeg",
        ".glb", ".gltf", ".lua", ".wav", ".ogg", ".mp3", ".flac"};
    const auto lower = [](std::string text) {
        for (char& value : text) if (value >= 'A' && value <= 'Z') value = char(value + ('a' - 'A'));
        return text;
    };
    auto extension = lower(Utf8String(path.extension()));
    if (extension == ".meta") extension = lower(Utf8String(path.stem().extension()));
    return extensions.contains(extension);
}

// Only the directory reserved by this invocation is owned by this guard.
struct ExportStage {
    fs::path path;
    ~ExportStage() { if (!path.empty()) { std::error_code ec; fs::remove_all(path, ec); } }
};

void CopyFile(const fs::path& source, const fs::path& destination, const fs::path& scope) {
    if (!Within(fs::canonical(source), scope) || fs::is_symlink(fs::symlink_status(source)) ||
        !fs::is_regular_file(source))
        throw fs::filesystem_error("Export source must be a regular file within its scope", source,
                                  std::make_error_code(std::errc::invalid_argument));
    fs::create_directories(destination.parent_path());
    fs::copy_file(source, destination); // Never overwrite a reserved destination.
}

void CopyTree(const fs::path& source, const fs::path& destination, bool assets, const fs::path& owner) {
    const auto scope = fs::canonical(source);
    if (!Within(scope, owner) || fs::is_symlink(fs::symlink_status(source)))
        throw fs::filesystem_error("Export refuses linked source roots", source,
                                  std::make_error_code(std::errc::invalid_argument));
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (!Within(fs::canonical(entry.path()), scope) || fs::is_symlink(entry.symlink_status()))
            throw fs::filesystem_error("Export refuses linked files and directories", entry.path(),
                                      std::make_error_code(std::errc::invalid_argument));
        const auto relative = entry.path().lexically_relative(source);
        for (const auto& part : relative)
            if (Utf8String(part).starts_with("."))
                throw fs::filesystem_error("Private directories are not runtime assets", entry.path(),
                                          std::make_error_code(std::errc::invalid_argument));
        if (entry.is_regular_file() && (!assets || RuntimeAsset(relative)))
            CopyFile(entry.path(), destination / relative, scope);
    }
}
} // namespace

Expected<void, Error> ExportGameProject(std::string_view projectFile,
    std::string_view destination, std::string_view runtimeDirectory) {
    try {
        if (projectFile.empty() || destination.empty() || runtimeDirectory.empty() ||
            projectFile.find('\0') != std::string_view::npos || destination.find('\0') != std::string_view::npos ||
            runtimeDirectory.find('\0') != std::string_view::npos)
            return Error{"Export requires project, new output directory and runtime distribution", 1};
        const auto project = fs::canonical(Utf8Path(projectFile));
        if (project.extension() != ".myeproj") return Error{"Export requires a .myeproj file", 1};
        const auto root = project.parent_path();
        const auto runtime = fs::canonical(Utf8Path(runtimeDirectory));
        auto release = ReadJsonFile(runtime / "release-manifest.json");
        if (!release) return release.GetError();
        const auto* runtimeVersion = release.Value().Find("version");
        const auto* platform = release.Value().Find("platform");
        const auto* configuration = release.Value().Find("configuration");
        if (!runtimeVersion || !runtimeVersion->IsString() || runtimeVersion->AsString() != MYE_EXPORT_RUNTIME_VERSION ||
            !platform || !platform->IsString() || platform->AsString() != "windows-x64" ||
            !configuration || !configuration->IsString() || configuration->AsString() != "Release")
            return Error{"Export requires a matching Windows x64 Release distribution", 1};
        const auto output = fs::weakly_canonical(fs::absolute(Utf8Path(destination)));
        if (fs::exists(output) || Within(output, root) || Within(root, output) ||
            Within(output, runtime) || Within(runtime, output))
            return Error{"Export output must be new and separate from the project and runtime", 1};
        if (!fs::is_directory(output.parent_path())) return Error{"Export output parent must exist", 1};
        auto manifest = ReadJsonFile(project);
        if (!manifest) return manifest.GetError();
        const auto* version = manifest.Value().Find("version");
        const auto* name = manifest.Value().Find("name");
        const auto* mainScene = manifest.Value().Find("mainScene");
        if (!manifest.Value().IsObject() || !version || !version->IsInteger() || version->AsInt() != 1 ||
            !name || !name->IsString() || !mainScene || !mainScene->IsString())
            return Error{"Export requires version 1, name and mainScene", 1};
        const auto scene = Utf8Path(mainScene->AsString());
        if (scene.has_root_path() || scene.extension() != ".scene" ||
            !Within(fs::canonical(root / scene), fs::canonical(root / "assets")))
            return Error{"Export mainScene must exist inside project assets", 1};
        auto input = LoadGameInputMap(manifest.Value().Find("inputMap"));
        if (!input) return input.GetError();
        // Explicit runtime fields prevent editor metadata or credentials from being published.
        json::Value::Object selected;
        for (const auto key : {"version", "name", "mainScene", "inputMap", "onlineCombat"})
            if (const auto* value = manifest.Value().Find(key)) selected[key] = *value;
        for (const auto required : {"MyGame.exe", "fonts/NanumSquareRoundR.ttf", "licenses/NanumSquareRound-LICENSE.txt",
                                   "licenses/NOTICE.txt", "LICENSE"})
            if (!fs::is_regular_file(runtime / required))
                return Error{"Runtime distribution is missing " + std::string(required), 1};

        const auto stagePath = output.parent_path() / (output.filename().wstring() + L".tmp-" +
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(stagePath)) return Error{"Cannot reserve export staging directory", 1};
        ExportStage stage{stagePath};
        CopyFile(runtime / "MyGame.exe", stage.path / "MyGame.exe", runtime);
        CopyFile(runtime / "fonts/NanumSquareRoundR.ttf", stage.path / "fonts/NanumSquareRoundR.ttf", runtime);
        CopyFile(runtime / "LICENSE", stage.path / "LICENSE", runtime);
        CopyTree(runtime / "licenses", stage.path / "licenses", false, runtime);
        CopyTree(root / "assets", stage.path / "assets", true, root);
        if (fs::is_directory(root / "licenses")) CopyTree(root / "licenses", stage.path / "game-licenses", false, root);
        if (fs::is_regular_file(root / "LICENSE")) CopyFile(root / "LICENSE", stage.path / "game-licenses/LICENSE", root);
        if (auto saved = WriteJsonFile(stage.path / "game.myeproj", json::Value(std::move(selected))); !saved) return saved.GetError();
        std::ofstream launcher(stage.path / "Play.cmd", std::ios::binary);
        launcher << "@echo off\r\n\"%~dp0MyGame.exe\" --project \"%~dp0game.myeproj\" %*\r\n";
        launcher.close();
        if (!launcher) return Error{"Could not write game launcher", 1};
        std::ofstream readme(stage.path / "README.txt", std::ios::binary);
        readme << "Run Play.cmd. Requires Windows 10/11 x64, DirectX 11 and Microsoft Visual C++ v14 x64 Redistributable.\r\n"
               << "Runtime download: https://aka.ms/vc14/vc_redist.x64.exe\r\n"
               << "Runtime version: " MYE_EXPORT_RUNTIME_VERSION "\r\n"
               << "Online servers and private credentials are supplied separately. License notices: licenses/ and game-licenses/.\r\n";
        readme.close();
        if (!readme) return Error{"Could not write game requirements", 1};
        if (fs::exists(output)) return Error{"Export destination appeared during preparation", 1};
        fs::rename(stage.path, output);
        stage.path.clear();
        return {};
    } catch (const std::system_error& error) {
        return Error{"Game export failed: " + std::string(error.what()), error.code().value()};
    }
}
} // namespace mye::runtime
