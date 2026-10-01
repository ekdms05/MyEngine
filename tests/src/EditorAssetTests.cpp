#include "TestFramework.h"
#include "mye/editor/ProjectAssetOperations.h"
#include "mye/editor/Project.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetMeta.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace mye;
using namespace mye::editor;
namespace {
namespace fs = std::filesystem;
void Write(const fs::path& path, const void* data, size_t size) { std::ofstream output(path, std::ios::binary); output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)); }
void Text(const fs::path& path, std::string_view text) { Write(path, text.data(), text.size()); }
}
MYE_TEST(EditorAssetImportAndDeletionBoundaries) {
    const auto root = Utf8Path(MYE_STARTER_SOURCE_DIR).parent_path().parent_path().parent_path() / "build" / "asset-operation-tests"
        / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code ec; fs::create_directories(root, ec); MYE_EXPECT(!ec);
    ProjectContext project;
    auto created = project.Create("Asset test", Utf8String(root / "project")); MYE_EXPECT(created);
    if (!created) return;
    const std::string projectRoot(project.RootDir()); const auto assets = Utf8Path(projectRoot) / "assets";
    constexpr unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,100,248,15,0,1,5,1,1,39,24,227,98,0,0,0,0,73,69,78,68,174,66,96,130};
    const auto source = root / "image.png"; Write(source, png, sizeof(png));
    MYE_EXPECT(CreateProjectAssetFolder(projectRoot, "images"));
    auto imported = ImportProjectAsset(projectRoot, Utf8String(source), "images/image.png"); MYE_EXPECT(imported);
    if (!imported) return;
    auto metadata = ReadJsonFile(assets / "images/image.png.meta"); MYE_EXPECT(metadata);
    if (!metadata) return;
    auto meta = asset::AssetMeta::Parse(json::Stringify(metadata.Value())); MYE_EXPECT(meta);
    if (!meta) return;
    MYE_EXPECT(meta.Value().guid.IsValid() && meta.Value().importer == "TextureImporter");
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(source), "images/image.png"));
    MYE_EXPECT(WriteJsonFile(assets / "images/occupied.png.meta", metadata.Value()));
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(source), "images/occupied.png"));
    MYE_EXPECT(!fs::exists(assets / "images/occupied.png"));
    fs::remove(assets / "images/occupied.png.meta", ec); MYE_EXPECT(!ec);
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(source), "../escape.png"));
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(source), "images/image.png:alternate"));
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(source), Utf8String(root / "escape.png")));
    MYE_EXPECT(!CreateProjectAssetFolder(projectRoot, "../escape"));
    MYE_EXPECT(!fs::exists(root / "escape.png"));
    auto unchanged = ReadJsonFile(assets / "images/image.png.meta"); MYE_EXPECT(unchanged);
    if (unchanged) MYE_EXPECT(json::Stringify(unchanged.Value()) == json::Stringify(metadata.Value()));
    const auto badPng = root / "bad.png"; Text(badPng, "not an image");
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(badPng), "images/bad.png"));
    MYE_EXPECT(!fs::exists(assets / "images/bad.png") && !fs::exists(assets / "images/bad.png.meta"));
    const auto lua = root / "script.lua"; Text(lua, "error('must not execute during import')\nreturn {}\n");
    MYE_EXPECT(ImportProjectAsset(projectRoot, Utf8String(lua), "script.lua"));
    for (std::string_view name : {"NUL.lua", "con.lua", "AUX.lua", "PRN.lua", "COM1.lua", "COM9.lua", "LPT1.lua", "LPT9.lua", "COM\xC2\xB9.lua"}) {
        auto reserved = ImportProjectAsset(projectRoot, Utf8String(lua), name);
        MYE_EXPECT(!reserved);
        if (!reserved) MYE_EXPECT(reserved.GetError().message.find("reserved device") != std::string::npos);
    }
    MYE_EXPECT(!CreateProjectAssetFolder(projectRoot, "images/NUL.folder"));
    const auto badLua = root / "broken.lua"; Text(badLua, "function ???");
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(badLua), "broken.lua"));
    MYE_EXPECT(!fs::exists(assets / "broken.lua"));
    constexpr unsigned char wav[] = {82,73,70,70,38,0,0,0,87,65,86,69,102,109,116,32,16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,100,97,116,97,2,0,0,0,0,0};
    const auto sound = root / "sound.wav"; Write(sound, wav, sizeof(wav));
    MYE_EXPECT(ImportProjectAsset(projectRoot, Utf8String(sound), "sound.wav"));
    asset::AnimationAsset animation;
    animation.sheet.texture.guid = meta.Value().guid; animation.imageSize = {1,1};
    asset::SpriteFrame frame; frame.rect = {0,0,1,1}; animation.sheet.frames.push_back(frame);
    animation.clip.name = "idle"; animation.clip.frameIndices = {0}; animation.clip.frameDurations = {0.1f};
    const auto animationSource = root / "idle.anim";
    MYE_EXPECT(WriteJsonFile(animationSource, animation.ToJson()));
    MYE_EXPECT(ImportProjectAsset(projectRoot, Utf8String(animationSource), "idle.anim"));
    auto broken = animation.ToJson().AsObject(); broken["width"] = json::Value(int64_t{0});
    MYE_EXPECT(WriteJsonFile(root / "broken.anim", json::Value(std::move(broken))));
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(root / "broken.anim"), "broken.anim"));
    auto missingTexture = animation.ToJson().AsObject(); missingTexture["texture"] = json::Value(asset::AssetGuid::Generate().ToString());
    MYE_EXPECT(WriteJsonFile(root / "missing.anim", json::Value(std::move(missingTexture))));
    MYE_EXPECT(!ImportProjectAsset(projectRoot, Utf8String(root / "missing.anim"), "missing.anim"));
    MYE_EXPECT(!fs::exists(assets / "missing.anim"));
    // Imported GUID metadata is accepted by the same index that viewport drag/drop consumes.
    asset::VirtualFileSystem vfs; vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(assets)), 0);
    asset::AssetManager manager(vfs, nullptr); manager.RegisterImporter(std::make_unique<asset::TextureImporter>());
    asset::AssetDatabase database(manager, nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(assets)));
    MYE_EXPECT(database.GuidFromPath("assets://images/image.png") == meta.Value().guid);
    // Saved animation reference blocks deletion of its PNG even when not opened.
    MYE_EXPECT(!CheckProjectAssetDeletion(project, "images/image.png"));
    MYE_EXPECT(CheckProjectAssetDeletion(project, "sound.wav"));
    auto opened = project.OpenAnimation(Utf8String(assets / "idle.anim")); MYE_EXPECT(opened);
    MYE_EXPECT(!CheckProjectAssetDeletion(project, "idle.anim"));
    if (opened) project.CloseDocument(opened.Value()->Id());
    MYE_EXPECT(CheckProjectAssetDeletion(project, "idle.anim"));
    auto* scene = project.Active(); MYE_EXPECT(scene);
    if (scene) {
        auto entity = scene->World().Create(); auto& sprite = scene->World().Add<scene::SpriteRenderer>(entity);
        sprite.sprite.guid = meta.Value().guid;
        // References in live unsaved scene data are checked before disk-only dependencies.
        auto inUse = CheckProjectAssetDeletion(project, "images/image.png"); MYE_EXPECT(!inUse);
        if (!inUse) MYE_EXPECT(inUse.GetError().message.find("open document") != std::string::npos);
        MYE_EXPECT(SceneSerializer{}.SaveToFile(scene->World(), Utf8String(root / "project/outside-assets.scene")));
    }
    Document destination({99}, Document::Kind::Scene, "");
    const auto spawn = destination.World().Create();
    destination.World().Add<scene::ObjectName>(spawn).value = "Spawn";
    destination.World().Add<scene::LocalTransform>(spawn);
    MYE_EXPECT(SceneSerializer{}.SaveToFile(destination.World(), Utf8String(assets / "scenes/exit.scene")));
    MYE_EXPECT(CheckProjectAssetDeletion(project, "scenes/exit.scene"));
    Document portalSource({100}, Document::Kind::Scene, "");
    const auto door = portalSource.World().Create();
    portalSource.World().Add<scene::LocalTransform>(door);
    portalSource.World().Add<runtime::InteractionTarget>(door);
    auto& portal = portalSource.World().Add<runtime::ScenePortal>(door);
    portal.scenePath = "assets/scenes/exit.scene"; portal.spawnName = "Spawn";
    // Portals store project-relative scene paths, and valid scene files can live outside assets.
    MYE_EXPECT(SceneSerializer{}.SaveToFile(portalSource.World(), Utf8String(root / "project/portal-reference.scene")));
    auto portalDependency = CheckProjectAssetDeletion(project, "scenes/exit.scene");
    MYE_EXPECT(!portalDependency);
    if (!portalDependency) MYE_EXPECT(portalDependency.GetError().message.find("portal-reference.scene") != std::string::npos);
    MYE_EXPECT(!CheckProjectAssetDeletion(project, "../image.png"));
    MYE_EXPECT(!CheckProjectAssetDeletion(project, "images/image.png.meta"));
    MYE_EXPECT(!CheckProjectAssetDeletion(project, "scenes/main.scene"));
}
