// mye/scene/SceneReflection.h — 코어 씬 컴포넌트 리플렉션 강제 등록 (docs/03, 07 §3)
//
// LocalTransform/SpriteRenderer/FloorLevel 등 코어 씬 컴포넌트에 리플렉션(직렬화 훅)을 부여해
// SceneSerializer가 씬을 실제 내용(위치·스프라이트 등)과 함께 저장/로드할 수 있게 한다.
// 리플렉션은 lazy(GetType 최초 호출 시 1회 등록)라, 씬 직렬화가 TypeRegistry::All()에서 찾으려면
// 게임/에디터/도구 시작 시 이 함수를 1회 호출해야 한다.
//
// 이름 규약: MYE_COMPONENT(Name)의 kComponentTypeId=HashFnv1a64("Name")(비수식)와 정합하도록
//   리플렉션도 비수식 이름("LocalTransform" 등)으로 등록한다(MYE_REFLECT_NAME).
#pragma once

namespace mye::scene {

// 코어 씬 컴포넌트 리플렉션을 TypeRegistry에 등록(멱등). 씬 직렬화 이전에 1회 호출.
void RegisterCoreComponentReflection();

} // namespace mye::scene
