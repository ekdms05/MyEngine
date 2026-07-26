// mye/scene/SceneReflection.cpp — 코어 씬 컴포넌트 리플렉션 등록 (SceneReflection.h 참조)
//
// core Vec/Quat/Color/Rect는 비리플렉션이라, 각 컴포넌트를 CustomSerialize 훅으로 float를
// 평탄화해 왕복시킨다(engine/ui AnchorRect와 동일 패턴). AssetRef는 IArchive가 1급 지원.
// 런타임 파생 필드(dirty·flash 등)는 직렬화하지 않는다.
#include "mye/scene/SceneReflection.h"

#include "mye/scene/Transform.h"      // LocalTransform
#include "mye/scene/Renderable.h"     // SpriteRenderer
#include "mye/scene/RenderExtract.h"  // FloorLevel, SortingRef

#include "mye/refl/TypeBuilder.h"
#include "mye/ser/Archive.h"
#include "mye/asset/AssetGuid.h"      // AssetRef

// 비수식 이름 등록 — MYE_COMPONENT(kComponentTypeId=HashFnv1a64("Name"))와 컴포넌트 ID 정합.
MYE_REFLECT_NAME(mye::scene::LocalTransform, "LocalTransform");
MYE_REFLECT_NAME(mye::scene::SpriteRenderer, "SpriteRenderer");
MYE_REFLECT_NAME(mye::scene::FloorLevel, "FloorLevel");

namespace mye::refl {
template <> void Reflect<mye::scene::LocalTransform>(TypeBuilder<mye::scene::LocalTransform>& b);
template <> void Reflect<mye::scene::SpriteRenderer>(TypeBuilder<mye::scene::SpriteRenderer>& b);
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
    b.Version(1).CustomSerialize(&SerLocalTransform);
}
template <> void mye::refl::Reflect<mye::scene::SpriteRenderer>(TypeBuilder<mye::scene::SpriteRenderer>& b) {
    b.Version(1).CustomSerialize(&SerSpriteRenderer);
}
template <> void mye::refl::Reflect<mye::scene::FloorLevel>(TypeBuilder<mye::scene::FloorLevel>& b) {
    b.Version(1).CustomSerialize(&SerFloorLevel);
}

namespace mye::scene {

void RegisterCoreComponentReflection() {
    // GetType<T>() 최초 호출이 lazy 등록을 트리거한다(멱등). 씬 직렬화가 All()에서 찾도록 강제.
    (void)refl::GetType<LocalTransform>();
    (void)refl::GetType<SpriteRenderer>();
    (void)refl::GetType<FloorLevel>();
}

} // namespace mye::scene
