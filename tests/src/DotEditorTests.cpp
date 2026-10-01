#include "TestFramework.h"
#include "mye/editor/DotEditing.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/Project.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/scene/Transform.h"
#include "mye/phys/Collision.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/ecs/World.h"
#include "mye/core/JsonFile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

using namespace mye;
using namespace mye::editor;
namespace {
std::filesystem::path FreshDotRoot() {
    return Utf8Path(MYE_TEST_DATA_DIR) / "dot-editor" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
DotDocument RigSample() {
    DotDocument data;
    auto& image = data.frames[0].image;
    image.width = image.height = 8; image.pixels.assign(64, 0);
    image.pixels[3 * 8 + 2] = 0xff00ff00;
    image.pixels[3 * 8 + 4] = 0xffff0000;
    data.frames.resize(3, data.frames.front()); data.motions[0].last = 2;
    MYE_EXPECT(AddDotBone(data, {2,3,1,1}, -1));
    MYE_EXPECT(AddDotBone(data, {4,3,1,1}, 0));
    return data;
}
void Bind(EditorContext& context, Document& document, PlayModeController& play) {
    play.SetEditWorld(&document.World()); context.playMode = &play;
    context.activeDocument = &document; context.commands = &document.Commands();
    document.Commands().SetContext(&context);
}
}

MYE_TEST(DotZoomPaintAndRigidHierarchy) {
    DotCanvasView view{{10,20},4}; const Vec2 anchor{31,49};
    const auto before = view.PixelAt(anchor); view.ZoomAt(anchor,32);
    MYE_EXPECT_NEAR(view.PixelAt(anchor).x,before.x,.0001f);
    MYE_EXPECT_NEAR(view.PixelAt(anchor).y,before.y,.0001f);
    view.Fit({900,400},{128,512}); MYE_EXPECT_NEAR(view.scale,.78125f,.0001f);
    MYE_EXPECT_NEAR(view.origin.x,400.f,.0001f);
    view.Fit({1000,800},{128,512}); MYE_EXPECT(view.scale == 1 && view.origin.y == 144);
    DotImage image; image.width = image.height = 8; image.pixels.assign(64,0);
    PaintDotLine(image,{0,0},{7,7},0xffffffff);
    FillDotRegion(image,{1,0},0xff0000ff);
    for (int i = 0; i < 8; ++i) MYE_EXPECT(image.pixels[i*9] == 0xffffffff);
    MYE_EXPECT(image.pixels[1] == 0xff0000ff && image.pixels[8] == 0);
    MYE_EXPECT(ResampleDotImage(image,16,16).pixels[2] == 0xff0000ff);
    auto data = RigSample();
    MYE_EXPECT(RenderDotFrame(data,0,0,true).pixels == data.frames[0].image.pixels);
    auto rest = data.bones[0].rest, moved = rest; moved.position.x += 2;
    data.motions[0].keys = {{0,0,rest},{2,0,moved}};
    const auto rendered = RenderDotFrame(data,0,1);
    MYE_EXPECT(rendered.pixels[3*8+3] == 0xff00ff00);
    MYE_EXPECT(rendered.pixels[3*8+5] == 0xffff0000 && rendered.pixels[3*8+2] == 0);
    data.motions[0].keys[0].pose.degrees = 170; data.motions[0].keys[1].pose.degrees = -170;
    MYE_EXPECT_NEAR(std::abs(EvaluateDotPose(data,0,0,1).degrees),180.f,.001f);
    data.bones[0].parent = 1; MYE_EXPECT(!data.Validate());
}

MYE_TEST(DotAtomicSourceSaveUndoAndRejectedPaths) {
    const auto root = FreshDotRoot(); ProjectContext project;
    MYE_EXPECT(project.Create("도트 저장",Utf8String(root))); if (!project.IsOpen()) return;
    auto* document = project.NewDot(); EditorContext context; PlayModeController play; Bind(context,*document,play);
    auto data = RigSample(); data.reference = std::make_shared<const DotImage>(data.frames[0].image);
    document->Commands().Push(std::make_unique<DotEditCommand>(document->Dot(),document->Dot(),data,"rig"));
    MYE_EXPECT(project.SaveDot(document->Id(),"assets/sprites/캐릭터.dot"));
    MYE_EXPECT(!document->IsDirty());
    const auto path = Utf8Path(document->Path()); const auto saved = ReadJsonFile(path);
    MYE_EXPECT(saved); if (!saved) return;
    const auto parsed = DotDocument::FromJson(saved.Value());
    MYE_EXPECT(parsed && parsed.Value().bones.size() == 2 && parsed.Value().reference);
    MYE_EXPECT(!project.SaveDot(document->Id(),"../outside.dot"));
    MYE_EXPECT(!project.SaveDot(document->Id(),"source.dot"));
    auto invalid = data.ToJson().AsObject(); invalid["version"] = json::Value(int64_t(2));
    MYE_EXPECT(!DotDocument::FromJson(json::Value(std::move(invalid))));
    const auto staging = Utf8Path(Utf8String(path)+".tmp");
    std::ofstream(staging,std::ios::binary) << "occupied";
    auto changed = data; changed.frames[0].image.pixels[0] = 0xffffffff;
    document->Commands().Push(std::make_unique<DotEditCommand>(document->Dot(),data,changed,"paint"));
    MYE_EXPECT(!project.SaveDot(document->Id(),Utf8String(path)) && document->IsDirty());
    const auto unchanged = ReadJsonFile(path);
    MYE_EXPECT(unchanged && json::Stringify(unchanged.Value()) == json::Stringify(saved.Value()));
    document->Commands().Undo(); MYE_EXPECT(!document->IsDirty());
    document->Commands().Undo(); MYE_EXPECT(document->Dot().bones.empty());
    document->Commands().Redo(); MYE_EXPECT(document->Dot().bones.size() == 2);
}

MYE_TEST(DotFrameRemovalExportAndSuccessor) {
    auto data = RigSample();
    data.motions[0].keys = {{0,0,data.bones[0].rest},{1,0,data.bones[0].rest},{2,1,data.bones[1].rest}};
    DotMotion end; end.name = "end"; end.first = end.last = 2;
    data.motions.push_back(end); data.motions[0].next = "end";
    MYE_EXPECT(RemoveDotFrame(data,1));
    MYE_EXPECT(data.motions[0].last == 1 && data.motions[1].first == 1 && data.motions[0].keys.size() == 2);
    MYE_EXPECT(data.Validate() && !DotPlaybackClip(data,0).loop);
    const auto root = FreshDotRoot(); std::filesystem::create_directories(root/"assets");
    const auto exported = ExportDotMotions(data,root); MYE_EXPECT(exported); if (!exported) return;
    const auto json = ReadJsonFile(exported.Value().directory/"motion-0.anim"); MYE_EXPECT(json); if (!json) return;
    const auto animation = asset::AnimationAsset::FromJson(json.Value());
    MYE_EXPECT(animation && animation.Value().nextAnimation.guid == exported.Value().animations[1].guid);
    const auto png = LoadDotImage(exported.Value().directory/"motion-0.png");
    MYE_EXPECT(png && png.Value().pixels[3*png.Value().width+2] == 0xff00ff00);
    const auto again = ExportDotMotions(data,root);
    MYE_EXPECT(again && again.Value().directory != exported.Value().directory);
    MYE_EXPECT(RemoveDotFrame(data,1)); MYE_EXPECT(!RemoveDotFrame(data,0));
}

MYE_TEST(CharacterMovementSetupUndoAndMotionState) {
    Document document({1},Document::Kind::Scene,""); EditorContext context; PlayModeController play; Bind(context,document,play);
    auto& world = document.World(); const auto player = world.Create();
    world.Add<scene::LocalTransform>(player); world.Add<scene::WorldTransform>(player);
    MYE_EXPECT(SetupCharacterMovement(context,player));
    MYE_EXPECT(world.TryGet<runtime::CharacterController2D>(player) && world.TryGet<phys::KinematicBody2D>(player));
    const auto position = document.Commands().Position();
    MYE_EXPECT(SetupCharacterMovement(context,player) && position == document.Commands().Position());
    document.Commands().Undo(); MYE_EXPECT(!world.TryGet<runtime::CharacterController2D>(player));
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(player)); document.Commands().Redo();
    const asset::AssetRef idle{asset::AssetGuid::Generate()}, walk{asset::AssetGuid::Generate()}, next{asset::AssetGuid::Generate()};
    MYE_EXPECT(AssignCharacterMotion(context,player,idle,false));
    MYE_EXPECT(AssignCharacterMotion(context,player,walk,true));
    runtime::ObjectSystem system(world); MYE_EXPECT(system.Initialize());
    system.Tick(.02f,{},false); auto* animator = world.TryGet<anim::SpriteAnimator>(player);
    MYE_EXPECT(animator); if (!animator) return;
    MYE_EXPECT(animator->animation.guid == idle.guid); animator->animation = next;
    system.Tick(.02f,{},false); MYE_EXPECT(animator->animation.guid == next.guid);
    system.Tick(.02f,{1,0},false); MYE_EXPECT(animator->animation.guid == walk.guid);
    const auto second = world.Create(); world.Add<scene::LocalTransform>(second); world.Add<scene::WorldTransform>(second);
    MYE_EXPECT(!SetupCharacterMovement(context,second) && !world.TryGet<phys::KinematicBody2D>(second));
    MYE_EXPECT(play.Play());
    MYE_EXPECT(!SetupCharacterMovement(context,player) && !AssignCharacterMotion(context,player,idle,false));
    play.Stop();
}

MYE_TEST(DotStarterSourceAndPlayableExport) {
    const auto image = LoadDotImage(Utf8Path(MYE_STARTER_SOURCE_DIR)/"assets/characters/novice.png");
    MYE_EXPECT(image); if (!image) return;
    DotImage crop; crop.width = 203; crop.height = 476; crop.pixels.resize(203*476);
    for (int y = 0; y < crop.height; ++y)
        std::copy_n(image.Value().pixels.begin()+size_t(y+100)*image.Value().width+43,203,crop.pixels.begin()+size_t(y)*203);
    const auto art = ResampleDotImage(crop,112,264); DotDocument data;
    auto& canvas = data.frames[0].image; canvas.width = 128; canvas.height = 288; canvas.pixels.assign(128*288,0);
    for (int y = 0; y < art.height; ++y) std::copy_n(art.pixels.begin()+size_t(y)*112,112,canvas.pixels.begin()+size_t(y+8)*128+8);
    data.reference = std::make_shared<const DotImage>(canvas); data.frames.resize(12,data.frames[0]); data.motions[0].last = 3;
    MYE_EXPECT(AddDotBone(data,{0,100,128,80},-1)); MYE_EXPECT(AddDotBone(data,{0,0,128,100},0));
    MYE_EXPECT(AddDotBone(data,{0,180,64,108},0)); MYE_EXPECT(AddDotBone(data,{64,180,64,108},0));
    data.bones[0].name = "Torso"; data.bones[1].name = "Head"; data.bones[2].name = "Left leg"; data.bones[3].name = "Right leg";
    DotMotion walk; walk.name = "walk"; walk.first = 4; walk.last = 11;
    for (int f = 4; f < 12; ++f) for (int b = 2; b < 4; ++b) {
        auto pose = data.bones[b].rest; pose.degrees = std::sin((f-4)*3.14159265f/4)*8*(b==2 ? 1 : -1);
        walk.keys.push_back({f,b,pose});
    }
    data.motions.push_back(walk); DotMotion greet; greet.name = "greet"; greet.last = 3; greet.next = "idle";
    auto head = data.bones[1].rest; auto tilted = head; tilted.degrees = 8;
    greet.keys = {{0,1,head},{1,1,tilted},{3,1,head}}; data.motions.push_back(greet);
    const auto root = FreshDotRoot(); ProjectContext project;
    MYE_EXPECT(project.Create("제작 가이드",Utf8String(root),false,MYE_STARTER_SOURCE_DIR)); if (!project.IsOpen()) return;
    auto* dot = project.NewDot(); dot->Dot() = data;
    MYE_EXPECT(project.SaveDot(dot->Id(),"assets/sprites/novice.dot"));
    const auto exported = ExportDotMotions(data,root); MYE_EXPECT(exported); if (!exported) return;
    auto* scene = project.Active(); MYE_EXPECT(scene); if (!scene) return;
    EditorContext context; PlayModeController play; Bind(context,*scene,play);
    scene->World().Query<runtime::CharacterController2D>().Each([&](ecs::Entity entity,auto&) {
        MYE_EXPECT(AssignCharacterMotion(context,entity,exported.Value().animations[0],false));
        MYE_EXPECT(AssignCharacterMotion(context,entity,exported.Value().animations[1],true));
        scene->World().TryGet<scene::LocalTransform>(entity)->scale = {.25f,.25f,.25f};
    });
    MYE_EXPECT(project.Save()); std::printf("  [ DOT GUIDE ] %s\n",Utf8String(root).c_str());
}
