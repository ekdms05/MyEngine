#include "TestFramework.h"
#include "mye/editor/Project.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Viewport.h"
#include "mye/editor/Command.h"
#include "mye/editor/EditorContext.h"
#include "mye/runtime/ObjectSystem.h"
#include "mye/runtime/OnlineScene.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/anim/AnimationSystem.h"
#include "mye/scene/SceneSerializer.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera2D.h"
#include "mye/scene/Camera3D.h"
#include "mye/phys/Collision.h"
#include "mye/phys/PhysicsWorld2D.h"
#include "mye/phys/PhysicsComponents3D.h"
#include "mye/ecs/World.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Events.h"
#include "mye/ser/JsonArchive.h"
#include "mye/gameplay/Progression.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/Widgets.h"
#include "mye/core/Input.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetMeta.h"
#include "mye/asset/FileSystem.h"
#include <fstream>
#include <chrono>
#include <filesystem>
#include <limits>

using namespace mye;
namespace {
ecs::Entity Object(ecs::World& world, std::string name, Vec2 position = {}) {
    const auto e = world.Create();
    world.Add<scene::ObjectName>(e).value = std::move(name);
    world.Add<scene::LocalTransform>(e).position = {position.x, position.y, 0};
    world.Add<scene::WorldTransform>(e);
    return e;
}
ecs::Entity Player(ecs::World& world) {
    const auto player = Object(world, "Player");
    world.Add<phys::Collider2D>(player).shape = phys::Shape2D::MakeBox(.1f, .1f);
    world.Add<phys::KinematicBody2D>(player);
    world.Add<runtime::CharacterController2D>(player).speed = 2;
    world.Add<gameplay::Progression>(player).level = 7;
    return player;
}
std::filesystem::path FreshObjectRoot() {
    return Utf8Path(MYE_TEST_DATA_DIR) / "object-workflow" / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
std::filesystem::path OnlineProject(ecs::World& world) {
    const auto root = FreshObjectRoot();
    std::filesystem::create_directories(root / "assets/scenes");
    MYE_EXPECT(WriteJsonFile(root / "project.myeproj", json::Value::Object{
        {"version", int64_t{1}}, {"name", std::string("Online fixture")},
        {"mainScene", std::string("assets/scenes/main.scene")}}));
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    return root;
}
}

MYE_TEST(GameUiClicksRunAtFixedTickAndModalCancelsGameplayAndPendingInput) {
    editor::Document document({1},editor::Document::Kind::Scene,"");
    auto& world=document.World(); const auto player=Player(world); const auto object=Object(world,"HUD");
    const auto root=FreshObjectRoot(); std::filesystem::create_directories(root/"assets");
    ui::UiDocument hud; hud.root.typeName="Panel"; hud.root.name="hud"; hud.root.anchors=ui::AnchorRect::Fill();
    ui::UiNodeDesc bar; bar.typeName="ProgressBar"; bar.name="count"; bar.anchors=ui::AnchorRect::TopLeft({20,120},{100,16});
    ui::UiNodeDesc open; open.typeName="Button"; open.name="open"; open.anchors=ui::AnchorRect::TopLeft({20,20},{100,40});
    ui::UiNodeDesc fail=open; fail.name="fail"; fail.anchors.offsetMin.y=70;
    ui::UiNodeDesc dialog; dialog.typeName="Panel"; dialog.name="dialog"; dialog.anchors=ui::AnchorRect::TopLeft({200,100},{200,100});
    dialog.properties={{"modal","true"},{"visible","false"}};
    auto close=open; close.name="close"; close.anchors.offsetMin={10,10}; dialog.children={close};
    ui::UiNodeDesc entry; entry.typeName="TextInput"; entry.name="entry"; entry.anchors=ui::AnchorRect::TopLeft({500,120},{200,30});
    hud.root.children={bar,open,fail,dialog,entry};
    const auto encoded=ui::SaveDocumentJson(hud); MYE_EXPECT(encoded); if(!encoded) return;
    { std::ofstream file(root/"assets/hud.ui",std::ios::binary); file<<encoded.Value(); }
    asset::VirtualFileSystem files; files.Mount("assets",std::make_unique<asset::LooseFileSystem>(Utf8String(root/"assets")),0);
    asset::AssetManager assets(files,nullptr); asset::AssetDatabase database(assets,nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(root/"assets")));
    world.Add<runtime::GameUi>(object).document={database.GuidFromPath("assets://hud.ui"),0};
    world.Add<runtime::ObjectBehavior>(object).luaSource=R"(
return {on_init=function(self)
    assert(not mye.ui.on_click("count",function() end))
    assert(not mye.ui.on_click("open",true))
    assert(not mye.ui.focus("missing"))
    assert(mye.ui.on_click("open",function()
        assert(mye.ui.set_progress("count",1,100))
        assert(mye.ui.set_visible("dialog",true))
        assert(not mye.ui.focus("open"))
        assert(mye.ui.focus("close"))
    end))
    assert(mye.ui.on_click("close",function()
        assert(mye.ui.set_progress("count",2,100))
        assert(mye.ui.set_visible("dialog",false))
    end))
    assert(mye.ui.on_click("fail",function() error("CLICK_FAILURE_WITNESS") end))
    assert(not mye.ui.on_submit("count",function() end))
    assert(not mye.ui.on_submit("entry",false))
    assert(not mye.ui.get_text("count"))
    assert(mye.ui.on_submit("entry",function(value)
        assert(value=="가😀{red}" and mye.ui.get_text("entry")==value)
        assert(not mye.ui.set_text("entry","a\nb") and mye.ui.get_text("entry")==value)
        assert(mye.ui.set_text("entry",""))
        assert(mye.ui.set_progress("count",3,100))
    end))
end}
)";
    editor::PlayModeController play; play.SetEditWorld(&world); MYE_EXPECT(play.Play());
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets));
    auto* count=play.UiRoot()->findByName("count")->As<ui::ProgressBar>();
    auto* transform=play.ActiveWorld()->TryGet<scene::LocalTransform>(player);
    MYE_EXPECT(count && transform); if(!count || !transform) return;
    const auto origin=transform->position;
    InputState physical; physical.OnKey(KeyCode::D,true); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
    auto filtered=physical; auto allowed=play.FilterUiInput(filtered,{30,30},true);
    MYE_EXPECT(allowed && allowed.Value() && filtered.IsMouseSuppressed() && !physical.IsMouseSuppressed());
    MYE_EXPECT(count->value==0); // Frame input queues; Lua must run only at the fixed tick.
    runtime::GameInputBuffer controls; MYE_EXPECT(controls.Configure(runtime::DefaultGameInputMap())); controls.Capture(filtered,allowed.Value());
    MYE_EXPECT(play.Tick(1.0f/60,controls.ConsumeTick(),Utf8String(root),&database,&files,&assets));
    MYE_EXPECT(count->value==1 && transform->position==origin);
    physical.NewFrame(); filtered=physical; allowed=play.FilterUiInput(filtered,{500,500},true);
    MYE_EXPECT(allowed && !allowed.Value()); controls.Capture(filtered,allowed.Value());
    MYE_EXPECT(play.Tick(1.0f/60,controls.ConsumeTick(),Utf8String(root),&database,&files,&assets));
    MYE_EXPECT(transform->position==origin);
    physical.NewFrame(); physical.OnKey(KeyCode::Enter,true); physical.OnKey(KeyCode::Enter,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{-1,-1},true));
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets)); MYE_EXPECT(count->value==2);
    physical.NewFrame(); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{30,30},true));
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{},false));
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets)); MYE_EXPECT(count->value==2);
    physical.NewFrame(); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{30,30},true));
    auto* openButton=play.UiRoot()->findByName("open")->As<ui::Button>(); openButton->state=ui::Button::State::Disabled;
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets)); MYE_EXPECT(count->value==2);
    openButton->state=ui::Button::State::Normal;
    for (int index=0; index<65; ++index) {
        physical.NewFrame(); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
        filtered=physical; const auto queued=play.FilterUiInput(filtered,{30,80},true);
        MYE_EXPECT(index<64 ? bool(queued) : (!queued && queued.GetError().message.find("64 pending UI actions")!=std::string::npos));
    }
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{},false));
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets));
    physical.NewFrame(); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{30,80},true));
    const auto failure=play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets);
    MYE_EXPECT(!failure && failure.GetError().message.find("CLICK_FAILURE_WITNESS")!=std::string::npos);
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets));
    physical.NewFrame(); physical.OnMouseButton(MouseButton::Left,true); physical.OnMouseButton(MouseButton::Left,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{510,130},true));
    const auto textFocus=play.TextFocus(); MYE_EXPECT(textFocus && filtered.IsKeyboardSuppressed()); if(!textFocus) return;
    physical.SetTextInputFocus(textFocus->id); physical.NewFrame();
    TextEdit composition; composition.kind=TextEdit::Kind::Composition; composition.text="ㄱ"; composition.cursorBytes=3; composition.composing=true;
    MYE_EXPECT(physical.OnTextEdit(composition)); physical.OnKey(KeyCode::Enter,true); physical.OnKey(KeyCode::Enter,false);
    filtered=physical; MYE_EXPECT(play.FilterUiInput(filtered,{-1,-1},true));
    auto* field=play.UiRoot()->findByName("entry")->As<ui::TextInput>();
    MYE_EXPECT(field->composition=="ㄱ" && field->text.empty());
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets)); MYE_EXPECT(count->value==2);
    physical.NewFrame(); MYE_EXPECT(physical.OnTextEdit(TextEdit{TextEdit::Kind::Insert,"가😀{red}"}));
    composition.text=""; composition.cursorBytes=0; composition.composing=false; MYE_EXPECT(physical.OnTextEdit(composition));
    physical.OnKey(KeyCode::Enter,true); physical.OnKey(KeyCode::Enter,false);
    filtered=physical; allowed=play.FilterUiInput(filtered,{-1,-1},true); MYE_EXPECT(allowed && allowed.Value());
    MYE_EXPECT(field->text=="가😀{red}" && field->composition.empty() && count->value==2);
    controls.Capture(filtered,allowed.Value());
    MYE_EXPECT(play.Tick(1.0f/60,controls.ConsumeTick(),Utf8String(root),&database,&files,&assets));
    MYE_EXPECT(count->value==3 && field->text.empty() && transform->position==origin);
    MYE_EXPECT(play.TextFocus() && play.TextFocus()->id!=textFocus->id); // Lua replacement invalidates native preedit.
    play.Stop(); MYE_EXPECT(!play.UiRoot());
}

MYE_TEST(SavedGameUiBindsLuaBeforeInitAndRestartsWithoutLeakingState) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto object = Object(world,"HUD");
    const auto root = FreshObjectRoot();
    std::filesystem::create_directories(root / "assets");
    ui::UiDocument hud;
    hud.root.typeName = "Panel"; hud.root.name = "hud"; hud.root.anchors = ui::AnchorRect::Fill();
    ui::UiNodeDesc label; label.typeName = "Label"; label.name = "stats"; label.properties = {{"text","saved"}};
    ui::UiNodeDesc bar; bar.typeName = "ProgressBar"; bar.name = "hp";
    ui::UiNodeDesc button; button.typeName = "Button"; button.name = "potion";
    hud.root.children = {label,bar,button};
    const auto saved = ui::SaveDocumentJson(hud); MYE_EXPECT(saved); if (!saved) return;
    const auto path = root / "assets/hud.ui";
    { std::ofstream output(path,std::ios::binary); output << saved.Value(); }
    asset::VirtualFileSystem files;
    files.Mount("assets",std::make_unique<asset::LooseFileSystem>(Utf8String(root / "assets")),0);
    asset::AssetManager assets(files,nullptr);
    asset::AssetDatabase database(assets,nullptr);
    MYE_EXPECT(database.ScanDirectory(Utf8String(root / "assets")));
    const auto guid = database.GuidFromPath("assets://hud.ui"); MYE_EXPECT(guid.IsValid());
    MYE_EXPECT(database.ScanDirectory(Utf8String(root / "assets")));
    MYE_EXPECT(database.GuidFromPath("assets://hud.ui") == guid);
    world.Add<runtime::GameUi>(object).document = {guid,ui::UiDocument::kAssetTypeId};
    world.Add<runtime::ObjectBehavior>(object).luaSource = R"(
return {on_init=function(self)
    assert(mye.ui.set_text("stats","한글 {color=#FF0000}literal"))
    assert(mye.ui.set_progress("hp",75,100))
    assert(mye.ui.set_enabled("potion",false))
    assert(mye.ui.set_visible("stats",false))
    for _,bad in ipairs({"75",0/0,math.huge,-1,101}) do
        local ok,err=mye.ui.set_progress("hp",bad,100) assert(ok==nil and type(err)=="string")
    end
    assert(not mye.ui.set_text("hp","wrong type"))
    assert(not mye.ui.set_text("stats",false))
    assert(not mye.ui.set_text("stats",string.rep("x",4097)))
    assert(not mye.ui.set_text("stats","nul"..string.char(0)))
    assert(not mye.ui.set_visible("missing",true))
    assert(not mye.ui.set_enabled("potion",1))
    assert(not mye.ui.set_visible({},true))
    assert(not mye.ui.set_progress("hp",0,0))
    assert(not mye.ui.set_progress("hp",0,100,42))
end,on_update=function(self,dt) assert(mye.ui.set_visible("stats",true)) end}
)";
    runtime::ObjectSystem unbound(world);
    MYE_EXPECT(!unbound.Initialize()); MYE_EXPECT(!unbound.UiRoot());
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    for (int run=0;run<2;++run) {
        MYE_EXPECT(play.Play());
        MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets));
        auto* uiRoot = play.UiRoot(); MYE_EXPECT(uiRoot);
        if (uiRoot) {
            auto* hp = uiRoot->findByName("hp")->As<ui::ProgressBar>();
            auto* stats = uiRoot->findByName("stats")->As<ui::Label>();
            auto* potion = uiRoot->findByName("potion")->As<ui::Button>();
            MYE_EXPECT(hp->value == 75 && hp->maximum == 100);
            MYE_EXPECT(stats->text == "한글 {{color=#FF0000}literal" && stats->visibility == ui::Visibility::Visible);
            MYE_EXPECT(potion->state == ui::Button::State::Disabled && !potion->interactive);
        }
        MYE_EXPECT(play.Message().empty());
        play.Stop(); MYE_EXPECT(!play.UiRoot() && world.Has<runtime::GameUi>(object));
    }
    // A destination whose UI fails to load must not replace the running world.
    std::filesystem::create_directories(root / "assets/scenes");
    Player(world);
    editor::Document destination({2},editor::Document::Kind::Scene,"");
    Player(destination.World());
    const auto spawn = Object(destination.World(),"Spawn",{5,0});
    destination.World().Add<runtime::GameUi>(spawn).document = {asset::AssetGuid::Generate(),0};
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(destination.World(),Utf8String(root / "assets/scenes/destination.scene")));
    runtime::ObjectConnection portal;
    portal.event = runtime::ObjectEvent::Start; portal.action = runtime::ObjectAction::ChangeMap;
    portal.text = "assets/scenes/destination.scene"; portal.target = "Spawn";
    world.TryGet<runtime::ObjectBehavior>(object)->connections.push_back(portal);
    MYE_EXPECT(play.Play());
    MYE_EXPECT(play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets));
    auto* originalWorld = play.ActiveWorld();
    auto* originalUi = play.UiRoot();
    bool refused = false;
    for (int tick=0;tick<90 && !refused;++tick)
        refused = !play.Tick(1.0f/60,runtime::GameInput{},Utf8String(root),&database,&files,&assets);
    MYE_EXPECT(refused && play.ActiveWorld() == originalWorld && play.UiRoot() == originalUi);
    if (play.UiRoot()) MYE_EXPECT(play.UiRoot()->findByName("hp")->As<ui::ProgressBar>()->value == 75);
    play.Stop();
    world.TryGet<runtime::GameUi>(object)->document.guid = asset::AssetGuid::Generate();
    runtime::ObjectSystem missing(world); MYE_EXPECT(!missing.Initialize(&database,&files,&assets));
    MYE_EXPECT(!missing.UiRoot());
    world.TryGet<runtime::GameUi>(object)->document.guid = guid;
    { std::ofstream output(path,std::ios::binary); output << "{}"; }
    runtime::ObjectSystem corrupt(world); MYE_EXPECT(!corrupt.Initialize(&database,&files,&assets));
    MYE_EXPECT(!corrupt.UiRoot());
    const auto another = Object(world,"AnotherHUD");
    world.Add<runtime::GameUi>(another).document = {guid,0}; MYE_EXPECT(!runtime::ValidateObjectComponents(world));
}

MYE_TEST(OnlineScene2DUsesSharedCentersFloorsAndValidatedSpawns) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    world.TryGet<phys::Collider2D>(player)->shape = phys::Shape2D::MakeCircle(.2f);
    world.TryGet<phys::Collider2D>(player)->offset = {.05f, .1f};
    world.Add<scene::FloorLevel>(player).level = 1;
    const auto parent = Object(world, "Parent", {2, 0});
    const auto wall = Object(world, "Wall", {.3f, .4f});
    auto& collider = world.Add<phys::Collider2D>(wall);
    collider.shape = phys::Shape2D::MakeBox(.005f, 10);
    collider.offset = {.1f, .2f};
    scene::ApplyReparent(world, wall, parent, false);
    world.Add<scene::FloorLevel>(wall).level = 1;
    const auto upper = Object(world, "Upper floor");
    world.Add<phys::Collider2D>(upper);
    world.Add<scene::FloorLevel>(upper).level = 2;
    const auto trigger = Object(world, "Trigger");
    world.Add<phys::Collider2D>(trigger).isTrigger = true;
    world.Add<scene::FloorLevel>(trigger).level = 1;
    scene::UpdateWorldTransforms(world);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    auto loaded = runtime::LoadOnlineScene2D(project);
    MYE_EXPECT(loaded);
    if (!loaded) return;
    const auto& online = loaded.Value();
    const auto selected = runtime::LoadOnlineScene(project);
    MYE_EXPECT(selected && std::holds_alternative<runtime::OnlineScene2D>(selected.Value()));
    if (selected) {
        const auto& scene = std::get<runtime::OnlineScene2D>(selected.Value());
        MYE_EXPECT(scene.hash == online.hash && scene.sceneId == online.sceneId);
        MYE_EXPECT_NEAR(scene.offset.y, online.offset.y, .00001f);
    }
    MYE_EXPECT(online.colliders.size() == 3 && online.hash != 0);
    MYE_EXPECT(online.character.shape.kind == phys::ShapeKind::Circle);
    MYE_EXPECT(online.character.floorMask == phys::FloorBit(1));
    MYE_EXPECT_NEAR(online.character.pos.x, .05f, .00001f);
    MYE_EXPECT_NEAR(online.character.pos.y, .1f, .00001f);
    MYE_EXPECT_NEAR(online.spawn.x, 0, .00001f);
    MYE_EXPECT_NEAR(online.offset.y, .1f, .00001f);
    MYE_EXPECT_NEAR(online.speed, 2, .00001f);
    const auto alias = runtime::LoadOnlineScene2D(project, "assets/scenes/../scenes/main.scene");
    MYE_EXPECT(alias && alias.Value().sceneId == online.sceneId && alias.Value().hash == online.hash);
    const auto moved = phys::MoveAndSlide2D(online.character, online.colliders, {30, 0}, .1f, online.maxSlideIters);
    MYE_EXPECT(moved);
    if (!moved) return;
    MYE_EXPECT_NEAR(moved.Value().position.x - online.offset.x, 2.145f, .00001f);
    world.TryGet<phys::KinematicBody2D>(player)->velocity = {30, 0};
    MYE_EXPECT(phys::PhysicsWorld2D{}.Step(world, nullptr, .1f));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x,
                    moved.Value().position.x - online.offset.x, .00001f);
    auto saved = online.character;
    saved.pos = {2.4f, .6f};
    MYE_EXPECT(!phys::ValidateSpawn2D(saved, online.colliders));
    MYE_EXPECT_NEAR(saved.pos.x, 2.4f, .00001f); // Invalid stored positions are never repaired.
    saved.pos = {.05f, .1f};
    MYE_EXPECT(phys::ValidateSpawn2D(saved, online.colliders));
    MYE_EXPECT(runtime::LoadOnlineScene2D(Utf8String(Utf8Path(MYE_STARTER_SOURCE_DIR) / "project.myeproj")));
}

MYE_TEST(OnlineMapCatalogValidatesNamedSpawnsAndCanonicalIdentities) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World(); Player(world);
    const auto gate = Object(world, "Gate");
    auto& portal = world.Add<runtime::ScenePortal>(gate);
    portal.scenePath = "assets/scenes/other.scene"; portal.spawnName = "Arrival"; portal.onInteract = true;
    world.Add<runtime::InteractionTarget>(gate);
    const auto arrival = Object(world, "Arrival", {2, 0});
    scene::UpdateWorldTransforms(world);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    MYE_EXPECT(!runtime::LoadOnlineMaps2D(project)); // A missing reachable destination refuses startup.
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/other.scene")));
    auto maps = runtime::LoadOnlineMaps2D(project);
    MYE_EXPECT(maps && maps.Value().size() == 2);
    if (maps) MYE_EXPECT(maps.Value()[0].hash != maps.Value()[1].hash); // Identical contents, separate map paths.
    world.TryGet<scene::ObjectName>(arrival)->value = "Renamed";
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/other.scene")));
    MYE_EXPECT(!runtime::LoadOnlineMaps2D(project));
    world.TryGet<scene::ObjectName>(arrival)->value = "Arrival";
    world.TryGet<scene::LocalTransform>(arrival)->position = {0, 0, 0};
    auto wall = Object(world, "Blocked arrival"); world.Add<phys::Collider2D>(wall);
    // Keep the prototype spawn clear while blocking the named arrival.
    world.TryGet<scene::LocalTransform>(arrival)->position = {2, 0, 0};
    world.TryGet<scene::LocalTransform>(wall)->position = {2, 0, 0};
    scene::UpdateWorldTransforms(world);
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/other.scene")));
    MYE_EXPECT(!runtime::LoadOnlineMaps2D(project));
}

MYE_TEST(SavedTwoDLocomotionMatchesPlayAndStandaloneFacing) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& authored = document.World();
    const auto player = Player(authored);
    const auto idle = asset::AssetGuid::Generate();
    const auto walk = asset::AssetGuid::Generate();
    authored.TryGet<runtime::CharacterController2D>(player)->idleAnimation.guid = idle;
    authored.TryGet<runtime::CharacterController2D>(player)->walkAnimation.guid = walk;
    authored.Add<anim::SpriteAnimator>(player).animation.guid = idle;
    const auto saved = scene::SceneSerializer{}.WriteWorld(authored);
    MYE_EXPECT(saved);
    if (!saved) return;
    editor::Document standalone({2}, editor::Document::Kind::Scene, "");
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(standalone.World(), saved.Value()));
    runtime::ObjectSystem objects(standalone.World());
    MYE_EXPECT(objects.Initialize());
    editor::PlayModeController play; play.SetEditWorld(&authored);
    MYE_EXPECT(play.Play());
    if (!play.ActiveWorld()) return;
    for (int i = 0; i < static_cast<int>(anim::Dir8::Count); ++i) {
        const auto direction = static_cast<anim::Dir8>(i);
        const runtime::GameInput input{anim::Dir8Vector(direction)};
        MYE_EXPECT(play.Tick(1.0f / 60, input, "") && objects.Tick(1.0f / 60, input));
        for (auto* world : {play.ActiveWorld(), &standalone.World()}) {
            const auto& animator = *world->TryGet<anim::SpriteAnimator>(player);
            MYE_EXPECT(animator.facing == direction && animator.animation.guid == walk);
        }
    }
    MYE_EXPECT(play.Tick(1.0f / 60, {}, "") && objects.Tick(1.0f / 60, {}));
    for (auto* world : {play.ActiveWorld(), &standalone.World()}) {
        const auto& animator = *world->TryGet<anim::SpriteAnimator>(player);
        MYE_EXPECT(animator.facing == anim::Dir8::DownRight && animator.animation.guid == idle);
    }
    play.Stop();
    MYE_EXPECT(authored.TryGet<anim::SpriteAnimator>(player)->facing == anim::Dir8::Down);
    MYE_EXPECT(authored.TryGet<anim::SpriteAnimator>(player)->animation.guid == idle);
}

MYE_TEST(SharedTwoDAnimationRequestsPreserveSuccessorsAndStoppedFacing) {
    runtime::CharacterController2D controller;
    controller.idleAnimation.guid = asset::AssetGuid::Generate();
    controller.walkAnimation.guid = asset::AssetGuid::Generate();
    anim::SpriteAnimator animator;
    runtime::UpdateCharacterAnimation2D(animator, controller, {.00001f, 0}, {1, 0});
    MYE_EXPECT(animator.animation.guid == controller.walkAnimation.guid && animator.facing == anim::Dir8::Right);
    const auto successor = asset::AssetGuid::Generate();
    animator.animation.guid = successor; animator.cursor = {2, .045f, false}; animator.started = true;
    runtime::UpdateCharacterAnimation2D(animator, controller, {0, .01f}, {0, 1});
    MYE_EXPECT(animator.animation.guid == successor && animator.facing == anim::Dir8::Up);
    MYE_EXPECT(animator.cursor.step == 2 && animator.started);
    MYE_EXPECT_NEAR(animator.cursor.timeInStep, .045f, 1e-6f);
    runtime::UpdateCharacterAnimation2D(animator, controller, {}, {-1, 0}); // Against a wall.
    MYE_EXPECT(animator.animation.guid == controller.idleAnimation.guid && animator.facing == anim::Dir8::Left);
    MYE_EXPECT(animator.cursor.step == 0 && !animator.started);
    animator.animation.guid = successor;
    runtime::UpdateCharacterAnimation2D(animator, controller, {}, {});
    MYE_EXPECT(animator.animation.guid == successor && animator.facing == anim::Dir8::Left);
    controller.idleAnimation = {};
    runtime::UpdateCharacterAnimation2D(animator, controller, {}, {});
    MYE_EXPECT(animator.animation.guid == successor);
    const auto invalid = std::numeric_limits<float>::quiet_NaN();
    MYE_EXPECT(anim::Dir8FromVector({invalid, 0}, anim::Dir8::Up) == anim::Dir8::Up);
    MYE_EXPECT(anim::Dir8FromVector({0, std::numeric_limits<float>::infinity()}, anim::Dir8::Left) == anim::Dir8::Left);
}

MYE_TEST(SavedTwoDCameraMatchesPlayAndStandaloneFixedTicks) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    Player(world);
    const auto cameraEntity = Object(world, "Camera", {100, 100});
    auto& camera = world.Add<scene::Camera2D>(cameraEntity);
    camera.followTarget = "Player"; camera.offset = {.003f, .002f};
    camera.zoom = 1.5f; camera.deadzoneHalf = {.4f, .3f};
    camera.boundsEnabled = true; camera.bounds = {-12, -8, 24, 16};
    world.Add<runtime::ObjectBehavior>(cameraEntity).luaSource = R"(
        return {on_init = function(self)
            mye.world.entity_from_packed(self.entity):shake_camera(.2, .2)
        end}
    )";
    const auto saved = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(saved);
    if (!saved) return;
    editor::Document standalone({2}, editor::Document::Kind::Scene, "");
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(standalone.World(), saved.Value()));
    runtime::ObjectSystem objects(standalone.World());
    MYE_EXPECT(objects.Initialize());
    render::Camera2D defaultCamera;
    runtime::UpdateDefaultCamera2D(standalone.World(), defaultCamera, true);
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    MYE_EXPECT(play.Play());
    play.Pause(); play.StepFrame();
    MYE_EXPECT(play.ConsumeStepRequest() && !play.ConsumeStepRequest());
    for (int i = 0; i < 120; ++i) {
        runtime::GameInput input{{1, 0}};
        if (i == 10) input.cameraZoomSteps = 1;
        MYE_EXPECT(play.Tick(1.0f / 60, input, ""));
        MYE_EXPECT(objects.Tick(1.0f / 60, input));
        runtime::UpdateDefaultCamera2D(standalone.World(), defaultCamera);
        const auto a = scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera());
        const auto b = scene::BuildGameView(standalone.World(), defaultCamera);
        MYE_EXPECT(a && b);
        if (!a || !b) return;
        for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
            MYE_EXPECT_NEAR(a.Value().viewProj.m[row][col], b.Value().viewProj.m[row][col], .000001f);
        MYE_EXPECT_NEAR(a.Value().subpixelResidual.x, b.Value().subpixelResidual.x, .000001f);
        MYE_EXPECT_NEAR(a.Value().subpixelResidual.y, b.Value().subpixelResidual.y, .000001f);
    }
    play.ActiveWorld()->Query<scene::Camera2D>().Each([&](ecs::Entity, auto& c) {
        MYE_EXPECT_NEAR(c.view.Position().x, 3.603f, .0001f);
        MYE_EXPECT_NEAR(c.zoom, 1.65f, .0001f);
        MYE_EXPECT(!c.view.IsShaking());
        const auto position = c.view.Position();
        MYE_EXPECT(scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera()));
        MYE_EXPECT(c.view.Position() == position); // Render does not consume another follow/shake tick.
        c.current = false;
    });
    standalone.World().Query<scene::Camera2D>().Each([](ecs::Entity, auto& c) { c.current = false; });
    const auto a = scene::BuildGameView(*play.ActiveWorld(), play.DefaultCamera());
    const auto b = scene::BuildGameView(standalone.World(), defaultCamera);
    MYE_EXPECT(a && b && ApproxEqual(a.Value().view.m[3][0], b.Value().view.m[3][0]));
    play.Stop();
    MYE_EXPECT(world.TryGet<scene::Camera2D>(cameraEntity)->zoom == 1.5f && !camera.initialized);
}

MYE_TEST(OnlineScene2DRefusesInvalidPrototypesAndPreservesSceneFiles) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    const auto sceneFile = root / "assets/scenes/main.scene";
    const auto verify = [&] {
        MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(sceneFile)));
        const auto before = ReadJsonFile(sceneFile);
        MYE_EXPECT(before);
        MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
        MYE_EXPECT(!runtime::LoadOnlineScene(project));
        const auto after = ReadJsonFile(sceneFile);
        MYE_EXPECT(after);
        if (before && after) MYE_EXPECT(json::Stringify(before.Value()) == json::Stringify(after.Value()));
    };
    world.TryGet<runtime::CharacterController2D>(player)->enabled = false;
    verify();
    world.TryGet<runtime::CharacterController2D>(player)->enabled = true;
    world.TryGet<phys::Collider2D>(player)->isTrigger = true;
    verify();
    world.TryGet<phys::Collider2D>(player)->isTrigger = false;
    const auto duplicate = Player(world);
    world.TryGet<scene::ObjectName>(duplicate)->value = "Second player";
    verify();
    world.Destroy(duplicate);
    const auto moving = Object(world, "Unsupported mover", {4, 0});
    world.Add<phys::Collider2D>(moving);
    world.Add<phys::KinematicBody2D>(moving);
    verify();
    world.Destroy(moving);
    const auto wall = Object(world, "Blocking spawn");
    world.Add<phys::Collider2D>(wall);
    verify();
    world.Destroy(wall);
    world.Add<scene::FloorLevel>(player).level = 8;
    verify();
}

MYE_TEST(OnlineScenePathBoundsAndExisting3DLoad) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Object(world, "Player");
    world.Add<phys::Collider3D>(player).offset = {0, .8f, 0};
    world.TryGet<phys::Collider3D>(player)->half = {.3f, .8f, .3f};
    world.Add<phys::KinematicBody3D>(player);
    world.Add<runtime::CharacterController3D>(player);
    const auto root = OnlineProject(world);
    const auto project = Utf8String(root / "project.myeproj");
    MYE_EXPECT(runtime::LoadOnlineScene3D(project));
    const auto selected = runtime::LoadOnlineScene(project);
    MYE_EXPECT(selected && std::holds_alternative<runtime::OnlineScene3D>(selected.Value()));
    const auto previousDirectory = std::filesystem::current_path();
    std::filesystem::current_path(root);
    const auto relativeProject = runtime::LoadOnlineScene3D("project.myeproj");
    std::filesystem::current_path(previousDirectory);
    MYE_EXPECT(relativeProject);
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
    const auto other = Player(world);
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    MYE_EXPECT(!runtime::LoadOnlineScene(project)); // Never choose a dimension for mixed enabled prototypes.
    world.Destroy(other);
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(world, Utf8String(root / "assets/scenes/main.scene")));
    for (auto path : {"../outside.scene", "assets/scenes/missing.scene", "assets/scenes/main.txt"}) {
        MYE_EXPECT(!runtime::LoadOnlineScene2D(project, path));
        MYE_EXPECT(!runtime::LoadOnlineScene3D(project, path));
        MYE_EXPECT(!runtime::LoadOnlineScene(project, path));
    }
    const auto absolute = Utf8String(root / "assets/scenes/main.scene");
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, absolute));
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, absolute));
    std::string nul = "assets/scenes/main.scene";
    nul.push_back('\0'); nul += "outside";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, nul));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, nul));
    const std::string invalidUtf8 = "assets/scenes/" + std::string(1, static_cast<char>(0xFF)) + ".scene";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project, invalidUtf8));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project, invalidUtf8));
    const auto invalidProject = project + std::string(1, static_cast<char>(0xFF)) + ".myeproj";
    MYE_EXPECT(!runtime::LoadOnlineScene3D(invalidProject));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(invalidProject));
    MYE_EXPECT(WriteJsonFile(root / "project.myeproj", json::Value::Object{
        {"version", int64_t{99}}, {"name", std::string("Unsupported")},
        {"mainScene", std::string("assets/scenes/main.scene")}}));
    MYE_EXPECT(!runtime::LoadOnlineScene3D(project));
    MYE_EXPECT(!runtime::LoadOnlineScene2D(project));
}

MYE_TEST(ObjectNamedActionsAreVisibleOnlyInsideTheirFixedTick) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    const auto marker = Object(world, "Input marker");
    world.Add<runtime::ObjectBehavior>(marker).luaSource = R"(
        return {
            on_init = function(self)
                assert(not mye.input.is_action_just_pressed('attack'))
            end,
            on_update = function(self, dt)
                local entity = mye.world.entity_from_packed(self.entity)
                local p = entity:get_position()
                if mye.input.is_action_just_pressed('attack') then p.x = p.x + 1 end
                if mye.input.is_action_just_released('attack') then p.y = p.y + 1 end
                entity:set_position(p)
            end,
            on_interact = function(self)
                assert(not mye.input.is_action_just_pressed('attack'))
                assert(not mye.input.is_action_pressed('attack'))
                mye.world.entity_from_packed(self.entity):set_position(mye.Vec2(10, 10))
            end,
            on_destroy = function(self)
                assert(not mye.input.is_action_pressed('attack'))
                mye.world.entity_from_packed(self.entity):set_position(mye.Vec2(20, 20))
            end
        }
    )";
    {
        runtime::ObjectSystem objects(world); MYE_EXPECT(objects.Initialize());
        {
            runtime::GameInputBuffer buffer;
            MYE_EXPECT(buffer.Configure({{{"attack", .2f, {{InputDevice::Key, static_cast<int>(KeyCode::F)}}}}}));
            InputState input; input.NewFrame(); input.OnKey(KeyCode::F, true); input.OnKey(KeyCode::F, false);
            buffer.Capture(input, true);
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(1, 1, 0));
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(1, 1, 0));
            input.NewFrame(); input.OnKey(KeyCode::F, true); buffer.Capture(input, true);
            MYE_EXPECT(objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 0;
            MYE_EXPECT(!objects.Tick(1.0f / 60, buffer.ConsumeTick()));
            world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 4;
            objects.Dispatch(marker, runtime::ObjectEvent::Interact);
            MYE_EXPECT(objects.Message().empty());
        } // The input source dies before callbacks outside Tick and VM teardown.
        objects.Dispatch(marker, runtime::ObjectEvent::Interact);
        MYE_EXPECT(objects.Message().empty());
        MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(10, 10, 0));
        MYE_EXPECT(!objects.Tick(0, runtime::GameInput{}));
        MYE_EXPECT(objects.Tick(1.0f / 60, runtime::GameInput{}));
    }
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(marker)->position == Vec3(20, 20, 0));
}

MYE_TEST(ObjectActionInputInterruptsBeforeHitAndLocksDeadControls) {
    constexpr float dt = 1.0f / 60;
    anim::AnimationClipData idle; idle.frameIndices = {0}; idle.frameDurations = {1};
    auto attack = idle; attack.loop = false; attack.frameIndices = {0, 1, 2};
    attack.frameDurations = {dt, dt, dt}; attack.events = {{0, "attack-entry"}, {2, "attack-hit"}};
    auto hurt = attack; hurt.events.clear();
    anim::AnimStateMachine machine;
    for (const auto& [name, clip] : std::vector<std::pair<std::string, const anim::AnimationClipData*>>{
             {"idle", &idle}, {"walk", &idle}, {"attack", &attack}, {"hurt", &hurt}, {"dead", &hurt}}) {
        anim::AnimState state; state.name = name; state.directional = false; state.singleClip = clip;
        machine.states.push_back(std::move(state));
    }
    machine.parameters = {{"moving", anim::ParamType::Bool, 0}, {"dead", anim::ParamType::Bool, 0},
                          {"attack", anim::ParamType::Trigger, 0}, {"hurt", anim::ParamType::Trigger, 0},
                          {"hits", anim::ParamType::Float, 0}, {"entries", anim::ParamType::Float, 0}};
    machine.transitions = {{-1, 4, {{"dead", anim::CmpOp::IsTrue}}, false, {}, false},
        {-1, 3, {{"hurt", anim::CmpOp::IsTrue}, {"dead", anim::CmpOp::IsFalse}}, false, {"attack"}, false},
        {-1, 2, {{"attack", anim::CmpOp::IsTrue}, {"dead", anim::CmpOp::IsFalse}}, false, {}, false},
        {2, 0, {}, true, {}, false}, {3, 0, {}, true, {}, false},
        {0, 1, {{"moving", anim::CmpOp::IsTrue}}, false, {}, false},
        {1, 0, {{"moving", anim::CmpOp::IsFalse}}, false, {}, false}};
    for (bool inPlay : {false, true}) {
        editor::Document document({1}, editor::Document::Kind::Scene, "");
        auto& authored = document.World(); const auto player = Player(authored);
        authored.Add<scene::SpriteRenderer>(player);
        authored.Add<anim::SpriteAnimator>(player).stateMachine.guid = {1, 1};
        const auto stale = Object(authored, "Stale"); authored.Destroy(stale);
        authored.Add<runtime::ObjectBehavior>(player).luaSource = "local stale=" + std::to_string(stale.Packed()) + R"(
            return {
                on_init=function(self)
                    local e=mye.world.entity_from_packed(self.entity)
                    assert(e:get_animation_state()=='idle' and mye.controller2d.is_enabled(self.entity))
                    assert(not pcall(mye.controller2d.set_enabled,self.entity,1))
                    assert(not pcall(mye.controller2d.set_enabled,0,false))
                    assert(not pcall(mye.controller2d.set_enabled,stale,false))
                    assert(not pcall(mye.controller2d.is_enabled,-1))
                end,
                on_update=function(self,dt)
                    local e=mye.world.entity_from_packed(self.entity)
                    local state=e:get_animation_state()
                    if mye.input.is_action_just_pressed('death') then
                        e:reset_trigger('attack'); e:reset_trigger('hurt'); e:set_bool('dead',true)
                        mye.controller2d.set_enabled(self.entity,false)
                    elseif not e:get_bool('dead') and mye.input.is_action_just_pressed('hurt') then
                        e:reset_trigger('attack'); e:set_trigger('hurt')
                        mye.controller2d.set_enabled(self.entity,false)
                    elseif not e:get_bool('dead') then
                        local ready=state=='idle' or state=='walk'
                        if ready and mye.input.is_action_just_pressed('attack') then
                            e:set_trigger('attack'); ready=false
                        end
                        mye.controller2d.set_enabled(self.entity,ready)
                    end
                end,
                on_event=function(self,name,payload)
                    if name~='animation' then return end
                    local e=mye.world.entity_from_packed(self.entity)
                    if payload.name=='attack-entry' then e:set_float('entries',e:get_float('entries')+1) end
                    if payload.name=='attack-hit' then
                        assert(e:get_animation_state()=='attack' and not e:get_bool('dead'))
                        e:set_float('hits',e:get_float('hits')+1)
                    end
                end
            }
        )";
        EventBus events; authored.SetEventBus(&events);
        const auto bind = [&](ecs::World& world) -> Expected<void, Error> {
            anim::BindAnimationState(*world.TryGet<anim::SpriteAnimator>(player), machine, 1); return {};
        };
        editor::PlayModeController play; play.SetEditWorld(&authored); play.SetWorldPreparation(bind);
        std::unique_ptr<runtime::ObjectSystem> objects;
        if (inPlay) MYE_EXPECT(play.Play());
        else { MYE_EXPECT(bind(authored)); objects = std::make_unique<runtime::ObjectSystem>(authored); MYE_EXPECT(objects->Initialize()); }
        auto& world = inPlay ? *play.ActiveWorld() : authored;
        runtime::GameInputBuffer buffer; auto map = runtime::DefaultGameInputMap();
        for (const auto& [name, key] : std::vector<std::pair<std::string, KeyCode>>{{"attack", KeyCode::F}, {"hurt", KeyCode::H}, {"death", KeyCode::K}})
            map.actions.push_back({name, .2f, {{InputDevice::Key, static_cast<int>(key)}}});
        MYE_EXPECT(buffer.Configure(std::move(map)));
        InputState input;
        const auto tick = [&] {
            buffer.Capture(input, true); const auto snapshot = buffer.ConsumeTick();
            MYE_EXPECT(inPlay ? play.Tick(dt, snapshot, "") : objects->Tick(dt, snapshot));
            anim::RunAnimationSystem(world, dt);
            MYE_EXPECT(inPlay ? play.Message().empty() : objects->Message().empty());
            input.NewFrame();
        };
        tick(); input.OnKey(KeyCode::D, true); tick();
        auto& a = *world.TryGet<anim::SpriteAnimator>(player);
        MYE_EXPECT(a.currentState == 1 && a.GetBool("moving"));
        input.OnKey(KeyCode::F, true); tick();
        const auto locked = world.TryGet<scene::LocalTransform>(player)->position;
        MYE_EXPECT(a.currentState == 2 && !a.GetBool("moving") && a.facing == anim::Dir8::Right);
        for (int i = 0; i < 4; ++i) tick();
        MYE_EXPECT(a.GetFloat("hits") == 1 && a.GetFloat("entries") == 1 && a.currentState == 1);
        MYE_EXPECT(world.TryGet<scene::LocalTransform>(player)->position.x > locked.x); // Held F does not restart.
        input.OnKey(KeyCode::F, false); tick(); input.OnKey(KeyCode::F, true); tick();
        input.OnKey(KeyCode::H, true); tick();
        MYE_EXPECT(a.currentState == 3 && !a.GetBool("attack") && a.GetFloat("hits") == 1);
        for (int i = 0; i < 5; ++i) tick();
        MYE_EXPECT(a.currentState == 1 && a.GetFloat("entries") == 2 && a.GetFloat("hits") == 1);
        input.OnKey(KeyCode::F, false); tick(); input.OnKey(KeyCode::F, true); tick();
        input.OnKey(KeyCode::K, true); tick();
        const auto deadPosition = world.TryGet<scene::LocalTransform>(player)->position;
        const auto deadFacing = a.facing;
        for (int i = 0; i < 8; ++i) {
            input.OnKey(KeyCode::F, i % 2 == 0); input.OnKey(KeyCode::H, i % 2 == 0); tick();
            MYE_EXPECT(a.currentState == 4 && !a.GetBool("moving") && a.facing == deadFacing);
            MYE_EXPECT(world.TryGet<scene::LocalTransform>(player)->position == deadPosition);
        }
        MYE_EXPECT(a.GetFloat("entries") == 3 && a.GetFloat("hits") == 1);
        MYE_EXPECT(!world.TryGet<runtime::CharacterController2D>(player)->enabled);
        play.Stop(); objects.reset();
        if (inPlay) MYE_EXPECT(authored.TryGet<runtime::CharacterController2D>(player)->enabled);
        authored.SetEventBus(nullptr);
    }
}

MYE_TEST(ObjectControlsCollisionTriggerAndLuaInteraction) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    world.TryGet<phys::Collider2D>(player)->offset = {.05f, .05f};
    const auto second = Player(world);
    world.TryGet<scene::ObjectName>(second)->value = "Second Player";
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
    world.Destroy(second);
    const auto wall = Object(world, "Wall", {1,0});
    world.Add<phys::Collider2D>(wall).shape = phys::Shape2D::MakeBox(.2f, 2);
    const auto trigger = Object(world, "Trigger", {.3f,0});
    auto& collider = world.Add<phys::Collider2D>(trigger);
    collider.isTrigger = true; collider.shape = phys::Shape2D::MakeBox(.15f,.2f);
    auto& behavior = world.Add<runtime::ObjectBehavior>(trigger);
    behavior.connections.push_back({runtime::ObjectEvent::TriggerEnter, runtime::ObjectAction::Message, {}, "entered"});
    behavior.connections.push_back({runtime::ObjectEvent::TriggerExit, runtime::ObjectAction::Message, {}, "left"});
    const auto sign = Object(world, "Sign", {0,-.2f});
    world.Add<runtime::InteractionTarget>(sign).radius = 2;
    world.Add<runtime::ObjectBehavior>(sign).luaSource = R"lua(return {
        on_interact = function(self)
            local entity = mye.world.entity_from_packed(self.entity)
            entity:set_position(mye.Vec2(0, -0.5))
        end
    })lua";
    runtime::ObjectSystem system(world);
    MYE_EXPECT(system.Initialize());
    for (int i = 0; i < 5; ++i) MYE_EXPECT(system.Tick(.02f, {}, false));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, 0.0f, .001f);
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.y, 0.0f, .001f);
    for (int i = 0; i < 12; ++i) MYE_EXPECT(system.Tick(.02f, {1,0}, false));
    MYE_EXPECT(system.Message() == "entered");
    for (int i = 0; i < 50; ++i) MYE_EXPECT(system.Tick(.02f, {1,0}, false));
    MYE_EXPECT(world.TryGet<scene::LocalTransform>(player)->position.x < .71f);
    MYE_EXPECT(system.Message() == "left");
    MYE_EXPECT(system.Tick(.02f, {}, true));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(sign)->position.y, -.5f, .001f);
    MYE_EXPECT(!system.Prompt().empty());
}

MYE_TEST(ObjectTickPropagatesPhysicsFailureBeforeMovementCommit) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto player = Player(world);
    runtime::ObjectSystem system(world);
    MYE_EXPECT(system.Initialize());
    world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 0;
    auto failed = system.Tick(.02f, {1, 0}, false);
    MYE_EXPECT(!failed);
    if (!failed) MYE_EXPECT(failed.GetError().message.find("1..16 slides") != std::string::npos);
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, 0, .001f);
    world.TryGet<phys::KinematicBody2D>(player)->maxSlideIters = 4;
    MYE_EXPECT(system.Tick(.02f, {1, 0}, false));
    MYE_EXPECT_NEAR(world.TryGet<scene::LocalTransform>(player)->position.x, .04f, .001f);
}

MYE_TEST(ObjectSceneRoundTripRejectsInvalidEventsAndDuplicateTargets) {
    editor::Document document({1}, editor::Document::Kind::Scene, "");
    auto& world = document.World();
    const auto object = Object(world, "Sign");
    auto& behavior = world.Add<runtime::ObjectBehavior>(object);
    behavior.connections.push_back({runtime::ObjectEvent::Interact, runtime::ObjectAction::Message, {}, "welcome"});
    behavior.luaSource = "return {}";
    auto data = scene::SceneSerializer{}.WriteWorld(world);
    MYE_EXPECT(data);
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    editor::EditorContext context;
    context.playMode = &play; context.commands = &document.Commands();
    document.Commands().SetContext(&context);
    auto edit = behavior;
    edit.connections[0].text = "edited";
    const auto* type = refl::GetType<runtime::ObjectBehavior>();
    auto before = ser::JsonArchive::ForWrite(), after = ser::JsonArchive::ForWrite();
    MYE_EXPECT(refl::ReadValue(*type, &behavior, {}, before));
    MYE_EXPECT(refl::ReadValue(*type, &edit, {}, after));
    document.Commands().Push(std::make_unique<editor::PropertyEditCommand>(editor::ObjectRef::Component(object, *type),
        refl::PropertyPath{}, editor::ValueBlob{json::Stringify(before.Root())}, editor::ValueBlob{json::Stringify(after.Root())}));
    MYE_EXPECT(behavior.connections[0].text == "edited");
    document.Commands().Undo();
    MYE_EXPECT(behavior.connections[0].text == "welcome" && behavior.luaSource == "return {}");
    editor::Document restored({2}, editor::Document::Kind::Scene, "");
    MYE_EXPECT(scene::SceneSerializer{}.ReadInto(restored.World(), data.Value()));
    restored.World().Query<runtime::ObjectBehavior>().Each([&](ecs::Entity, const auto& b) {
        MYE_EXPECT(b.connections.size() == 1 && b.connections[0].text == "welcome" && b.luaSource == "return {}");
    });
    auto text = json::Stringify(data.Value());
    const auto pos = text.find("Interact");
    MYE_EXPECT(pos != std::string::npos);
    if (pos == std::string::npos) return;
    text.replace(pos, 8, "WrongEvent");
    auto invalid = json::Parse(text);
    MYE_EXPECT(invalid);
    MYE_EXPECT(!scene::SceneSerializer{}.ReadInto(restored.World(), invalid.Value()));
    auto oversized = json::Parse(R"json({"__version":1,"entities":[{"id":1,"components":{"ObjectBehavior":{"__version":1,"connections":[{"__version":1,"x":1e100}]}}}]})json");
    MYE_EXPECT(oversized);
    if (oversized) MYE_EXPECT(!scene::SceneSerializer{}.ReadInto(restored.World(), oversized.Value()));
    Object(world, "Sign");
    MYE_EXPECT(!runtime::ValidateObjectComponents(world));
}

MYE_TEST(ObjectMapTransitionPreservesEditWorldAndProgressionAndRejectsMissingMaps) {
    const auto root = FreshObjectRoot();
    std::filesystem::create_directories(root / "assets/scenes");
    editor::Document source({1}, editor::Document::Kind::Scene, "");
    auto& world = source.World();
    Player(world);
    const auto door = Object(world, "Door");
    world.Add<runtime::InteractionTarget>(door);
    auto& portal = world.Add<runtime::ScenePortal>(door);
    portal.scenePath = "assets/scenes/destination.scene"; portal.spawnName = "Spawn";
    editor::Document target({2}, editor::Document::Kind::Scene, "");
    Player(target.World());
    Object(target.World(), "Spawn", {4,2});
    MYE_EXPECT(scene::SceneSerializer{}.SaveToFile(target.World(), Utf8String(root / portal.scenePath)));
    editor::PlayModeController play;
    play.SetEditWorld(&world);
    MYE_EXPECT(play.Play());
    auto* old = play.ActiveWorld();
    MYE_EXPECT(play.Tick(.02f, {}, true, Utf8String(root)));
    for (int i = 0; i < 30; ++i) MYE_EXPECT(play.Tick(.02f, {}, false, Utf8String(root)));
    MYE_EXPECT(play.ActiveWorld() != old);
    play.ActiveWorld()->Query<runtime::CharacterController2D, scene::LocalTransform, gameplay::Progression>().Each(
        [&](ecs::Entity, const auto&, const auto& t, const auto& p) { MYE_EXPECT_NEAR(t.position.x, 4.0f, .001f); MYE_EXPECT(p.level == 7); });
    play.Stop();
    MYE_EXPECT(play.ActiveWorld() == &world);
    portal.scenePath = "assets/scenes/missing.scene";
    MYE_EXPECT(play.Play()); old = play.ActiveWorld();
    MYE_EXPECT(play.Tick(.02f, {}, true, Utf8String(root)));
    bool failed = false;
    for (int i = 0; i < 30; ++i) if (!play.Tick(.02f, {}, false, Utf8String(root))) failed = true;
    MYE_EXPECT(failed && play.ActiveWorld() == old);
    play.Stop();
}

MYE_TEST(ViewportPerspectiveProjectsWorldZAndRoundTripsTheEditingPlane) {
    for (bool perspective : {false, true}) {
        editor::ViewportCamera camera;
        camera.perspective = perspective; camera.center = {2,-1};
        const Vec3 point{3,2,0};
        const auto pixel = editor::ProjectViewportPoint(camera, point, 960,540);
        const auto restored = editor::ViewportPointOnPlane(camera, pixel,960,540);
        MYE_EXPECT_NEAR(restored.x, point.x, .002f); MYE_EXPECT_NEAR(restored.y, point.y, .002f);
        if (perspective) {
            const auto displaced = editor::ProjectViewportPoint(camera, {point.x,point.y,2},960,540);
            MYE_EXPECT(std::abs(displaced.x - pixel.x) + std::abs(displaced.y - pixel.y) > 1);
        }
    }
}

MYE_TEST(ObjectInvalidSavePreservesTheOriginalSceneAndDirtyState) {
    editor::ProjectContext project;
    MYE_EXPECT(project.Create("Save validation", Utf8String(FreshObjectRoot())));
    auto* doc = project.Active();
    if (!doc) return;
    const auto original = ReadJsonFile(Utf8Path(doc->Path()));
    MYE_EXPECT(original);
    editor::PlayModeController play;
    play.SetEditWorld(&doc->World());
    editor::EditorContext context;
    context.playMode = &play; context.activeDocument = doc; context.commands = &doc->Commands();
    doc->Commands().SetContext(&context);
    auto create = std::make_unique<editor::CreateEntityCommand>();
    auto* created = create.get();
    doc->Commands().Push(std::move(create));
    doc->Commands().Push(std::make_unique<editor::AddComponentCommand>(created->Created(), *refl::GetType<runtime::ScenePortal>()));
    MYE_EXPECT(doc->IsDirty());
    MYE_EXPECT(!project.Save()); // Incomplete portal cannot make the next file open fail.
    MYE_EXPECT(doc->IsDirty());
    const auto retained = ReadJsonFile(Utf8Path(doc->Path()));
    MYE_EXPECT(retained && json::Stringify(retained.Value()) == json::Stringify(original.Value()));
}
