#include "mye/editor/ProjectAssetOperations.h"
#include "mye/editor/Project.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/asset/AssetMeta.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/asset/AudioImporter.h"
#include "mye/asset/Importer.h"
#include "mye/core/JsonFile.h"

#include <Windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace mye::editor {
namespace {
namespace fs = std::filesystem;
constexpr uintmax_t kMaxSourceBytes = 256u * 1024u * 1024u;

bool IsReservedDeviceName(std::string_view name) {
    auto base = std::string(name.substr(0, name.find('.')));
    while (!base.empty() && base.back() == ' ') base.pop_back();
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
    if (!base.starts_with("com") && !base.starts_with("lpt")) return false;
    const auto digit = std::string_view(base).substr(3);
    return (digit.size() == 1 && digit[0] >= '1' && digit[0] <= '9')
        || digit == "\xC2\xB9" || digit == "\xC2\xB2" || digit == "\xC2\xB3";
}

Expected<void, Error> RejectReparsePoints(const fs::path& path) {
    fs::path part;
    for (const auto& segment : path) {
        part /= segment;
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
            return Error{"Cannot inspect asset path", static_cast<int32_t>(error)};
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return Error{"Asset operations cannot follow links or junctions", 1};
    }
    return {};
}

Expected<fs::path, Error> AssetPath(std::string_view projectRoot, std::string_view relative, bool allowRoot = false) {
    if (projectRoot.empty() || relative.find('\0') != std::string_view::npos || relative.find(':') != std::string_view::npos)
        return Error{"Choose a path inside the project assets folder", 1};
    const auto requested = Utf8Path(relative);
    if ((!allowRoot && requested.empty()) || requested.is_absolute() || requested.has_root_name())
        return Error{"Asset paths must be relative to assets", 1};
    for (const auto& segment : requested) {
        const auto name = Utf8String(segment);
        if (name == ".." || name == "." || (!name.empty() && (name.back() == ' ' || name.back() == '.')))
            return Error{"Asset paths cannot contain traversal or ambiguous names", 1};
        if (IsReservedDeviceName(name)) return Error{"Asset paths cannot use Windows reserved device names", 1};
    }
    std::error_code ec;
    const auto root = fs::absolute(Utf8Path(projectRoot), ec).lexically_normal();
    if (ec) return Error{"Project path is unavailable: " + ec.message(), ec.value()};
    const auto assets = root / "assets";
    const auto path = (assets / requested).lexically_normal();
    auto checked = RejectReparsePoints(path);
    if (!checked) return checked.GetError();
    if (!fs::is_directory(assets, ec) || ec) return Error{"Open a project with an assets folder first", 1};
    return path;
}

Expected<std::vector<std::byte>, Error> ReadSource(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) return Error{"Asset source is not a regular file", 1};
    const auto size = fs::file_size(path, ec);
    if (ec || size == 0 || size > kMaxSourceBytes) return Error{"Asset source must be nonempty and at most 256 MiB", 1};
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return Error{"Cannot read the complete asset source", 1};
    return bytes;
}

Expected<void, Error> WriteNewFile(const fs::path& path, std::span<const std::byte> bytes) {
    const auto close = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(close)> output(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr), close);
    if (output.get() == INVALID_HANDLE_VALUE) { output.release(); return Error{"Cannot create asset file without overwriting: " + Utf8String(path.filename()), static_cast<int32_t>(GetLastError())}; }
    DWORD writtenBytes = 0;
    const bool flushed = WriteFile(output.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &writtenBytes, nullptr)
        && writtenBytes == bytes.size() && FlushFileBuffers(output.get());
    const auto writeError = GetLastError();
    output.reset();
    if (!flushed) {
        std::error_code ec;
        fs::remove(path, ec);
        return Error{"Cannot write the complete asset file" + (ec ? "; incomplete file remains: " + ec.message() : std::string{}), static_cast<int32_t>(writeError)};
    }
    return {};
}

std::string Extension(const fs::path& path) {
    auto extension = Utf8String(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

Expected<void, Error> ValidateAnimationReferences(const fs::path& assets, const asset::AnimationAsset& animation) {
    bool textureFound = false;
    bool nextFound = !animation.nextAnimation.guid.IsValid();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(assets, ec), end; !ec && it != end; it.increment(ec)) {
        auto checked = RejectReparsePoints(it->path()); if (!checked) return checked.GetError();
        if (!it->is_regular_file(ec) || Extension(it->path()) != ".meta") continue;
        auto source = it->path(); source.replace_extension();
        checked = RejectReparsePoints(source); if (!checked) return checked.GetError();
        if (!fs::is_regular_file(source, ec)) continue;
        auto value = ReadJsonFile(it->path()); if (!value) return value.GetError();
        auto meta = asset::AssetMeta::Parse(json::Stringify(value.Value())); if (!meta) return meta.GetError();
        if (meta.Value().guid == animation.sheet.texture.guid && meta.Value().importer == "TextureImporter") textureFound = true;
        if (meta.Value().guid == animation.nextAnimation.guid && meta.Value().importer == "AnimationAsset") nextFound = true;
    }
    if (ec) return Error{"Cannot verify animation references: " + ec.message(), ec.value()};
    if (!textureFound || !nextFound) return Error{"Animation texture or successor GUID is missing from this project; assign existing project assets before importing", 1};
    return {};
}

Expected<std::string, Error> ValidateSource(const fs::path& source, const fs::path& target, const fs::path& assets, const std::vector<std::byte>& bytes) {
    const auto extension = Extension(target);
    if (Extension(source) != extension) return Error{"Keep the source file extension when importing", 1};
    if (extension == ".png") {
        constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
        if (bytes.size() < 24 || !std::equal(signature.begin(), signature.end(), reinterpret_cast<const unsigned char*>(bytes.data())))
            return Error{"The source is not a PNG image", 1};
        const auto dimension = [&](size_t index) {
            const auto* data = reinterpret_cast<const unsigned char*>(bytes.data()) + index;
            return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | data[3];
        };
        const uint32_t width = dimension(16), height = dimension(20);
        if (width == 0 || height == 0 || width > 8192 || height > 8192)
            return Error{"PNG dimensions must be between 1 and 8192 pixels", 1};
        auto decoded = asset::TextureImporter::DecodePng(bytes, false);
        if (!decoded) return decoded.GetError();
        return std::string("TextureImporter");
    }
    if (extension == ".anim") {
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto parsed = json::Parse(text);
        if (!parsed) return parsed.GetError();
        auto animation = asset::AnimationAsset::FromJson(parsed.Value());
        if (!animation) return animation.GetError();
        auto valid = animation.Value().Validate();
        if (!valid) return valid.GetError();
        auto references = ValidateAnimationReferences(assets, animation.Value());
        if (!references) return references.GetError();
        return std::string("AnimationAsset");
    }
    if (extension == ".wav") {
        auto decoded = asset::AudioImporter::DecodeWav(bytes, {});
        if (!decoded) return decoded.GetError();
        return std::string("AudioImporter");
    }
    if (extension == ".lua") {
        const auto close = [](lua_State* state) { lua_close(state); };
        std::unique_ptr<lua_State, decltype(close)> state(luaL_newstate(), close);
        if (!state) return Error{"Cannot create a Lua syntax validator", 1};
        if (luaL_loadbufferx(state.get(), reinterpret_cast<const char*>(bytes.data()), bytes.size(), "asset import", "t") != LUA_OK)
            return Error{lua_tostring(state.get(), -1), 1};
        return std::string("ScriptImporter");
    }
    return Error{"Supported imports: PNG images, .anim animations, Lua scripts and WAV audio", 1};
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool TextReferences(std::string_view source, const std::string& guid, const std::string& vpath) {
    const auto text = LowerAscii(std::string(source));
    // Asset references use VFS paths; map portals and ChangeMap use project-relative paths.
    const auto projectPath = "assets/" + vpath.substr(std::string_view("assets://").size());
    return (!guid.empty() && text.find(guid) != std::string::npos) || text.find(vpath) != std::string::npos
        || text.find(projectPath) != std::string::npos;
}

bool ContainsReference(const json::Value& value, const std::string& guid, const std::string& vpath) {
    if (value.IsString()) {
        const auto& text = value.AsString();
        return TextReferences(text, guid, vpath);
    }
    if (value.IsArray()) for (const auto& item : value.AsArray()) if (ContainsReference(item, guid, vpath)) return true;
    if (value.IsObject()) for (const auto& [key, item] : value.AsObject()) if (ContainsReference(item, guid, vpath)) return true;
    return false;
}
} // namespace

Expected<void, Error> ImportProjectAsset(std::string_view projectRoot, std::string_view sourcePath, std::string_view relative) {
    auto target = AssetPath(projectRoot, relative);
    if (!target) return target.GetError();
    if (sourcePath.empty() || sourcePath.find('\0') != std::string_view::npos) return Error{"Choose an asset source file", 1};
    std::error_code ec;
    const auto source = fs::absolute(Utf8Path(sourcePath), ec).lexically_normal();
    if (ec) return Error{"Asset source path is unavailable: " + ec.message(), ec.value()};
    auto checked = RejectReparsePoints(source);
    if (!checked) return checked.GetError();
    const auto sidecar = Utf8Path(asset::MetaPathFor(Utf8String(target.Value())));
    if (fs::exists(target.Value(), ec) || fs::exists(sidecar, ec) || ec) return Error{"An asset or .meta already exists at the destination; choose another name", 1};
    if (!fs::is_directory(target.Value().parent_path(), ec) || ec) return Error{"Create the destination folder before importing", 1};
    auto bytes = ReadSource(source);
    if (!bytes) return bytes.GetError();
    auto importer = ValidateSource(source, target.Value(), Utf8Path(projectRoot) / "assets", bytes.Value());
    if (!importer) return importer.GetError();
    // Write the validated snapshot, so a source changing during import cannot bypass validation.
    auto copied = WriteNewFile(target.Value(), bytes.Value());
    if (!copied) return copied.GetError();
    const auto meta = asset::AssetMeta::CreateFor(importer.Value(), 1).Stringify();
    auto written = WriteNewFile(sidecar, std::as_bytes(std::span(meta.data(), meta.size())));
    if (!written) {
        fs::remove(target.Value(), ec);
        if (ec) return Error{written.GetError().message + "; cannot remove the incomplete copy: " + ec.message(), ec.value()};
        return written.GetError();
    }
    return {};
}

Expected<void, Error> CreateProjectAssetFolder(std::string_view projectRoot, std::string_view relative) {
    auto path = AssetPath(projectRoot, relative);
    if (!path) return path.GetError();
    std::error_code ec;
    if (!fs::create_directory(path.Value(), ec)) return Error{"Cannot create folder: " + (ec ? ec.message() : "the name already exists"), ec.value()};
    return {};
}

Expected<void, Error> CheckProjectAssetDeletion(const ProjectContext& project, std::string_view relative) {
    if (!project.IsOpen()) return Error{"Open a project first", 1};
    auto target = AssetPath(project.RootDir(), relative);
    if (!target) return target.GetError();
    std::error_code ec;
    if (!fs::is_regular_file(target.Value(), ec) || ec) return Error{"Choose an asset file to delete", 1};
    if (Extension(target.Value()) == ".meta") return Error{"Delete the source asset together with its .meta", 1};
    const std::string vpath = LowerAscii("assets://" + Utf8String(target.Value().lexically_relative(Utf8Path(project.RootDir()) / "assets")));
    std::string guid;
    const auto sidecar = Utf8Path(asset::MetaPathFor(Utf8String(target.Value())));
    if (fs::exists(sidecar, ec)) {
        auto checked = RejectReparsePoints(sidecar); if (!checked) return checked.GetError();
        auto value = ReadJsonFile(sidecar); if (!value) return value.GetError();
        auto meta = asset::AssetMeta::Parse(json::Stringify(value.Value())); if (!meta) return meta.GetError();
        guid = meta.Value().guid.ToString();
    }
    if (ec) return Error{"Cannot inspect asset metadata: " + ec.message(), ec.value()};
    for (const auto* document : project.Documents()) {
        if (!document->Path().empty() && fs::equivalent(Utf8Path(document->Path()), target.Value(), ec))
            return Error{"Close the asset document before deleting it", 1};
        ec.clear();
        auto value = document->GetKind() == Document::Kind::Scene ? SceneSerializer{}.WriteWorld(document->World()) : Expected<json::Value, Error>(document->Animation().ToJson());
        if (!value) return value.GetError();
        if (ContainsReference(value.Value(), guid, vpath)) return Error{"An open document uses this asset; remove its reference first", 1};
    }
    auto metadata = ReadJsonFile(Utf8Path(project.ProjectFilePath()));
    if (!metadata) return metadata.GetError();
    if (const auto* main = metadata.Value().Find("mainScene"); main && main->IsString() && fs::equivalent(Utf8Path(project.RootDir()) / Utf8Path(main->AsString()), target.Value(), ec))
        return Error{"The project main scene cannot be deleted", 1};
    ec.clear();
    const auto root = Utf8Path(project.RootDir());
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        auto checked = RejectReparsePoints(it->path()); if (!checked) return checked.GetError();
        if (it->is_directory(ec) && Utf8String(it->path().filename()).starts_with('.')) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || it->path() == target.Value() || it->path() == sidecar) continue;
        const auto extension = Extension(it->path());
        if (extension != ".scene" && extension != ".anim" && extension != ".prefab" && extension != ".lua") continue;
        auto bytes = ReadSource(it->path()); if (!bytes) return bytes.GetError();
        const std::string text(reinterpret_cast<const char*>(bytes.Value().data()), bytes.Value().size());
        if (TextReferences(text, guid, vpath))
            return Error{"Another asset uses this file: " + Utf8String(it->path().lexically_relative(root)), 1};
    }
    if (ec) return Error{"Cannot check asset references: " + ec.message(), ec.value()};
    return {};
}

Expected<void, Error> RecycleProjectAsset(const ProjectContext& project, std::string_view relative, void* nativeWindow) {
    auto checked = CheckProjectAssetDeletion(project, relative); if (!checked) return checked.GetError();
    auto path = AssetPath(project.RootDir(), relative); if (!path) return path.GetError();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return Error{"Cannot initialize the Recycle Bin operation", static_cast<int32_t>(initialized)};
    struct ComScope { ~ComScope() { CoUninitialize(); } } com;
    IFileOperation* raw = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
    if (FAILED(result)) return Error{"Cannot create the Recycle Bin operation", static_cast<int32_t>(result)};
    const auto release = [](IFileOperation* operation) { operation->Release(); };
    std::unique_ptr<IFileOperation, decltype(release)> operation(raw, release);
    result = operation->SetOperationFlags(FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD | FOF_WANTNUKEWARNING);
    if (SUCCEEDED(result) && nativeWindow) result = operation->SetOwnerWindow(static_cast<HWND>(nativeWindow));
    if (FAILED(result)) return Error{"Cannot configure the Recycle Bin operation", static_cast<int32_t>(result)};
    const auto queue = [&](const fs::path& file) -> HRESULT {
        IShellItem* item = nullptr;
        HRESULT status = SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(status)) return status;
        status = operation->DeleteItem(item, nullptr); item->Release(); return status;
    };
    result = queue(path.Value());
    std::error_code ec;
    const auto sidecar = Utf8Path(asset::MetaPathFor(Utf8String(path.Value())));
    const bool hasMeta = fs::exists(sidecar, ec);
    if (ec) return Error{"Cannot inspect the .meta file: " + ec.message(), ec.value()};
    if (SUCCEEDED(result) && hasMeta) result = queue(sidecar);
    if (FAILED(result)) return Error{"Cannot prepare source and .meta for recycling", static_cast<int32_t>(result)};
    result = operation->PerformOperations();
    BOOL aborted = FALSE;
    const HRESULT status = operation->GetAnyOperationsAborted(&aborted);
    if (FAILED(result) || FAILED(status) || aborted)
        return Error{"Recycling was incomplete or cancelled. Check the folder and Recycle Bin before retrying", static_cast<int32_t>(FAILED(result) ? result : status)};
    return {};
}

Expected<void, Error> RevealProjectAssetFolder(std::string_view projectRoot, std::string_view relative) {
    auto path = AssetPath(projectRoot, relative, true); if (!path) return path.GetError();
    std::error_code ec;
    if (!fs::is_directory(path.Value(), ec) || ec) return Error{"Choose an existing asset folder", 1};
    const auto result = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", path.Value().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) return Error{"Cannot open the asset folder in Explorer", static_cast<int32_t>(result)};
    return {};
}
} // namespace mye::editor
