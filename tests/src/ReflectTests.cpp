// ReflectTests.cpp — 리플렉션·직렬화 v1 검증 (04, M3-A)
//
// 커버리지:
//   - 타입 등록(struct·enum·중첩·vector·상속 평탄화) + TypeId 안정성·충돌 없음
//   - JsonArchive 왕복(중첩 struct·enum 문자열·vector·프리미티브·AssetRef·버전 필드)
//   - 버전 상향 시 신규 필드 기본값 주입 + RenamedFrom 폴백
//   - PropertyPath 파싱·해석·경로 단위 read/write
//   - refl::TypeId == asset::AssetTypeId 정본 승격(Texture/Mesh 이름 해시 일치)
#include "TestFramework.h"

#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/refl/PropertyPath.h"
#include "mye/ser/JsonArchive.h"
#include "mye/ser/Serialize.h"
#include "mye/asset/AssetGuid.h"
#include "mye/asset/Texture.h"
#include "mye/asset/Mesh.h"
#include "mye/core/Json.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace mye;

// ---- 테스트용 타입 ----
namespace refltest {

enum class Facing : std::int32_t { North = 0, East = 1, South = 2, West = 3 };

struct TVec2 { std::int32_t x = 0; std::int32_t y = 0; };

struct Inner {
    float  scale = 1.0f;
    Facing dir = Facing::North;
};

struct Outer {
    std::string               name;
    TVec2                     pos;
    Inner                     inner;
    std::vector<std::int32_t> ids;
    bool                      enabled = true;
    mye::asset::AssetRef      ref;
};

// 버전 진화: v2. 'title' 은 v2 에서 'name' 에서 리네임(구버전 JSON 키 폴백).
// 'health' 는 v2 신규 필드(구버전 데이터에 없으면 기본값 100 유지).
struct Versioned {
    std::string  title;
    std::int32_t health = 100;
};

// 상속 평탄화 테스트.
struct Base { std::int32_t id = 0; };
struct Derived : Base { std::int32_t extra = 0; };

} // namespace refltest

// ---- 리플렉션 등록(비침투 매크로 — 전역 스코프) ----
MYE_REFLECT(refltest::TVec2);
MYE_REFLECT(refltest::Inner);
MYE_REFLECT(refltest::Outer);
MYE_REFLECT(refltest::Versioned);
MYE_REFLECT(refltest::Base);
MYE_REFLECT(refltest::Derived);
MYE_REFLECT_ENUM(refltest::Facing);

template <> void mye::refl::Reflect(TypeBuilder<refltest::TVec2>& b) {
    b.Version(1)
     .Field("x", &refltest::TVec2::x)
     .Field("y", &refltest::TVec2::y);
}
template <> void mye::refl::Reflect(TypeBuilder<refltest::Inner>& b) {
    b.Version(1)
     .Field("scale", &refltest::Inner::scale).Attr(Attribute::MakeRange(0.0, 10.0))
     .Field("dir", &refltest::Inner::dir);
}
template <> void mye::refl::Reflect(TypeBuilder<refltest::Outer>& b) {
    b.Version(1)
     .Field("name", &refltest::Outer::name)
     .Field("pos", &refltest::Outer::pos)
     .Field("inner", &refltest::Outer::inner)
     .Field("ids", &refltest::Outer::ids)
     .Field("enabled", &refltest::Outer::enabled)
     .Field("ref", &refltest::Outer::ref);
}
template <> void mye::refl::Reflect(TypeBuilder<refltest::Versioned>& b) {
    b.Version(2)
     .Field("title", &refltest::Versioned::title).RenamedFrom(2, "name")
     .Field("health", &refltest::Versioned::health);
}
template <> void mye::refl::Reflect(TypeBuilder<refltest::Base>& b) {
    b.Version(1).Field("id", &refltest::Base::id);
}
template <> void mye::refl::Reflect(TypeBuilder<refltest::Derived>& b) {
    b.Version(1).Base<refltest::Base>().Field("extra", &refltest::Derived::extra);
}
template <> void mye::refl::Reflect(EnumBuilder<refltest::Facing>& b) {
    b.Value("North", refltest::Facing::North)
     .Value("East",  refltest::Facing::East)
     .Value("South", refltest::Facing::South)
     .Value("West",  refltest::Facing::West);
}

// -----------------------------------------------------------------------------
// 등록·TypeId 검증
// -----------------------------------------------------------------------------
MYE_TEST(ReflectRegistration) {
    using namespace refltest;
    const refl::TypeInfo* outer = refl::GetType<Outer>();
    MYE_EXPECT(outer != nullptr);
    MYE_EXPECT(outer->Name() == std::string_view("refltest::Outer"));
    MYE_EXPECT(outer->GetKind() == refl::Kind::Struct);
    MYE_EXPECT(outer->Fields().size() == 6);

    // 중첩 타입 필드 해석.
    const refl::FieldInfo* pos = outer->FindField("pos");
    MYE_EXPECT(pos != nullptr);
    MYE_EXPECT(pos->Type().Name() == std::string_view("refltest::TVec2"));
    MYE_EXPECT(pos->Type().Fields().size() == 2);

    // vector 필드 = Kind::Vector + 원소 i32.
    const refl::FieldInfo* ids = outer->FindField("ids");
    MYE_EXPECT(ids != nullptr);
    MYE_EXPECT(ids->Type().GetKind() == refl::Kind::Vector);
    MYE_EXPECT(ids->Type().ElementType() != nullptr);
    MYE_EXPECT(ids->Type().ElementType()->Name() == std::string_view("i32"));

    // AssetRef 필드 = Kind::AssetRef.
    const refl::FieldInfo* ref = outer->FindField("ref");
    MYE_EXPECT(ref != nullptr);
    MYE_EXPECT(ref->Type().GetKind() == refl::Kind::AssetRef);

    // enum.
    const refl::TypeInfo* facing = refl::GetType<Facing>();
    MYE_EXPECT(facing != nullptr && facing->GetKind() == refl::Kind::Enum);
    const refl::EnumInfo* e = facing->AsEnum();
    MYE_EXPECT(e != nullptr && e->Constants().size() == 4);
    std::int64_t v = -1;
    MYE_EXPECT(e->ValueOf("South", v) && v == 2);
    std::string_view nm;
    MYE_EXPECT(e->NameOf(3, nm) && nm == std::string_view("West"));

    // Attribute 부착 검증.
    const refl::TypeInfo* inner = refl::GetType<Inner>();
    const refl::FieldInfo* scale = inner->FindField("scale");
    MYE_EXPECT(scale->HasAttribute(refl::AttributeKind::Range));
    const refl::Attribute* ra = scale->FindAttribute(refl::AttributeKind::Range);
    MYE_EXPECT(ra && ra->rangeMax == 10.0);
}

MYE_TEST(ReflectTypeIdStability) {
    using namespace refltest;
    // TypeId == 이름 FNV-1a 64(정본). GetType 재호출 시 동일 포인터(멱등 등록).
    const refl::TypeInfo* a = refl::GetType<Outer>();
    const refl::TypeInfo* b = refl::GetType<Outer>();
    MYE_EXPECT(a == b);
    MYE_EXPECT(a->Id() == refl::TypeIdFromName("refltest::Outer"));
    MYE_EXPECT(a->Id() == mye::HashFnv1a64("refltest::Outer"));

    // 이름 조회 = ID 조회 동치.
    MYE_EXPECT(refl::TypeRegistry::Get().Find("refltest::Outer") == a);
    MYE_EXPECT(refl::TypeRegistry::Get().Find(a->Id()) == a);

    // 서로 다른 타입은 서로 다른 ID(충돌 없음).
    MYE_EXPECT(refl::GetType<Inner>()->Id() != refl::GetType<Outer>()->Id());
    MYE_EXPECT(refl::GetType<TVec2>()->Id() != refl::GetType<Facing>()->Id());
}

MYE_TEST(ReflectInheritanceFlattening) {
    using namespace refltest;
    const refl::TypeInfo* d = refl::GetType<Derived>();
    MYE_EXPECT(d != nullptr);
    // 베이스 필드('id')가 앞, 파생('extra')이 뒤.
    MYE_EXPECT(d->Fields().size() == 2);
    MYE_EXPECT(d->Fields()[0].Name() == std::string_view("id"));
    MYE_EXPECT(d->Fields()[1].Name() == std::string_view("extra"));
}

// -----------------------------------------------------------------------------
// JsonArchive 왕복
// -----------------------------------------------------------------------------
MYE_TEST(JsonArchiveRoundtrip) {
    using namespace refltest;
    Outer src;
    src.name = "player";
    src.pos = {3, -7};
    src.inner.scale = 2.5f;
    src.inner.dir = Facing::South;
    src.ids = {10, 20, 30};
    src.enabled = false;
    src.ref.guid = asset::AssetGuid{0x0123456789abcdefull, 0xfedcba9876543210ull};
    src.ref.type = asset::Texture::kAssetTypeId;

    // 쓰기.
    auto wr = ser::JsonArchive::ForWrite();
    auto wres = ser::Serialize(wr, src);
    MYE_EXPECT((bool)wres);
    const json::Value& root = wr.Root();
    MYE_EXPECT(root.IsObject());

    // 버전 필드 존재.
    const json::Value* ver = root.Find("__version");
    MYE_EXPECT(ver && ver->AsInt() == 1);

    // enum 은 문자열로 직렬화.
    const json::Value* inner = root.Find("inner");
    MYE_EXPECT(inner && inner->IsObject());
    const json::Value* dir = inner->Find("dir");
    MYE_EXPECT(dir && dir->IsString() && dir->AsString() == std::string_view("South"));

    // 텍스트 왕복(Stringify → Parse) 후 읽기.
    std::string text = json::Stringify(root);
    auto parsed = json::Parse(text);
    MYE_EXPECT((bool)parsed);

    Outer dst;
    auto rd = ser::JsonArchive::ForRead(parsed.Value());
    auto rres = ser::Serialize(rd, dst);
    MYE_EXPECT((bool)rres);

    MYE_EXPECT(dst.name == "player");
    MYE_EXPECT(dst.pos.x == 3 && dst.pos.y == -7);
    MYE_EXPECT_NEAR(dst.inner.scale, 2.5f, 1e-6f);
    MYE_EXPECT(dst.inner.dir == Facing::South);
    MYE_EXPECT(dst.ids.size() == 3 && dst.ids[0] == 10 && dst.ids[2] == 30);
    MYE_EXPECT(dst.enabled == false);
    MYE_EXPECT(dst.ref.guid.hi == 0x0123456789abcdefull);
    MYE_EXPECT(dst.ref.guid.lo == 0xfedcba9876543210ull);
    MYE_EXPECT(dst.ref.type == asset::Texture::kAssetTypeId);
}

// -----------------------------------------------------------------------------
// 버전 마이그레이션: 신규 필드 기본값 + RenamedFrom 폴백
// -----------------------------------------------------------------------------
MYE_TEST(JsonArchiveVersionMigration) {
    using namespace refltest;
    // 구버전(v1) 데이터: 'name' 키(현재 'title' 의 옛 이름), 'health' 없음.
    const char* oldJson = R"({ "__version": 1, "name": "hero" })";
    auto parsed = json::Parse(oldJson);
    MYE_EXPECT((bool)parsed);

    Versioned dst;   // health 기본 100
    auto rd = ser::JsonArchive::ForRead(parsed.Value());
    auto rres = ser::Serialize(rd, dst);
    MYE_EXPECT((bool)rres);

    // RenamedFrom(2,"name") — 현재 'title' 키가 없으면 옛 이름 'name' 폴백으로 채워진다.
    MYE_EXPECT(dst.title == "hero");
    // 신규 필드 'health' 는 구버전 데이터에 없으므로 기본값 유지.
    MYE_EXPECT(dst.health == 100);
}

// -----------------------------------------------------------------------------
// PropertyPath 파싱·해석·경로 단위 read/write
// -----------------------------------------------------------------------------
MYE_TEST(PropertyPathParseAndResolve) {
    using namespace refltest;
    auto p = refl::PropertyPath::Parse("inner.dir");
    MYE_EXPECT((bool)p);
    MYE_EXPECT(p.Value().ToString() == "inner.dir");

    auto p2 = refl::PropertyPath::Parse("ids[1]");
    MYE_EXPECT((bool)p2);
    MYE_EXPECT(p2.Value().ToString() == "ids[1]");

    Outer obj;
    obj.inner.dir = Facing::West;
    obj.ids = {5, 6, 7};

    const refl::TypeInfo* ot = refl::GetType<Outer>();

    // ResolvePath 리프 접근.
    auto r = refl::ResolvePath(*ot, &obj, p.Value());
    MYE_EXPECT((bool)r);
    MYE_EXPECT(r.Value().type->GetKind() == refl::Kind::Enum);

    auto ri = refl::ResolvePath(*ot, &obj, p2.Value());
    MYE_EXPECT((bool)ri);
    MYE_EXPECT(*static_cast<std::int32_t*>(ri.Value().ptr) == 6);

    // 경로 단위 read → write 왕복(Undo 값 blob 시나리오).
    auto out = ser::JsonArchive::ForWrite();
    auto rdres = refl::ReadValue(*ot, &obj, p2.Value(), out);
    MYE_EXPECT((bool)rdres);
    // ids[1] == 6 이 스칼라로 기록됨.
    MYE_EXPECT(out.Root().IsNumber() && out.Root().AsInt() == 6);

    // 값을 바꿔 쓰기.
    auto changed = json::Parse("42");
    Outer obj2 = obj;
    auto in = ser::JsonArchive::ForRead(changed.Value());
    auto wrres = refl::WriteValue(*ot, &obj2, p2.Value(), in);
    MYE_EXPECT((bool)wrres);
    MYE_EXPECT(obj2.ids[1] == 42);
}

// -----------------------------------------------------------------------------
// AssetTypeId 정본 승격 — refl::TypeId 와 asset::AssetTypeId 동일 알고리즘·값.
// -----------------------------------------------------------------------------
MYE_TEST(AssetTypeIdPromotion) {
    // 기존 에셋 타입의 kAssetTypeId 는 refl::TypeIdFromName(이름) 과 비트 동일.
    MYE_EXPECT(asset::Texture::kAssetTypeId == refl::TypeIdFromName("Texture"));
    MYE_EXPECT(asset::Mesh::kAssetTypeId == refl::TypeIdFromName("Mesh"));
    // AssetTypeId 는 refl::TypeId 의 별칭(같은 타입).
    static_assert(std::is_same_v<asset::AssetTypeId, refl::TypeId>,
                  "AssetTypeId must be promoted to refl::TypeId");
    MYE_EXPECT(true);
}
