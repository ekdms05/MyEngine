// EditorTests.cpp — M4-A 에디터 셸·커맨드/Undo·선택·플레이모드 검증 (docs/07 §3·§4·§5·확장)
//
// 커버리지:
//   - CommandStack: push/execute·Undo/Redo·redo 파기·dirty(저장 지점)·상한·트랜잭션·병합
//   - PropertyEditCommand: 리플렉션 PropertyPath + old/new blob 왕복 Undo·연속 편집 병합
//   - 구조 커맨드: CreateEntity/DestroyEntity(서브트리 스냅샷 복원)/Reparent 왕복
//   - PlayModeController: 상태 머신·이벤트·스냅샷 왕복(Play 중 수정 → Stop → 편집 상태 원복)
//   - SelectionManager: 다중(Replace/Add/Toggle)·primary·이력·변경 이벤트
//   - EditorExtensionRegistry: 메뉴·툴바·드로어 등록·조회
//   - PanelManager: 팩토리 등록·오픈(단일 인스턴스)·레이아웃 직렬화
#include "TestFramework.h"

#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/ExtensionRegistry.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Panel.h"

#include "mye/core/Events.h"
#include "mye/core/Json.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/scene/Transform.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"

#include <memory>
#include <string>

using namespace mye;
using namespace mye::editor;

// -----------------------------------------------------------------------------
// 테스트용 컴포넌트 — 리플렉션 이름과 MYE_COMPONENT 이름을 일치시켜 ComponentTypeId ==
//   refl::TypeId 규약을 만족한다(SceneSerializer가 이 규약으로 컴포넌트를 선별한다).
// -----------------------------------------------------------------------------
namespace edtest {

struct Health {
    MYE_COMPONENT(Health);
    std::int32_t hp = 100;
    float        regen = 1.0f;
};

} // namespace edtest

// 리플렉션 이름을 컴포넌트 이름("Health")과 일치시킨다 — 이래야 refl::TypeId ==
//   ComponentTypeId(=HashFnv1a64("Health")) 규약이 성립해 World 풀 조회·SceneSerializer 선별이
//   맞아떨어진다(03/04 정본 승격 규약, SceneSerializer가 요구하는 동일 규약).
MYE_REFLECT_NAME(edtest::Health, "Health");
namespace mye::refl {
    template <> void Reflect<edtest::Health>(TypeBuilder<edtest::Health>& b);
}
template <> void mye::refl::Reflect(TypeBuilder<edtest::Health>& b) {
    b.Version(1)
     .Field("hp", &edtest::Health::hp)
     .Field("regen", &edtest::Health::regen);
}

namespace {

// World에 컴포넌트 타입 C의 완전한 풀(construct/destruct/move 포함)을 등록.
template <typename C>
void RegisterPool(ecs::World& world) {
    world.RegisterComponent<C>(refl::GetType<C>()->Name().data());
}

// EditorContext(World 하나·플레이모드 하나)를 구성하는 최소 하네스.
struct Harness {
    EventBus            bus;
    ecs::World          world;
    SelectionManager    selection{&bus};
    PlayModeController   playMode;
    EditorContext        ctx{};

    Harness() {
        playMode.SetEditWorld(&world);
        playMode.SetEventBus(&bus);
        ctx.events = &bus;
        ctx.selection = &selection;
        ctx.playMode = &playMode;   // activeWorld() → Edit World.
    }
};

// PropertyEditCommand용 값 blob(JSON 스칼라).
ValueBlob IntBlob(std::int32_t v) { return ValueBlob{std::to_string(v)}; }

} // namespace

// -----------------------------------------------------------------------------
// CommandStack — 기본 Undo/Redo·dirty·redo 파기
// -----------------------------------------------------------------------------

// 단순 카운터 커맨드(스택 자료구조 검증용).
namespace {
struct CounterCmd final : IEditorCommand {
    int* target;
    int  delta;
    std::string label;
    CounterCmd(int* t, int d, std::string l) : target(t), delta(d), label(std::move(l)) {}
    void Execute(EditorContext&) override { *target += delta; }
    void Undo(EditorContext&) override { *target -= delta; }
    std::string_view Label() const override { return label; }
};
} // namespace

MYE_TEST(CommandStackUndoRedo) {
    Harness h;
    CommandStack stack(&h.ctx);
    int value = 0;

    stack.Push(std::make_unique<CounterCmd>(&value, 5, "add5"));
    MYE_EXPECT(value == 5);
    stack.Push(std::make_unique<CounterCmd>(&value, 3, "add3"));
    MYE_EXPECT(value == 8);
    MYE_EXPECT(stack.CanUndo() && !stack.CanRedo());
    MYE_EXPECT(stack.UndoLabel() == std::string_view("add3"));

    stack.Undo();
    MYE_EXPECT(value == 5);
    MYE_EXPECT(stack.CanRedo());
    MYE_EXPECT(stack.RedoLabel() == std::string_view("add3"));

    stack.Redo();
    MYE_EXPECT(value == 8);

    // Undo 후 새 push → redo 가지 파기.
    stack.Undo();                 // value=5
    stack.Push(std::make_unique<CounterCmd>(&value, 100, "add100"));  // value=105
    MYE_EXPECT(value == 105);
    MYE_EXPECT(!stack.CanRedo());
}

MYE_TEST(CommandStackDirtyAndSavePoint) {
    Harness h;
    CommandStack stack(&h.ctx);
    int value = 0;
    MYE_EXPECT(!stack.IsDirty());

    stack.Push(std::make_unique<CounterCmd>(&value, 1, "a"));
    MYE_EXPECT(stack.IsDirty());

    stack.MarkSaved();
    MYE_EXPECT(!stack.IsDirty());

    stack.Push(std::make_unique<CounterCmd>(&value, 1, "b"));
    MYE_EXPECT(stack.IsDirty());
    stack.Undo();
    MYE_EXPECT(!stack.IsDirty());   // 저장 지점으로 복귀.
}

MYE_TEST(CommandStackLimit) {
    Harness h;
    CommandStack stack(&h.ctx, /*limit*/ 4);
    int value = 0;
    for (int i = 0; i < 10; ++i)
        stack.Push(std::make_unique<CounterCmd>(&value, 1, "inc"));
    MYE_EXPECT(value == 10);
    // 상한 4 — 오래된 것부터 파기되어 Undo는 최대 4회.
    int undos = 0;
    while (stack.CanUndo()) { stack.Undo(); ++undos; }
    MYE_EXPECT(undos == 4);
    MYE_EXPECT(value == 6);   // 4회만 되돌려짐.
}

MYE_TEST(CommandStackTransaction) {
    Harness h;
    CommandStack stack(&h.ctx);
    int value = 0;

    stack.BeginTransaction("multi");
    stack.Push(std::make_unique<CounterCmd>(&value, 1, "a"));
    stack.Push(std::make_unique<CounterCmd>(&value, 2, "b"));
    stack.Push(std::make_unique<CounterCmd>(&value, 4, "c"));
    stack.EndTransaction();
    MYE_EXPECT(value == 7);

    // 트랜잭션 전체가 Undo 1회로 되돌려진다.
    MYE_EXPECT(stack.CanUndo());
    stack.Undo();
    MYE_EXPECT(value == 0);
    MYE_EXPECT(!stack.CanUndo());

    // Redo도 1회로 전체 재적용.
    stack.Redo();
    MYE_EXPECT(value == 7);
}

// -----------------------------------------------------------------------------
// PropertyEditCommand — 리플렉션 blob 왕복 + 병합
// -----------------------------------------------------------------------------
MYE_TEST(PropertyEditCommandRoundtrip) {
    Harness h;
    RegisterPool<edtest::Health>(h.world);
    ecs::Entity e = h.world.Create();
    edtest::Health& comp = h.world.Add<edtest::Health>(e);
    comp.hp = 100;

    const refl::TypeInfo* type = refl::GetType<edtest::Health>();
    ObjectRef target = ObjectRef::Component(e, *type);
    auto path = refl::PropertyPath::Parse("hp");
    MYE_EXPECT((bool)path);

    CommandStack stack(&h.ctx);
    stack.Push(std::make_unique<PropertyEditCommand>(target, path.Value(),
                                                     IntBlob(100), IntBlob(250), "set hp"));
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 250);

    stack.Undo();
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 100);

    stack.Redo();
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 250);
}

MYE_TEST(PropertyEditCommandMerge) {
    Harness h;
    RegisterPool<edtest::Health>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<edtest::Health>(e).hp = 0;

    const refl::TypeInfo* type = refl::GetType<edtest::Health>();
    ObjectRef target = ObjectRef::Component(e, *type);
    auto path = refl::PropertyPath::Parse("hp").Value();

    CommandStack stack(&h.ctx);
    // 연속 드래그 편집(같은 target/path, 병합 창 이내) — 하나로 병합되어 Undo 1회로 원복.
    stack.Push(std::make_unique<PropertyEditCommand>(target, path, IntBlob(0), IntBlob(10), "drag"));
    stack.Push(std::make_unique<PropertyEditCommand>(target, path, IntBlob(10), IntBlob(20), "drag"));
    stack.Push(std::make_unique<PropertyEditCommand>(target, path, IntBlob(20), IntBlob(30), "drag"));
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 30);

    // 병합되었으므로 Undo 한 번에 최초 값(0)으로.
    stack.Undo();
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 0);
    MYE_EXPECT(!stack.CanUndo());
}

// -----------------------------------------------------------------------------
// 구조 커맨드 — Create/Destroy/Reparent 왕복
// -----------------------------------------------------------------------------
MYE_TEST(CreateEntityCommandRoundtrip) {
    Harness h;
    CommandStack stack(&h.ctx);

    stack.Push(std::make_unique<CreateEntityCommand>());
    // 생성 직후 유효 엔티티 1개(LocalTransform 부여됨).
    int count = 0;
    h.world.Query<scene::LocalTransform>().Each([&](ecs::Entity, scene::LocalTransform&) { ++count; });
    MYE_EXPECT(count == 1);

    stack.Undo();
    count = 0;
    h.world.Query<scene::LocalTransform>().Each([&](ecs::Entity, scene::LocalTransform&) { ++count; });
    MYE_EXPECT(count == 0);

    stack.Redo();
    count = 0;
    h.world.Query<scene::LocalTransform>().Each([&](ecs::Entity, scene::LocalTransform&) { ++count; });
    MYE_EXPECT(count == 1);
}

MYE_TEST(DestroyEntityCommandRoundtrip) {
    Harness h;
    RegisterPool<edtest::Health>(h.world);
    // 씬 직렬화가 LocalTransform도 다루도록 풀 등록(리플렉션은 아래에서 별도 등록 불필요 —
    //   Health만으로 컴포넌트 복원 왕복을 검증한다).
    ecs::Entity e = h.world.Create();
    h.world.Add<edtest::Health>(e).hp = 77;

    CommandStack stack(&h.ctx);
    stack.Push(std::make_unique<DestroyEntityCommand>(e));
    MYE_EXPECT(!h.world.Valid(e));   // 파괴됨.

    // Undo — 서브트리 스냅샷에서 복원. 새 핸들에 Health(hp=77) 재구성.
    stack.Undo();
    int restored = 0; int hp = -1;
    h.world.Query<edtest::Health>().Each([&](ecs::Entity, edtest::Health& c) { ++restored; hp = c.hp; });
    MYE_EXPECT(restored == 1);
    MYE_EXPECT(hp == 77);
}

MYE_TEST(ReparentCommandRoundtrip) {
    Harness h;
    ecs::Entity parent = h.world.Create();
    ecs::Entity child = h.world.Create();
    h.world.Add<scene::LocalTransform>(parent);
    h.world.Add<scene::LocalTransform>(child);

    CommandStack stack(&h.ctx);
    stack.Push(std::make_unique<ReparentCommand>(child, parent, /*keepWorld*/ true));

    scene::Parent* pc = h.world.TryGet<scene::Parent>(child);
    MYE_EXPECT(pc && pc->parent == parent);

    stack.Undo();
    pc = h.world.TryGet<scene::Parent>(child);
    MYE_EXPECT(!pc || pc->parent.IsNull());   // 루트로 복귀.

    stack.Redo();
    pc = h.world.TryGet<scene::Parent>(child);
    MYE_EXPECT(pc && pc->parent == parent);
}

// -----------------------------------------------------------------------------
// PlayModeController — 상태 머신·이벤트·스냅샷 왕복
// -----------------------------------------------------------------------------
MYE_TEST(PlayModeStateMachine) {
    Harness h;
    int changes = 0;
    PlayState last = PlayState::Edit;
    ScopedSubscription sub(h.bus, h.bus.Subscribe<PlayStateChangedEvent>(
        [&](const PlayStateChangedEvent& ev) { ++changes; last = ev.current; return false; }));

    MYE_EXPECT(h.playMode.State() == PlayState::Edit);
    h.playMode.Play();
    MYE_EXPECT(h.playMode.State() == PlayState::Playing);
    MYE_EXPECT(last == PlayState::Playing);

    h.playMode.Pause();
    MYE_EXPECT(h.playMode.State() == PlayState::Paused);
    h.playMode.Resume();
    MYE_EXPECT(h.playMode.State() == PlayState::Playing);

    h.playMode.Stop();
    MYE_EXPECT(h.playMode.State() == PlayState::Edit);
    MYE_EXPECT(changes == 4);   // Playing·Paused·Playing·Edit.

    // Play 중 별도 Undo 스택 존재, Stop 후 파기.
    h.playMode.Play();
    MYE_EXPECT(h.playMode.PlayCommandStack() != nullptr);
    h.playMode.Stop();
    MYE_EXPECT(h.playMode.PlayCommandStack() == nullptr);
}

MYE_TEST(PlayModeSnapshotRoundtrip) {
    Harness h;
    RegisterPool<edtest::Health>(h.world);
    ecs::Entity e = h.world.Create();
    h.world.Add<edtest::Health>(e).hp = 100;

    // Play — 편집 World 스냅샷 → Play World. ActiveWorld는 이제 Play World.
    h.playMode.Play();
    ecs::World* playWorld = h.playMode.ActiveWorld();
    MYE_EXPECT(playWorld != nullptr && playWorld != &h.world);

    // Play World에서 값 수정(휘발 대상).
    int playCount = 0;
    playWorld->Query<edtest::Health>().Each([&](ecs::Entity pe, edtest::Health& c) {
        ++playCount; c.hp = 999; (void)pe;
    });
    MYE_EXPECT(playCount == 1);   // 스냅샷이 엔티티+Health를 왕복 복원.

    // 편집 World는 보존(플레이 중 수정은 Play World에만).
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 100);

    // Stop — Play World 파기, 편집 World 재표시(편집 상태 원복).
    h.playMode.Stop();
    MYE_EXPECT(h.playMode.ActiveWorld() == &h.world);
    MYE_EXPECT(h.world.TryGet<edtest::Health>(e)->hp == 100);   // 편집 상태 그대로.
}

// -----------------------------------------------------------------------------
// SelectionManager — 다중·primary·이력·이벤트
// -----------------------------------------------------------------------------
MYE_TEST(SelectionMultiAndPrimary) {
    EventBus bus;
    SelectionManager sel(&bus);
    int events = 0;
    ScopedSubscription sub(bus, bus.Subscribe<SelectionChangedEvent>(
        [&](const SelectionChangedEvent&) { ++events; return false; }));

    SelectableRef a{SelectableKind::Entity, 1};
    SelectableRef b{SelectableKind::Entity, 2};
    SelectableRef c{SelectableKind::Entity, 3};

    sel.Select(a, SelectMode::Replace);
    MYE_EXPECT(sel.Current().size() == 1 && sel.Primary() == a);

    sel.Select(b, SelectMode::Add);
    MYE_EXPECT(sel.Current().size() == 2 && sel.Primary() == b);
    MYE_EXPECT(sel.IsSelected(a) && sel.IsSelected(b));

    sel.Select(b, SelectMode::Toggle);   // b 해제.
    MYE_EXPECT(sel.Current().size() == 1 && !sel.IsSelected(b));

    sel.Select(c, SelectMode::Replace);
    MYE_EXPECT(sel.Current().size() == 1 && sel.Primary() == c);

    MYE_EXPECT(events == 4);
}

MYE_TEST(SelectionHistory) {
    EventBus bus;
    SelectionManager sel(&bus);
    SelectableRef a{SelectableKind::Entity, 1};
    SelectableRef b{SelectableKind::Entity, 2};

    sel.Select(a, SelectMode::Replace);
    sel.Select(b, SelectMode::Replace);
    MYE_EXPECT(sel.Primary() == b);
    MYE_EXPECT(sel.CanNavigateBack());

    sel.NavigateBack();
    MYE_EXPECT(sel.Primary() == a);
    MYE_EXPECT(sel.CanNavigateForward());

    sel.NavigateForward();
    MYE_EXPECT(sel.Primary() == b);
}

// -----------------------------------------------------------------------------
// EditorExtensionRegistry — 메뉴·툴바·드로어 등록·조회
// -----------------------------------------------------------------------------
MYE_TEST(ExtensionRegistryMenusAndToolbars) {
    EditorExtensionRegistry reg;
    int clicked = 0;
    reg.AddMenuItem(MenuPath{"Tools/My Plugin/Do Thing"}, MenuItemDesc{},
                    [&](EditorContext&) { ++clicked; });
    reg.AddToolbarButton(ToolbarSection::Custom, ToolbarButtonDesc{"⚡", "run", nullptr, nullptr},
                         [&](EditorContext&) { ++clicked; });

    MYE_EXPECT(reg.MenuEntries().size() == 1);
    MYE_EXPECT(reg.ToolbarEntries().size() == 1);
    MYE_EXPECT(reg.MenuEntries()[0].path.path == "Tools/My Plugin/Do Thing");

    // 콜백 실행 검증.
    EditorContext ctx{};
    reg.MenuEntries()[0].onClick(ctx);
    reg.ToolbarEntries()[0].onClick(ctx);
    MYE_EXPECT(clicked == 2);
}

namespace {
struct DummyDrawer final : IPropertyDrawer {
    bool OnGui(PropertyDrawContext&) override { return false; }
};
} // namespace

MYE_TEST(ExtensionRegistryDrawerLookup) {
    EditorExtensionRegistry reg;
    const refl::TypeInfo* type = refl::GetType<edtest::Health>();
    MYE_EXPECT(type != nullptr);
    MYE_EXPECT(reg.FindPropertyDrawer(*type) == nullptr);   // 미등록 → nullptr.

    reg.AddPropertyDrawer(*type, std::make_unique<DummyDrawer>());
    MYE_EXPECT(reg.FindPropertyDrawer(*type) != nullptr);   // 등록 후 조회 성공.
}

// -----------------------------------------------------------------------------
// PanelManager — 팩토리 등록·단일 인스턴스 오픈·레이아웃 직렬화
// -----------------------------------------------------------------------------
namespace {
const PanelDesc kTestPanelDesc{"test.panel", "Test", /*allowMultiple*/ false, DockSlot::Left};
struct TestPanel final : IEditorPanel {
    const PanelDesc& Desc() const override { return kTestPanelDesc; }
    void OnGui(EditorContext&) override {}
};
struct TestPanelFactory final : IEditorPanelFactory {
    const PanelDesc& Desc() const override { return kTestPanelDesc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<TestPanel>(); }
};
} // namespace

MYE_TEST(PanelManagerRegisterAndOpen) {
    PanelManager pm;
    pm.RegisterFactory(std::make_unique<TestPanelFactory>());
    MYE_EXPECT(pm.RegisteredPanels().size() == 1);
    MYE_EXPECT(!pm.IsOpen("test.panel"));

    PanelInstanceId a = pm.Open("test.panel");
    MYE_EXPECT(a.IsValid());
    MYE_EXPECT(pm.IsOpen("test.panel"));

    // 단일 인스턴스 — 재오픈 시 기존 핸들 반환.
    PanelInstanceId b = pm.Open("test.panel");
    MYE_EXPECT(a == b);

    // 레이아웃 직렬화 → 열린 패널 목록 왕복.
    json::Value layout;
    pm.SerializeLayout(layout);
    const json::Value* panels = layout.Find("panels");
    MYE_EXPECT(panels && panels->IsArray() && panels->AsArray().size() == 1);

    pm.Close(a);
    MYE_EXPECT(!pm.IsOpen("test.panel"));
}
