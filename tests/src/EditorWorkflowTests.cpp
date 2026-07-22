// EditorWorkflowTests.cpp — M4 완료기준 엔드투엔드 워크플로우 검증 (docs/00 §7 M4)
//
// M4 완료기준: 에디터에서 스프라이트·프랍·NPC 프리팹을 뷰포트에 배치 → 인스펙터로 값 수정 →
//   Ctrl+S 저장 → ▶Play 로 즉시 걸어보고 → ■Stop 으로 편집 상태 복귀. 모든 편집이 Ctrl+Z 로 되돌려짐.
//
// 이 테스트는 GUI 를 헤드리스로 대체해, 실제 에디터 진입점(InstantiateAssetToWorld 드롭 처리,
//   PropertyEditCommand 인스펙터 편집, SceneSerializer::SaveToFile 저장, PlayModeController Play/Stop,
//   CommandStack Undo)을 그대로 호출해 각 완료기준을 왕복 검증한다. 패널 위젯(ImGui)만 우회하고,
//   그 아래 커맨드/직렬화/플레이모드 경로는 프로덕션 코드 그대로다.
#include "TestFramework.h"

#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Selection.h"

#include "mye/core/Events.h"
#include "mye/core/Json.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Renderable.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace mye;
using namespace mye::editor;

// -----------------------------------------------------------------------------
// 테스트용 스프라이트 엔티티 컴포넌트 — 씬 저장이 왕복시키려면 리플렉션 이름 == 컴포넌트 이름
//   규약(refl::TypeId == ComponentTypeId)이 성립해야 한다. 실 SpriteRenderer 는 씬 모듈에서
//   리플렉션 미등록이라(M4-B 현황), 여기서는 저장 왕복을 검증할 수 있는 리플렉션 컴포넌트로
//   동등 시나리오를 구성한다. LocalTransform 도 저장 대상이 되도록 짧은 이름으로 리플렉션 등록.
// -----------------------------------------------------------------------------
namespace ewtest {
struct Sprite {
    MYE_COMPONENT(Sprite);
    std::uint64_t guid = 0;
    float         tintR = 1.0f;
};
struct Xform {   // LocalTransform 대체(저장 왕복용 위치 컴포넌트)
    MYE_COMPONENT(Xform);
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
} // namespace ewtest

MYE_REFLECT_NAME(ewtest::Sprite, "Sprite");
MYE_REFLECT_NAME(ewtest::Xform, "Xform");
namespace mye::refl {
template <> void Reflect<ewtest::Sprite>(TypeBuilder<ewtest::Sprite>& b);
template <> void Reflect<ewtest::Xform>(TypeBuilder<ewtest::Xform>& b);
}
template <> void mye::refl::Reflect(TypeBuilder<ewtest::Sprite>& b) {
    b.Version(1).Field("guid", &ewtest::Sprite::guid).Field("tintR", &ewtest::Sprite::tintR);
}
template <> void mye::refl::Reflect(TypeBuilder<ewtest::Xform>& b) {
    b.Version(1)
     .Field("x", &ewtest::Xform::x)
     .Field("y", &ewtest::Xform::y)
     .Field("z", &ewtest::Xform::z);
}

namespace {

template <typename C>
void RegisterPool(ecs::World& world) {
    world.RegisterComponent<C>(refl::GetType<C>()->Name().data());
}

// EditorContext 하네스(편집 World 하나 + 커맨드 스택 + 플레이모드).
struct WFHarness {
    EventBus          bus;
    ecs::World        world;
    SelectionManager  selection{&bus};
    PlayModeController playMode;
    CommandStack      commands{nullptr};   // ctx 는 아래에서 배선
    EditorContext     ctx{};

    WFHarness() : commands(&ctxRef()) {
        playMode.SetEditWorld(&world);
        playMode.SetEventBus(&bus);
        ctx.events = &bus;
        ctx.selection = &selection;
        ctx.playMode = &playMode;     // activeWorld() → Edit World(플레이 중엔 Play World)
        ctx.commands = &commands;
    }
    EditorContext& ctxRef() { return ctx; }
};

ValueBlob F32Blob(float v) {
    std::ostringstream os;
    os << v;
    return ValueBlob{os.str()};
}

std::string SlurpFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// -----------------------------------------------------------------------------
// (a) 배치 — 에셋 드롭(InstantiateAssetToWorld) 이 스프라이트 생성 커맨드를 발행, Undo 가능.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowPlaceSpriteViaDrop) {
    WFHarness h;
    // 실 드롭 경로: 뷰포트가 "MYE_ASSET" 페이로드(vpath) + 월드좌표로 이 함수를 호출한다.
    //   project·engine 이 없어도 스프라이트는 빈 AssetRef 로 생성된다(안전 fallback).
    InstantiateAssetToWorld(h.ctx, "assets://chars/hero.png", Vec2{3.0f, -2.0f});

    // 생성 확인: SpriteRenderer + LocalTransform 엔티티 1개.
    int spriteCount = 0;
    Vec3 pos{};
    h.world.Query<scene::SpriteRenderer, scene::LocalTransform>().Each(
        [&](ecs::Entity, scene::SpriteRenderer&, scene::LocalTransform& lt) {
            ++spriteCount;
            pos = lt.position;
        });
    MYE_EXPECT(spriteCount == 1);
    MYE_EXPECT(pos.x == 3.0f && pos.y == -2.0f);
    MYE_EXPECT(h.commands.CanUndo());

    // Undo — 배치 취소.
    h.commands.Undo();
    int after = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++after; });
    MYE_EXPECT(after == 0);

    // Redo — 다시 배치.
    h.commands.Redo();
    after = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++after; });
    MYE_EXPECT(after == 1);
}

// -----------------------------------------------------------------------------
// (b)+(e) 편집 — 인스펙터 값 변경(PropertyEditCommand) → Undo 왕복.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowEditPropertyAndUndo) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<ewtest::Xform>(e).x = 0.0f;

    const refl::TypeInfo* type = refl::GetType<ewtest::Xform>();
    ObjectRef target = ObjectRef::Component(e, *type);
    auto path = refl::PropertyPath::Parse("x");
    MYE_EXPECT((bool)path);

    // 인스펙터가 x 를 0 → 42 로 편집(PropertyEditCommand).
    h.commands.Push(std::make_unique<PropertyEditCommand>(
        target, path.Value(), F32Blob(0.0f), F32Blob(42.0f), "set x"));
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 42.0f);

    // Ctrl+Z 로 원복.
    h.commands.Undo();
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);
    h.commands.Redo();
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 42.0f);
}

// -----------------------------------------------------------------------------
// (c) 저장 — SaveActive 경로(SceneSerializer::SaveToFile) 가 씬 JSON 파일을 실제로 쓰고,
//   내용에 배치·편집 결과가 반영되는지 파일 텍스트로 검증.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowSaveSceneToFile) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    RegisterPool<ewtest::Sprite>(h.world);

    // 배치 + 편집: Xform(x=7) + Sprite(guid=123) 엔티티.
    ecs::Entity e = h.world.Create();
    auto& xf = h.world.Add<ewtest::Xform>(e);
    xf.x = 7.0f; xf.y = 8.0f;
    auto& sp = h.world.Add<ewtest::Sprite>(e);
    sp.guid = 123;

    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "mye_m4_wf";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path scenePath = dir / "untitled.scene";
    fs::remove(scenePath, ec);

    SceneSerializer ser;
    auto saved = ser.SaveToFile(h.world, scenePath.string());
    MYE_EXPECT((bool)saved);
    MYE_EXPECT(fs::exists(scenePath));

    // 파일 내용에 컴포넌트·값이 텍스트로 존재하는지 확인(JSON 씬).
    std::string text = SlurpFile(scenePath.string());
    MYE_EXPECT(!text.empty());
    MYE_EXPECT(text.find("Xform") != std::string::npos);
    MYE_EXPECT(text.find("Sprite") != std::string::npos);
    MYE_EXPECT(text.find("123") != std::string::npos);   // sprite guid
    MYE_EXPECT(text.find("entities") != std::string::npos);

    // 로드 왕복: 빈 World 로 다시 읽어 값 복원 확인.
    ecs::World loaded;
    RegisterPool<ewtest::Xform>(loaded);
    RegisterPool<ewtest::Sprite>(loaded);
    auto roots = ser.LoadFromFile(loaded, scenePath.string());
    MYE_EXPECT((bool)roots);
    int found = 0; float lx = -1.0f; std::uint64_t lg = 0;
    loaded.Query<ewtest::Xform, ewtest::Sprite>().Each(
        [&](ecs::Entity, ewtest::Xform& x, ewtest::Sprite& s) { ++found; lx = x.x; lg = s.guid; });
    MYE_EXPECT(found == 1);
    MYE_EXPECT(lx == 7.0f);
    MYE_EXPECT(lg == 123);

    fs::remove(scenePath, ec);
}

// -----------------------------------------------------------------------------
// (d) 플레이 — PlayMode Start → Play World 에서 tick 시뮬레이션(이동) → Stop → 편집 상태 원복.
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowPlayTickStopRestore) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<ewtest::Xform>(e).x = 0.0f;   // 편집 상태 위치.

    // ▶Play — 편집 World 스냅샷 → Play World.
    h.playMode.Play();
    ecs::World* pw = h.playMode.ActiveWorld();
    MYE_EXPECT(pw != nullptr && pw != &h.world);

    // Play World 를 tick(캐릭터 이동 시뮬레이션): x += 5 (게이팅된 시스템이 하는 일의 대역).
    int moved = 0;
    pw->Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { x.x += 5.0f; ++moved; });
    MYE_EXPECT(moved == 1);
    pw->Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { MYE_EXPECT(x.x == 5.0f); });

    // 편집 World 는 플레이 중 수정에 영향받지 않음.
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);

    // ■Stop — Play World 파기, 편집 상태 원복.
    h.playMode.Stop();
    MYE_EXPECT(h.playMode.State() == PlayState::Edit);
    MYE_EXPECT(h.playMode.ActiveWorld() == &h.world);
    MYE_EXPECT(h.world.TryGet<ewtest::Xform>(e)->x == 0.0f);   // 편집 상태 그대로.
}

// -----------------------------------------------------------------------------
// (e) 전체 편집 왕복 — 배치·값편집·삭제·재부모화가 모두 Undo 가능(단일 스택 순차 왕복).
// -----------------------------------------------------------------------------
MYE_TEST(WorkflowUndoAllEditKinds) {
    WFHarness h;
    RegisterPool<ewtest::Xform>(h.world);

    // 1) 배치(에셋 드롭 → 스프라이트 엔티티).
    InstantiateAssetToWorld(h.ctx, "assets://chars/hero.png", Vec2{0.0f, 0.0f});
    ecs::Entity sprite = ecs::Entity::Null();
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity en, scene::SpriteRenderer&) { sprite = en; });
    MYE_EXPECT(!sprite.IsNull());

    // 2) 부모 엔티티 생성(구조 커맨드).
    auto createParent = std::make_unique<CreateEntityCommand>();
    CreateEntityCommand* cpPtr = createParent.get();
    h.commands.Push(std::move(createParent));
    ecs::Entity parent = cpPtr->Created();
    MYE_EXPECT(!parent.IsNull());

    // 3) 재부모화(스프라이트를 parent 밑으로).
    h.commands.Push(std::make_unique<ReparentCommand>(sprite, parent, /*keepWorld*/ true));
    {
        scene::Parent* pc = h.world.TryGet<scene::Parent>(sprite);
        MYE_EXPECT(pc && pc->parent == parent);
    }

    // 4) 별도 엔티티 생성 후 삭제(구조 커맨드).
    ecs::Entity victim = h.world.Create();
    h.world.Add<ewtest::Xform>(victim).x = 99.0f;
    h.commands.Push(std::make_unique<DestroyEntityCommand>(victim));
    MYE_EXPECT(!h.world.Valid(victim));

    // ---- 전부 Ctrl+Z 로 역순 되돌리기 ----
    // Undo 4: 삭제 취소(victim 복원).
    h.commands.Undo();
    int restored = 0;
    h.world.Query<ewtest::Xform>().Each([&](ecs::Entity, ewtest::Xform& x) { if (x.x == 99.0f) ++restored; });
    MYE_EXPECT(restored == 1);

    // Undo 3: 재부모화 취소(루트로 복귀).
    h.commands.Undo();
    {
        scene::Parent* pc = h.world.TryGet<scene::Parent>(sprite);
        MYE_EXPECT(!pc || pc->parent.IsNull());
    }

    // Undo 2: 부모 생성 취소.
    h.commands.Undo();
    MYE_EXPECT(!h.world.Valid(parent));

    // Undo 1: 배치 취소(스프라이트 제거).
    h.commands.Undo();
    int sprites = 0;
    h.world.Query<scene::SpriteRenderer>().Each([&](ecs::Entity, scene::SpriteRenderer&) { ++sprites; });
    MYE_EXPECT(sprites == 0);
    MYE_EXPECT(!h.commands.CanUndo());   // 스택 소진.
}
