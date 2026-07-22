// EditorSerializationTests.cpp — 씬/프리팹 직렬화 + 인스펙터 값 blob 검증 (M4-A, docs/07·04·03)
//
// 커버리지:
//   - SceneSerializer 왕복: 컴포넌트 값(프리미티브·enum·Vec·중첩·vector·AssetRef) 보존
//   - Transform 계층(Parent) 로컬 ID 왕복 재결선
//   - Entity 참조 필드(EntityRef)의 로컬 ID 안정 왕복
//   - 파일 저장/로드 왕복
//   - 프리팹 캡처·인스턴스화(+ PropertyPath 오버라이드)
//   - 인스펙터 값 blob: PropertyPath 리프 read/write(인스펙터 ReadLeafBlob·PropertyEditCommand 근거)
#include "TestFramework.h"

#include "mye/editor/SceneSerializer.h"
#include "mye/editor/Prefab.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"

#include "mye/ecs/World.h"
#include "mye/ecs/ComponentType.h"
#include "mye/scene/Transform.h"
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/refl/PropertyPath.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"
#include "mye/asset/AssetGuid.h"
#include "mye/core/Json.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace mye;

// ---------------------------------------------------------------------------
// 테스트용 컴포넌트 — MYE_COMPONENT 이름과 리플렉션 이름을 동일 문자열로 맞춰
//   ComponentTypeId(HashFnv1a64(unqualified)) == refl::TypeId(HashFnv1a64(리플렉션명)) 정합.
//   (SceneSerializer는 refl 이름 해시로 ComponentTypeId를 유도한다 — 등록 규약.)
// ---------------------------------------------------------------------------
namespace estest {

enum class Team : std::int32_t { Red = 0, Blue = 1, Green = 2 };

struct Stat { std::int32_t hp = 10; float speed = 1.0f; };

struct Unit {
    MYE_COMPONENT(Unit);
    std::string               name;
    Team                      team = Team::Red;
    Stat                      stat;             // 중첩 struct
    std::vector<std::int32_t> tags;             // vector
    bool                      alive = true;
    mye::asset::AssetRef      icon;             // AssetRef
    float                     posX = 0.0f;
};

// EntityRef를 담는 컴포넌트(직렬화 시 로컬 ID로 안정화되는지 검증).
struct Link {
    MYE_COMPONENT(Link);
    mye::ecs::Entity target = mye::ecs::Entity::Null();
};

} // namespace estest

// 리플렉션 이름을 컴포넌트 이름(MYE_COMPONENT의 unqualified)과 동일하게 부여한다 —
//   SceneSerializer가 ComponentTypeId를 refl 이름 해시로 유도하므로, 두 해시가 일치해야
//   World 등록 컴포넌트로 인식된다(등록 규약). MYE_REFLECT는 정규화 이름(#Type)을 쓰므로
//   여기서는 MYE_REFLECT_NAME + 수동 Reflect 선언으로 짧은 이름을 준다.
MYE_REFLECT_NAME(estest::Stat, "Stat");
MYE_REFLECT_NAME(estest::Unit, "Unit");
MYE_REFLECT_NAME(estest::Link, "Link");
namespace mye::refl {
template <> void Reflect<estest::Stat>(TypeBuilder<estest::Stat>& b);
template <> void Reflect<estest::Unit>(TypeBuilder<estest::Unit>& b);
template <> void Reflect<estest::Link>(TypeBuilder<estest::Link>& b);
}
MYE_REFLECT_ENUM(estest::Team);
// EntityRef 규약: refl 이름 "Entity"(SceneSerializer가 EntityRef로 인식하는 이름 중 하나).
MYE_REFLECT_NAME(mye::ecs::Entity, "Entity");
namespace mye::refl {
template <> void Reflect<mye::ecs::Entity>(TypeBuilder<mye::ecs::Entity>& b);
}

namespace mye::refl {
template <> void Reflect(TypeBuilder<estest::Stat>& b) {
    b.Version(1).Field("hp", &estest::Stat::hp).Field("speed", &estest::Stat::speed);
}
template <> void Reflect(TypeBuilder<estest::Unit>& b) {
    b.Version(1)
     .Field("name", &estest::Unit::name)
     .Field("team", &estest::Unit::team)
     .Field("stat", &estest::Unit::stat)
     .Field("tags", &estest::Unit::tags)
     .Field("alive", &estest::Unit::alive)
     .Field("icon", &estest::Unit::icon)
     .Field("posX", &estest::Unit::posX);
}
template <> void Reflect(TypeBuilder<estest::Link>& b) {
    b.Version(1).Field("target", &estest::Link::target);
}
template <> void Reflect(TypeBuilder<mye::ecs::Entity>& b) {
    // index=로컬 ID(직렬화 시 치환), generation=0. SceneSerializer RelinkEntityRefs가 재결선.
    b.Version(1)
     .Field("index", &mye::ecs::Entity::index)
     .Field("generation", &mye::ecs::Entity::generation);
}
template <> void Reflect(EnumBuilder<estest::Team>& b) {
    b.Value("Red", estest::Team::Red).Value("Blue", estest::Team::Blue)
     .Value("Green", estest::Team::Green);
}
} // namespace mye::refl

// ---- 헬퍼: 테스트 컴포넌트를 World에 리플렉션 포함 등록 ----
namespace {

template <typename C>
void RegisterReflected(ecs::World& w, const char* name) {
    ecs::ComponentTypeDesc d = ecs::MakeComponentTypeDesc<C>(name);
    d.reflection = refl::GetType<C>();
    w.RegisterComponent(d);
}

void RegisterAll(ecs::World& w) {
    RegisterReflected<estest::Unit>(w, "Unit");
    RegisterReflected<estest::Link>(w, "Link");
    // 계층·캐시 컴포넌트(Parent/Children) — SceneSerializer가 직접(특수) 취급하므로 리플렉션 불요.
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::Parent>("Parent"));
    w.RegisterComponent(ecs::MakeComponentTypeDesc<scene::Children>("Children"));
}

} // namespace

// -----------------------------------------------------------------------------
// 씬 직렬화 왕복 — 값·enum·Vec/중첩/vector/AssetRef 보존
// -----------------------------------------------------------------------------
MYE_TEST(SceneSerializerRoundtripValues) {
    ecs::World src;
    RegisterAll(src);

    ecs::Entity e = src.Create();
    auto& u = src.Add<estest::Unit>(e);
    u.name = "hero";
    u.team = estest::Team::Green;
    u.stat.hp = 77;
    u.stat.speed = 3.25f;
    u.tags = {1, 2, 3};
    u.alive = false;
    u.icon.guid = asset::AssetGuid{0xAABBCCDDEEFF0011ull, 0x2233445566778899ull};
    u.icon.type = refl::TypeIdFromName("Texture");
    u.posX = 12.5f;

    editor::SceneSerializer ser;
    auto tree = ser.WriteWorld(src);
    MYE_EXPECT((bool)tree);

    // 텍스트 왕복.
    std::string text = json::Stringify(tree.Value());
    auto parsed = json::Parse(text);
    MYE_EXPECT((bool)parsed);

    ecs::World dst;
    RegisterAll(dst);
    auto roots = ser.ReadInto(dst, parsed.Value());
    MYE_EXPECT((bool)roots);
    MYE_EXPECT(roots.Value().size() == 1);

    ecs::Entity de = roots.Value().front();
    auto* du = dst.TryGet<estest::Unit>(de);
    MYE_EXPECT(du != nullptr);
    if (du) {
        MYE_EXPECT(du->name == "hero");
        MYE_EXPECT(du->team == estest::Team::Green);
        MYE_EXPECT(du->stat.hp == 77);
        MYE_EXPECT_NEAR(du->stat.speed, 3.25f, 1e-6f);
        MYE_EXPECT(du->tags.size() == 3 && du->tags[0] == 1 && du->tags[2] == 3);
        MYE_EXPECT(du->alive == false);
        MYE_EXPECT(du->icon.guid.hi == 0xAABBCCDDEEFF0011ull);
        MYE_EXPECT(du->icon.guid.lo == 0x2233445566778899ull);
        MYE_EXPECT(du->icon.type == refl::TypeIdFromName("Texture"));
        MYE_EXPECT_NEAR(du->posX, 12.5f, 1e-6f);
    }
}

// -----------------------------------------------------------------------------
// 계층(Parent) 왕복 — 로컬 ID로 부모 재결선
// -----------------------------------------------------------------------------
MYE_TEST(SceneSerializerHierarchy) {
    ecs::World src;
    RegisterAll(src);

    ecs::Entity parent = src.Create();
    src.Add<estest::Unit>(parent).name = "parent";
    ecs::Entity child = src.Create();
    src.Add<estest::Unit>(child).name = "child";
    src.Add<scene::Parent>(child).parent = parent;

    editor::SceneSerializer ser;
    auto tree = ser.WriteWorld(src);
    MYE_EXPECT((bool)tree);

    ecs::World dst;
    RegisterAll(dst);
    auto roots = ser.ReadInto(dst, tree.Value());
    MYE_EXPECT((bool)roots);
    // 루트는 부모 1개(child는 parent를 가지므로 루트 아님).
    MYE_EXPECT(roots.Value().size() == 1);

    // 부모/자식 이름으로 대응 확인 + 자식의 Parent가 유효 부모를 가리키는지.
    ecs::Entity dParent = ecs::Entity::Null(), dChild = ecs::Entity::Null();
    // 로드된 엔티티는 루트(부모)만 반환되므로, 자식은 Parent 참조로 역추적.
    dParent = roots.Value().front();
    auto* pu = dst.TryGet<estest::Unit>(dParent);
    MYE_EXPECT(pu && pu->name == "parent");

    // 자식 찾기: parent를 가리키는 Parent를 가진 엔티티.
    //   (직접 열거 API가 없으므로 새 Create로 index를 알던 두 엔티티 index 0/1을 검사.)
    bool foundChild = false;
    for (std::uint32_t idx = 0; idx < 8; ++idx) {
        ecs::Entity cand{idx, dst.EntityGeneration(idx)};
        if (!dst.Valid(cand)) continue;
        auto* p = dst.TryGet<scene::Parent>(cand);
        auto* cu = dst.TryGet<estest::Unit>(cand);
        if (p && cu && cu->name == "child") {
            foundChild = true;
            MYE_EXPECT(dst.Valid(p->parent));
            MYE_EXPECT(p->parent == dParent);
        }
    }
    MYE_EXPECT(foundChild);
}

// -----------------------------------------------------------------------------
// Entity 참조 필드(EntityRef) 로컬 ID 안정 왕복
// -----------------------------------------------------------------------------
MYE_TEST(SceneSerializerEntityRef) {
    ecs::World src;
    RegisterAll(src);

    ecs::Entity a = src.Create();
    src.Add<estest::Unit>(a).name = "A";
    ecs::Entity b = src.Create();
    src.Add<estest::Unit>(b).name = "B";
    src.Add<estest::Link>(b).target = a;   // B가 A를 참조.

    editor::SceneSerializer ser;
    auto tree = ser.WriteWorld(src);
    MYE_EXPECT((bool)tree);

    ecs::World dst;
    RegisterAll(dst);
    auto roots = ser.ReadInto(dst, tree.Value());
    MYE_EXPECT((bool)roots);

    // B의 Link.target이 로드된 A를 정확히 가리키는지.
    ecs::Entity dA = ecs::Entity::Null(), dB = ecs::Entity::Null();
    for (std::uint32_t idx = 0; idx < 8; ++idx) {
        ecs::Entity cand{idx, dst.EntityGeneration(idx)};
        if (!dst.Valid(cand)) continue;
        auto* u = dst.TryGet<estest::Unit>(cand);
        if (!u) continue;
        if (u->name == "A") dA = cand;
        if (u->name == "B") dB = cand;
    }
    MYE_EXPECT(!dA.IsNull() && !dB.IsNull());
    auto* link = dst.TryGet<estest::Link>(dB);
    MYE_EXPECT(link != nullptr);
    if (link) {
        MYE_EXPECT(dst.Valid(link->target));
        MYE_EXPECT(link->target == dA);
    }
}

// -----------------------------------------------------------------------------
// 파일 저장/로드 왕복
// -----------------------------------------------------------------------------
MYE_TEST(SceneSerializerFileRoundtrip) {
    ecs::World src;
    RegisterAll(src);
    ecs::Entity e = src.Create();
    src.Add<estest::Unit>(e).name = "saved";
    src.TryGet<estest::Unit>(e)->stat.hp = 42;

    // _dupenv_s: getenv C4996(안전하지 않음) 회피 — 실패/미정의 시 현재 디렉터리 폴백.
    std::string tmpDir = ".";
    {
        char*  buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, "TEMP") == 0 && buf) { tmpDir = buf; }
        else if (_dupenv_s(&buf, &len, "TMP") == 0 && buf) { tmpDir = buf; }
        std::free(buf);
    }
    const std::string path = tmpDir + "/mye_m4a_scene_test.myscene";
    editor::SceneSerializer ser;
    auto sr = ser.SaveToFile(src, path);
    MYE_EXPECT((bool)sr);

    ecs::World dst;
    RegisterAll(dst);
    auto roots = ser.LoadFromFile(dst, path);
    MYE_EXPECT((bool)roots);
    MYE_EXPECT(roots.Value().size() == 1);
    auto* u = dst.TryGet<estest::Unit>(roots.Value().front());
    MYE_EXPECT(u && u->name == "saved" && u->stat.hp == 42);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// 프리팹 캡처·인스턴스화 + PropertyPath 오버라이드
// -----------------------------------------------------------------------------
MYE_TEST(PrefabCaptureInstantiate) {
    ecs::World w;
    RegisterAll(w);

    ecs::Entity root = w.Create();
    auto& u = w.Add<estest::Unit>(root);
    u.name = "prefab_root";
    u.stat.hp = 100;
    // 자식.
    ecs::Entity child = w.Create();
    w.Add<estest::Unit>(child).name = "prefab_child";
    w.Add<scene::Parent>(child).parent = root;
    w.Add<scene::Children>(root).list.push_back(child);

    auto cap = editor::PrefabAsset::CaptureFrom(w, root);
    MYE_EXPECT((bool)cap);
    if (!cap) return;

    // 오버라이드 없이 인스턴스화.
    auto inst = cap.Value().Instantiate(w);
    MYE_EXPECT((bool)inst);
    if (inst) {
        auto* iu = w.TryGet<estest::Unit>(inst.Value());
        MYE_EXPECT(iu && iu->name == "prefab_root" && iu->stat.hp == 100);
    }

    // PropertyPath 오버라이드로 hp 치환(로컬 ID 1 = 루트).
    editor::PrefabOverride ov;
    ov.targetLocalId = 1;
    ov.path = refl::PropertyPath::Parse("Unit.stat.hp").Value();
    ov.valueJson = "250";
    editor::PrefabOverride ovs[] = {ov};
    auto inst2 = cap.Value().Instantiate(w, ovs);
    MYE_EXPECT((bool)inst2);
    if (inst2) {
        auto* iu = w.TryGet<estest::Unit>(inst2.Value());
        MYE_EXPECT(iu && iu->stat.hp == 250);
        MYE_EXPECT(iu && iu->name == "prefab_root");   // 다른 필드는 보존.
    }
}

// -----------------------------------------------------------------------------
// 인스펙터 값 blob — PropertyPath 리프 read/write(인스펙터 ReadLeafBlob·PropertyEditCommand 근거)
// -----------------------------------------------------------------------------
MYE_TEST(InspectorValueBlobReadWrite) {
    estest::Unit u;
    u.stat.hp = 5;
    u.team = estest::Team::Blue;

    const refl::TypeInfo* ut = refl::GetType<estest::Unit>();
    MYE_EXPECT(ut != nullptr);

    // 리프 읽기(인스펙터 ReadLeafBlob 경로): stat.hp → "5".
    auto path = refl::PropertyPath::Parse("stat.hp");
    MYE_EXPECT((bool)path);
    auto wr = ser::JsonArchive::ForWrite();
    auto rr = refl::ReadValue(*ut, &u, path.Value(), wr);
    MYE_EXPECT((bool)rr);
    std::string before = json::Stringify(wr.Root(), 0);
    MYE_EXPECT(wr.Root().IsNumber() && wr.Root().AsInt() == 5);

    // 값 blob으로 쓰기(PropertyEditCommand Undo/Redo가 소비하는 왕복): "5" → "99".
    auto newVal = json::Parse("99");
    MYE_EXPECT((bool)newVal);
    auto rd = ser::JsonArchive::ForRead(newVal.Value());
    auto wres = refl::WriteValue(*ut, &u, path.Value(), rd);
    MYE_EXPECT((bool)wres);
    MYE_EXPECT(u.stat.hp == 99);

    // enum 리프 blob: team → "Blue".
    auto tp = refl::PropertyPath::Parse("team");
    auto wr2 = ser::JsonArchive::ForWrite();
    MYE_EXPECT((bool)refl::ReadValue(*ut, &u, tp.Value(), wr2));
    MYE_EXPECT(wr2.Root().IsString() && wr2.Root().AsString() == std::string_view("Blue"));

    // PropertyEditCommand 생성(인스펙터가 발행하는 커맨드 — 라벨·경로·blob 보유 검증).
    editor::ObjectRef target = editor::ObjectRef::Component(ecs::Entity{1, 1}, *ut);
    editor::PropertyEditCommand cmd(target, path.Value(),
                                    editor::ValueBlob{before}, editor::ValueBlob{"99"},
                                    "Edit hp");
    MYE_EXPECT(cmd.Label() == std::string_view("Edit hp"));
    MYE_EXPECT(cmd.Path().ToString() == "stat.hp");
    MYE_EXPECT(cmd.Target().componentType == ut);
}

// -----------------------------------------------------------------------------
// 메모리 스냅샷 왕복(플레이 모드 스냅샷 근거)
// -----------------------------------------------------------------------------
MYE_TEST(SceneSerializerSnapshotRestore) {
    ecs::World src;
    RegisterAll(src);
    ecs::Entity e = src.Create();
    src.Add<estest::Unit>(e).name = "snap";
    src.TryGet<estest::Unit>(e)->tags = {7, 8};

    editor::SceneSerializer ser;
    auto blob = ser.Snapshot(src);
    MYE_EXPECT((bool)blob);

    ecs::World dst;
    RegisterAll(dst);
    auto rr = ser.Restore(dst, blob.Value());
    MYE_EXPECT((bool)rr);

    bool found = false;
    for (std::uint32_t idx = 0; idx < 8; ++idx) {
        ecs::Entity cand{idx, dst.EntityGeneration(idx)};
        if (!dst.Valid(cand)) continue;
        auto* u = dst.TryGet<estest::Unit>(cand);
        if (u && u->name == "snap") {
            found = true;
            MYE_EXPECT(u->tags.size() == 2 && u->tags[0] == 7 && u->tags[1] == 8);
        }
    }
    MYE_EXPECT(found);
}

