// mye/scene/SceneReflection.cpp — 코어 씬 컴포넌트 리플렉션 등록 (SceneReflection.h 참조)
//
// Vec/Quat/Color/Rect는 새 컴포넌트의 중첩 필드에 사용한다. 기존 컴포넌트의
// CustomSerialize는 평탄화된 파일 계약을 보존한다. AssetRef는 IArchive가 1급 지원.
// 런타임 파생 필드(dirty·flash 등)는 직렬화하지 않는다.
#include "mye/scene/SceneReflection.h"

#include "mye/anim/SpriteAnimator.h"
#include "mye/phys/Collision.h"
#include "mye/scene/Transform.h"      // LocalTransform
#include "mye/scene/Renderable.h"     // SpriteRenderer
#include "mye/scene/Camera3D.h"
#include "mye/ecs/World.h"
#include "mye/scene/RenderExtract.h"  // FloorLevel, SortingRef

#include "mye/refl/TypeBuilder.h"
#include "mye/ser/Archive.h"
#include "mye/asset/AssetGuid.h"      // AssetRef

// 비수식 이름 등록 — MYE_COMPONENT(kComponentTypeId=HashFnv1a64("Name"))와 컴포넌트 ID 정합.
MYE_REFLECT_NAME(mye::Vec2, "Vec2");
MYE_REFLECT_NAME(mye::Vec3, "Vec3");
MYE_REFLECT_NAME(mye::Quat, "Quat");
MYE_REFLECT_NAME(mye::Rect, "Rect");
MYE_REFLECT_NAME(mye::Color, "Color");
template <> void mye::refl::Reflect<mye::Vec2>(TypeBuilder<mye::Vec2>& b) {
    b.Field("x", &mye::Vec2::x).Field("y", &mye::Vec2::y);
}
template <> void mye::refl::Reflect<mye::Vec3>(TypeBuilder<mye::Vec3>& b) {
    b.Field("x", &mye::Vec3::x).Field("y", &mye::Vec3::y).Field("z", &mye::Vec3::z);
}
template <> void mye::refl::Reflect<mye::Quat>(TypeBuilder<mye::Quat>& b) {
    b.Field("x", &mye::Quat::x).Field("y", &mye::Quat::y).Field("z", &mye::Quat::z).Field("w", &mye::Quat::w);
}
template <> void mye::refl::Reflect<mye::Rect>(TypeBuilder<mye::Rect>& b) {
    b.Field("x", &mye::Rect::x).Field("y", &mye::Rect::y).Field("w", &mye::Rect::w).Field("h", &mye::Rect::h);
}
template <> void mye::refl::Reflect<mye::Color>(TypeBuilder<mye::Color>& b) {
    b.Field("r", &mye::Color::r).Field("g", &mye::Color::g).Field("b", &mye::Color::b).Field("a", &mye::Color::a);
}

MYE_REFLECT_NAME(mye::scene::LocalTransform, "LocalTransform");
MYE_REFLECT_NAME(mye::scene::SpriteRenderer, "SpriteRenderer");
MYE_REFLECT_NAME(mye::scene::BillboardRenderer, "BillboardRenderer");
MYE_REFLECT_NAME(mye::scene::MeshRenderer, "MeshRenderer");
MYE_REFLECT_NAME(mye::scene::Camera3D, "Camera3D");
MYE_REFLECT_NAME(mye::scene::SortingRef, "SortingRef");
template<> void mye::refl::Reflect(TypeBuilder<mye::scene::SortingRef>& b) {
    b.Field("sortLayer", &mye::scene::SortingRef::sortLayer)
        .Field("orderInLayer", &mye::scene::SortingRef::orderInLayer);
}
MYE_REFLECT_ENUM(mye::scene::BillboardMode);
template<> void mye::refl::Reflect(EnumBuilder<mye::scene::BillboardMode>& b) {
    b.Value("Full", mye::scene::BillboardMode::Full)
        .Value("YAxis", mye::scene::BillboardMode::YAxis).Value("None", mye::scene::BillboardMode::None);
}
MYE_REFLECT_NAME(mye::scene::FloorLevel, "FloorLevel");
MYE_REFLECT_NAME(mye::anim::SpriteAnimator, "SpriteAnimator");

namespace mye::refl {
template <> void Reflect<mye::scene::LocalTransform>(TypeBuilder<mye::scene::LocalTransform>& b);
template <> void Reflect<mye::scene::SpriteRenderer>(TypeBuilder<mye::scene::SpriteRenderer>& b);
template <> void Reflect<mye::anim::SpriteAnimator>(TypeBuilder<mye::anim::SpriteAnimator>& b);
template <> void Reflect<mye::scene::FloorLevel>(TypeBuilder<mye::scene::FloorLevel>& b);
} // namespace mye::refl

namespace {

using mye::ser::IArchive;

// float 필드 왕복(f64 평탄화). 읽기면 채우고 쓰기면 내보낸다.
void RwF(IArchive& ar, const char* key, float& v) {
    ar.Key(key);
    double d = static_cast<double>(v);
    ar.Value(d);
    if (ar.IsReading()) v = static_cast<float>(d);
}
void RwBool(IArchive& ar, const char* key, bool& v) {
    ar.Key(key);
    ar.Value(v);
}
// 정수(폭 무관) 왕복 — i64 평탄화.
template <typename T>
void RwInt(IArchive& ar, const char* key, T& v) {
    ar.Key(key);
    std::int64_t i = static_cast<std::int64_t>(v);
    ar.Value(i);
    if (ar.IsReading()) v = static_cast<T>(i);
}

void SerLocalTransform(IArchive& ar, void* inst) {
    auto& t = *static_cast<mye::scene::LocalTransform*>(inst);
    RwF(ar, "px", t.position.x); RwF(ar, "py", t.position.y); RwF(ar, "pz", t.position.z);
    RwF(ar, "rx", t.rotation.x); RwF(ar, "ry", t.rotation.y);
    RwF(ar, "rz", t.rotation.z); RwF(ar, "rw", t.rotation.w);
    RwF(ar, "sx", t.scale.x);    RwF(ar, "sy", t.scale.y);    RwF(ar, "sz", t.scale.z);
    if (ar.IsReading()) t.dirty = true;   // 로드 후 TransformSystem 재계산 유도
}

void SerSpriteRenderer(IArchive& ar, void* inst) {
    auto& s = *static_cast<mye::scene::SpriteRenderer*>(inst);
    ar.Key("sprite"); ar.Value(s.sprite);   // AssetRef(guid+type) — IArchive 1급 지원
    RwF(ar, "uvx", s.srcUV.x); RwF(ar, "uvy", s.srcUV.y);
    RwF(ar, "uvw", s.srcUV.w); RwF(ar, "uvh", s.srcUV.h);
    RwF(ar, "pvx", s.pivotPx.x); RwF(ar, "pvy", s.pivotPx.y);
    RwF(ar, "tr", s.tint.r); RwF(ar, "tg", s.tint.g); RwF(ar, "tb", s.tint.b); RwF(ar, "ta", s.tint.a);
    RwBool(ar, "flipX", s.flipX); RwBool(ar, "flipY", s.flipY); RwBool(ar, "visible", s.visible);
    RwInt(ar, "sortLayer", s.sort.sortLayer);
    RwInt(ar, "orderInLayer", s.sort.orderInLayer);
    // flashColor/flashAmount는 런타임 히트 이펙트 — 직렬화 제외.
}

void SerFloorLevel(IArchive& ar, void* inst) {
    auto& f = *static_cast<mye::scene::FloorLevel*>(inst);
    RwInt(ar, "level", f.level);
}

} // namespace

template <> void mye::refl::Reflect<mye::scene::LocalTransform>(TypeBuilder<mye::scene::LocalTransform>& b) {
    b.Version(1).CustomSerialize(&SerLocalTransform)
        .Field("position", &mye::scene::LocalTransform::position).Attr(Attribute::MakeTooltip("부모 기준 위치. 월드 단위, +Y 위쪽. 최상위 조작 캐릭터는 지면 XY를 사용합니다."))
        .Field("rotation", &mye::scene::LocalTransform::rotation).Attr(Attribute::MakeTooltip("회전 쿼터니언 x/y/z/w. 단위 회전은 0,0,0,1입니다."))
        .Field("scale", &mye::scene::LocalTransform::scale).Attr(Attribute::MakeTooltip("축별 크기 배율. 기본 1,1,1입니다."));
}
template <> void mye::refl::Reflect<mye::scene::SpriteRenderer>(TypeBuilder<mye::scene::SpriteRenderer>& b) {
    b.Version(1).CustomSerialize(&SerSpriteRenderer)
        .Field("sprite", &mye::scene::SpriteRenderer::sprite).Attr(Attribute::MakeTooltip("표시할 PNG 에셋입니다. 에셋 브라우저에서 드래그하여 지정하세요."))
        .Field("srcUV", &mye::scene::SpriteRenderer::srcUV).Attr(Attribute::MakeTooltip("이미지에서 사용할 영역. 0~1 정규화 값이며 전체 이미지는 0,0,1,1입니다."))
        .Field("pivotPx", &mye::scene::SpriteRenderer::pivotPx).Attr(Attribute::MakeTooltip("잘라낸 영역의 좌상단 기준 피벗(픽셀). 캐릭터는 발밑에 맞추세요."))
        .Field("tint", &mye::scene::SpriteRenderer::tint).Attr(Attribute::MakeTooltip("이미지 색에 곱할 RGBA 값입니다. 흰색이면 원본 색을 사용합니다."))
        .Field("visible", &mye::scene::SpriteRenderer::visible).Attr(Attribute::MakeTooltip("스프라이트 표시 여부. 충돌 컴포넌트의 활성 여부와 별개입니다."))
        .Field("flipX", &mye::scene::SpriteRenderer::flipX).Attr(Attribute::MakeTooltip("이미지를 좌우로 반전합니다."))
        .Field("flipY", &mye::scene::SpriteRenderer::flipY).Attr(Attribute::MakeTooltip("이미지를 위아래로 반전합니다."));
}
template <> void mye::refl::Reflect<mye::scene::FloorLevel>(TypeBuilder<mye::scene::FloorLevel>& b) {
    b.Version(1).CustomSerialize(&SerFloorLevel).Field("level", &mye::scene::FloorLevel::level).Attr(Attribute::MakeTooltip("높이 층 번호. 일반 지면은 0입니다."));
}

template <> void mye::refl::Reflect(TypeBuilder<mye::scene::BillboardRenderer>& b) {
    using C = mye::scene::BillboardRenderer;
    b.Version(1).Field("sprite", &C::sprite).Attr(Attribute::MakeTooltip("PNG 또는 .anim의 시트 텍스처. 3D 월드에서 PPU 48로 표시합니다."))
        .Field("srcUV", &C::srcUV).Field("pivotPx", &C::pivotPx).Attr(Attribute::MakeTooltip("잘라낸 프레임 좌상단 기준 픽셀 피벗. 0,0은 발밑 중앙입니다."))
        .Field("tint", &C::tint).Field("mode", &C::mode).Attr(Attribute::MakeTooltip("Full: 카메라의 수평/수직축. YAxis: +Y를 유지하며 카메라 방향으로 회전. None: 오브젝트 회전 유지. 물리 방향은 바꾸지 않습니다."))
        .Field("flipX", &C::flipX).Field("flipY", &C::flipY).Field("visible", &C::visible).Field("sort", &C::sort);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::scene::MeshRenderer>& b) {
    using C = mye::scene::MeshRenderer;
    b.Version(1).Field("mesh", &C::mesh).Attr(Attribute::MakeTooltip("정적 GLB 또는 임베디드 glTF 메시를 드래그하세요. 외부 .bin, 리깅, glTF 재질은 미지원입니다."))
        .Field("material", &C::material).Attr(Attribute::MakeTooltip("단일 알베도 PNG 텍스처 GUID. 비어 있으면 흰색을 사용합니다."))
        .Field("depthMode", &C::depthMode).Attr(Attribute::MakeTooltip("2D: 0 앵커 깊이, 1 앵커+기하 바이어스, 2 기하 깊이. 원근 카메라는 기하 깊이를 사용합니다."))
        .Field("visible", &C::visible).Field("sort", &C::sort);
}
template <> void mye::refl::Reflect(TypeBuilder<mye::scene::Camera3D>& b) {
    using C = mye::scene::Camera3D;
    b.Version(1).Field("current", &C::current).Attr(Attribute::MakeTooltip("씬에 하나만 켜세요. Play와 MyGame이 이 카메라를 사용하며, 없으면 기존 2D 카메라를 사용합니다."))
        .Field("target", &C::target).Attr(Attribute::MakeTooltip("바라보는 월드 XYZ. followTarget이 있으면 대상 위치에서의 오프셋입니다."))
        .Field("followTarget", &C::followTarget).Attr(Attribute::MakeTooltip("추종할 고유 오브젝트 이름. 지정하면 카메라 위치와 target 모두 대상 월드 위치에서의 오프셋입니다."))
        .Field("fovDegrees", &C::fovDegrees).Attr(Attribute::MakeTooltip("세로 시야각 1~179도. 줌은 시야각 또는 카메라 위치로 조정합니다."))
        .Field("nearPlane", &C::nearPlane).Field("farPlane", &C::farPlane).Attr(Attribute::MakeTooltip("절두체 거리. 0 < nearPlane < farPlane, 월드 단위입니다."));
}

template <> void mye::refl::Reflect<mye::anim::SpriteAnimator>(TypeBuilder<mye::anim::SpriteAnimator>& b) {
    b.Version(1).Field("animation", &mye::anim::SpriteAnimator::animation).Attr(Attribute::MakeTooltip(".anim 에셋을 드래그하세요. 원본 이미지는 애니메이션 에셋에서 지정합니다."))
        .Field("speed", &mye::anim::SpriteAnimator::speed).Attr(Attribute::MakeTooltip("모션 재생 배율. 1은 원래 속도입니다. 이동 speed와는 별개입니다."))
        .Field("playing", &mye::anim::SpriteAnimator::playing).Attr(Attribute::MakeTooltip("실행 중 모션을 자동 재생합니다."));
}

MYE_REFLECT_NAME(mye::scene::ObjectName, "ObjectName");
MYE_REFLECT_NAME(mye::phys::Collider2D, "Collider2D");
MYE_REFLECT_NAME(mye::phys::KinematicBody2D, "KinematicBody2D");
MYE_REFLECT(mye::phys::Shape2D);
MYE_REFLECT_ENUM(mye::phys::ShapeKind);
template<> void mye::refl::Reflect(EnumBuilder<mye::phys::ShapeKind>& b) {
    b.Value("Box", mye::phys::ShapeKind::AABB).Value("Circle", mye::phys::ShapeKind::Circle);
}
template<> void mye::refl::Reflect(TypeBuilder<mye::phys::Shape2D>& b) {
    b.Version(1).Field("kind", &mye::phys::Shape2D::kind).Attr(Attribute::MakeTooltip("Box는 박스, Circle은 원입니다. 원은 half.x를 반지름으로 사용합니다.")).Field("half", &mye::phys::Shape2D::half).Attr(Attribute::MakeTooltip("박스 반폭·반높이(월드 단위). 48 픽셀 전체 폭은 half.x=0.5입니다."));
}
template<> void mye::refl::Reflect(TypeBuilder<mye::scene::ObjectName>& b) {
    b.Version(1).Field("value", &mye::scene::ObjectName::value).Attr(Attribute::MakeTooltip("이벤트 대상·포털 도착 지점에서 사용하는 고유 오브젝트 이름입니다."));
}
template<> void mye::refl::Reflect(TypeBuilder<mye::phys::Collider2D>& b) {
    using C = mye::phys::Collider2D;
    b.Version(1).Field("shape", &C::shape).Attr(Attribute::MakeTooltip("충돌 모양과 크기. 뷰포트의 충돌 영역 표시로 확인하세요.")).Field("offset", &C::offset).Attr(Attribute::MakeTooltip("오브젝트 위치에서 충돌 중심까지의 XY 오프셋(월드 단위).")).Field("isTrigger", &C::isTrigger).Attr(Attribute::MakeTooltip("켜면 이동을 막지 않고 TriggerEnter/TriggerExit 이벤트를 보냅니다."))
        .Field("layerMask", &C::layerMask).Attr(Attribute::MakeTooltip("이 충돌체가 속한 그룹의 비트 마스크입니다.")).Field("collidesWith", &C::collidesWith).Attr(Attribute::MakeTooltip("충돌할 상대 그룹의 비트 마스크입니다."))
        .Field("floorMask", &C::floorMask).Attr(Attribute::MakeTooltip("충돌을 허용할 높이 층의 비트 마스크입니다.")).Field("triggerId", &C::triggerId).Attr(Attribute::MakeTooltip("트리거 식별 번호입니다. 서로 다른 이벤트 영역을 구분할 때 사용합니다."));
}
template<> void mye::refl::Reflect(TypeBuilder<mye::phys::KinematicBody2D>& b) {
    using C = mye::phys::KinematicBody2D;
    b.Version(1).Field("snapToGround", &C::snapToGround).Attr(Attribute::MakeTooltip("바닥 높이 판정에 맞춥니다. 바닥 판정 서비스 연결이 필요합니다.")).Field("skin", &C::skin).Attr(Attribute::MakeTooltip("충돌면과 유지하는 작은 간격(월드 단위). 0~1 범위를 사용합니다.")).Field("maxSlideIters", &C::maxSlideIters).Attr(Attribute::MakeTooltip("한 고정 틱의 최대 벽면 미끄러짐 횟수. 1~16 범위입니다."));
}

namespace mye::scene {

void RegisterCoreComponentReflection() {
    // GetType<T>() 최초 호출이 lazy 등록을 트리거한다(멱등). 씬 직렬화가 All()에서 찾도록 강제.
    (void)refl::GetType<ObjectName>();
    (void)refl::GetType<phys::Collider2D>();
    (void)refl::GetType<phys::KinematicBody2D>();
    (void)refl::GetType<LocalTransform>();
    (void)refl::GetType<SpriteRenderer>();
    (void)refl::GetType<BillboardRenderer>();
    (void)refl::GetType<MeshRenderer>();
    (void)refl::GetType<Camera3D>();
    (void)refl::GetType<FloorLevel>();
    (void)refl::GetType<anim::SpriteAnimator>();
}

void RegisterCoreComponents(ecs::World& world) {
    RegisterCoreComponentReflection();
    world.RegisterComponent<ObjectName>("ObjectName");
    world.RegisterComponent<LocalTransform>("LocalTransform");
    world.RegisterComponent<WorldTransform>("WorldTransform");
    world.RegisterComponent<Parent>("Parent");
    world.RegisterComponent<Children>("Children");
    world.RegisterComponent<SpriteRenderer>("SpriteRenderer");
    world.RegisterComponent<BillboardRenderer>("BillboardRenderer");
    world.RegisterComponent<MeshRenderer>("MeshRenderer");
    world.RegisterComponent<Camera3D>("Camera3D");
    world.RegisterComponent<FloorLevel>("FloorLevel");
    world.RegisterComponent<phys::Collider2D>("Collider2D");
    world.RegisterComponent<phys::KinematicBody2D>("KinematicBody2D");
    world.RegisterComponent<anim::SpriteAnimator>("SpriteAnimator");
}

} // namespace mye::scene
