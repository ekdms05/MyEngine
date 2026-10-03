#include "TestFramework.h"
#include "mye/editor/ProjectAssetOperations.h"
#include "mye/editor/Project.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AnimationStateAsset.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetMeta.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/ui/UiDocument.h"
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
MYE_TEST(EditorUiImportPreservesGuidAndProtectsUnopenedTextureReferences) {
    const auto root = Utf8Path(MYE_TEST_DATA_DIR) / "ui-import" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    ProjectContext project;
    MYE_EXPECT(project.Create("UI import",Utf8String(root / "project")));
    const std::string projectRoot(project.RootDir());
    ui::UiDocument document; document.root.typeName = "Image"; document.root.name = "icon";
    const auto image = asset::AssetGuid::Generate();
    document.root.properties = {{"texture",image.ToString()},{"source","0,0,16,16"}};
    auto encoded = ui::SaveDocumentJson(document); MYE_EXPECT(encoded); if (!encoded) return;
    const auto source = root / "hud.ui"; Text(source,encoded.Value());
    MYE_EXPECT(ImportProjectAsset(projectRoot,Utf8String(source),"hud.ui"));
    const auto metadata = root / "project/assets/hud.ui.meta";
    auto original = ReadJsonFile(metadata); MYE_EXPECT(original); if (!original) return;
    auto meta = asset::AssetMeta::Parse(json::Stringify(original.Value())); MYE_EXPECT(meta);
    MYE_EXPECT(meta && meta.Value().importer == "UiDocument");
    MYE_EXPECT(!ImportProjectAsset(projectRoot,Utf8String(source),"hud.ui"));
    auto after = ReadJsonFile(metadata); MYE_EXPECT(after);
    MYE_EXPECT(after && json::Stringify(after.Value()) == json::Stringify(original.Value()));
    Text(root / "broken.ui","{}");
    MYE_EXPECT(!ImportProjectAsset(projectRoot,Utf8String(root / "broken.ui"),"broken.ui"));
    MYE_EXPECT(!fs::exists(root / "project/assets/broken.ui") && !fs::exists(root / "project/assets/broken.ui.meta"));
    // A .ui not opened by the editor still protects its PNG dependency.
    Text(root / "project/assets/icon.png","fixture only; deletion check does not decode");
    auto imageMeta = asset::AssetMeta::CreateFor("TextureImporter",1); imageMeta.guid = image;
    Text(root / "project/assets/icon.png.meta",imageMeta.Stringify());
    MYE_EXPECT(!CheckProjectAssetDeletion(project,"icon.png"));
    MYE_EXPECT(CheckProjectAssetDeletion(project,"hud.ui"));
    // Pending UI edits protect references even before a document has a file name.
    auto newUi = project.NewUi(); MYE_EXPECT(newUi);
    if (newUi) {
        Text(root / "project/assets/draft.png", "deletion fixture");
        auto draftMeta = asset::AssetMeta::CreateFor("TextureImporter",1);
        Text(root / "project/assets/draft.png.meta", draftMeta.Stringify());
        auto draft = newUi.Value()->Ui(); draft.root.typeName = "Image";
        draft.root.properties = {{"texture",draftMeta.guid.ToString()}};
        newUi.Value()->StageUi(draft);
        MYE_EXPECT(!CheckProjectAssetDeletion(project,"draft.png"));
        newUi.Value()->DiscardUiDraft();
        MYE_EXPECT(CheckProjectAssetDeletion(project,"draft.png"));
        project.CloseDocument(newUi.Value()->Id());
    }
    if (meta && project.Active()) {
        const auto entity = project.Active()->World().Create();
        project.Active()->World().Add<runtime::GameUi>(entity).document = {meta.Value().guid,0};
        MYE_EXPECT(!CheckProjectAssetDeletion(project,"hud.ui"));
    }
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


MYE_TEST(AnimationStateGuidFilesAndDeletionBoundary) {
    const auto root = Utf8Path(MYE_STARTER_SOURCE_DIR).parent_path().parent_path().parent_path() / "build" / "animation-state-tests"
        / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ProjectContext project; auto created = project.Create("State test", Utf8String(root)); MYE_EXPECT(created); if (!created) return;
    const auto assets = root / "assets";
    asset::AnimationAsset clip; clip.imageSize = {16, 24}; clip.sheet.texture.guid = asset::AssetGuid::Generate();
    asset::SpriteFrame frame; frame.rect = {0, 0, 16, 24}; frame.pivot = {8, 24}; frame.pivotInPixels = true;
    clip.sheet.frames = {frame}; clip.clip.name = "idle"; clip.clip.frameIndices = {0}; clip.clip.frameDurations = {.1f};
    MYE_EXPECT(WriteJsonFile(assets / "idle.anim", clip.ToJson()));
    asset::VirtualFileSystem files; files.Mount("assets", std::make_unique<asset::LooseFileSystem>(Utf8String(assets)), 0);
    asset::AssetManager manager(files, nullptr); asset::AssetDatabase database(manager, nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(assets)));
    const auto clipGuid = database.GuidFromPath("assets://idle.anim"); MYE_EXPECT(clipGuid.IsValid());
    asset::AnimationStateAsset graph; graph.name = "player"; graph.states = {{"idle", {clipGuid}}};
    auto encoded = graph.ToJson(); MYE_EXPECT(encoded); if (!encoded) return;
    MYE_EXPECT(WriteJsonFile(assets / "player.animstate", encoded.Value()));
    MYE_EXPECT(database.ScanDirectory(Utf8String(assets)));
    const auto guid = database.GuidFromPath("assets://player.animstate"); MYE_EXPECT(guid.IsValid());
    auto metadata = ReadJsonFile(assets / "player.animstate.meta"); MYE_EXPECT(metadata); if (!metadata) return;
    auto loaded = asset::AnimationStateAsset::Load(guid, database, files); MYE_EXPECT(loaded);
    if (loaded) MYE_EXPECT(loaded.Value().states[0].animation.guid == clipGuid);
    MYE_EXPECT(database.ScanDirectory(Utf8String(assets)));
    MYE_EXPECT(database.GuidFromPath("assets://player.animstate") == guid);
    auto sameMetadata = ReadJsonFile(assets / "player.animstate.meta"); MYE_EXPECT(sameMetadata);
    if (sameMetadata) MYE_EXPECT(json::Stringify(metadata.Value()) == json::Stringify(sameMetadata.Value()));
    auto deletion = CheckProjectAssetDeletion(project, "idle.anim"); MYE_EXPECT(!deletion);
    if (!deletion) MYE_EXPECT(deletion.GetError().message.find("player.animstate") != std::string::npos);
    MYE_EXPECT(!asset::AnimationStateAsset::Load(clipGuid, database, files));
    graph.states[0].animation.guid = asset::AssetGuid::Generate(); auto missing = graph.ToJson(); MYE_EXPECT(missing); if (!missing) return;
    MYE_EXPECT(WriteJsonFile(assets / "player.animstate", missing.Value()));
    MYE_EXPECT(!asset::AnimationStateAsset::Load(guid, database, files));
    graph.states[0].animation.guid = guid; auto wrongType = graph.ToJson(); MYE_EXPECT(wrongType); if (!wrongType) return;
    MYE_EXPECT(WriteJsonFile(assets / "player.animstate", wrongType.Value()));
    MYE_EXPECT(!asset::AnimationStateAsset::Load(guid, database, files));
    MYE_EXPECT(WriteJsonFile(assets / "player.animstate", encoded.Value()));
    Text(assets / "idle.anim", "{broken"); MYE_EXPECT(!asset::AnimationStateAsset::Load(guid, database, files));
    std::error_code ec; fs::remove(assets / "idle.anim", ec); MYE_EXPECT(!ec);
    MYE_EXPECT(!asset::AnimationStateAsset::Load(guid, database, files));
    MYE_EXPECT(fs::exists(assets / "player.animstate") && fs::exists(assets / "player.animstate.meta"));
}
