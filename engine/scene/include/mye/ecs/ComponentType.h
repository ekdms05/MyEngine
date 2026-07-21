// mye/ecs/ComponentType.h — 컴포넌트 타입 등록(네이티브·플러그인·Lua 공용) (docs/03 §확장 1)
//
// ComponentTypeId는 타입 이름의 FNV-1a 64bit 해시 — 04 refl::TypeId의 인용/별칭이다
// (M2-A에는 리플렉션 프레임워크가 없으므로 이름 해시를 직접 쓴다; M3에서 refl::TypeId로 정합).
// 희소집합 선택의 최대 수혜 지점: 플러그인/Lua가 런타임에 "풀 하나 추가"로 타입 등록.
#pragma once

#include "mye/core/Base.h"   // HashFnv1a64, ServiceId 패턴

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace mye::refl { class TypeInfo; }   // 04 소유(M3). M2-A에는 전방 선언만.

namespace mye::ecs {

// 정본은 04 refl::TypeId(FNV-1a 64bit 이름 해시). M2-A는 이름 해시를 직접 쓴다.
using ComponentTypeId = uint64_t;

// 네이티브 컴포넌트 타입에 static constexpr id를 심는다.
//   struct SpriteRenderer { MYE_COMPONENT(SpriteRenderer); ... };
#define MYE_COMPONENT(TypeName) \
    static constexpr ::mye::ecs::ComponentTypeId kComponentTypeId = \
        ::mye::HashFnv1a64(#TypeName)

// 컴포넌트 타입 서술자 — 비템플릿(동적) 등록 경로(플러그인·Lua·직렬화)가 사용.
// 희소집합 풀은 이 서술자만으로 임의 타입의 저장·생성·파괴·이동을 수행한다.
struct ComponentTypeDesc {
    const char* name = nullptr;
    ComponentTypeId id = 0;
    size_t size = 0;
    size_t align = 0;

    // 배치(placement) 생성/파괴/이동 — 희소집합 풀이 원소 슬롯을 관리하는 데 사용.
    void (*construct)(void* dst) = nullptr;              // 기본 생성
    void (*destruct)(void* obj) = nullptr;              // 파괴
    void (*moveConstruct)(void* dst, void* src) = nullptr; // src → dst 이동 생성(swap-remove용)

    // 04 리플렉션 메타(직렬화·에디터 인스펙터용). M2-A에는 nullptr 허용.
    const refl::TypeInfo* reflection = nullptr;
};

// 네이티브 타입 T로부터 서술자를 자동 생성(값 시맨틱 타입 가정).
template <typename T>
inline ComponentTypeDesc MakeComponentTypeDesc(const char* name) {
    ComponentTypeDesc d;
    d.name = name;
    d.id = T::kComponentTypeId;
    d.size = sizeof(T);
    d.align = alignof(T);
    d.construct = [](void* dst) { ::new (dst) T(); };
    d.destruct = [](void* obj) { static_cast<T*>(obj)->~T(); };
    d.moveConstruct = [](void* dst, void* src) {
        ::new (dst) T(std::move(*static_cast<T*>(src)));
        static_cast<T*>(src)->~T();
    };
    return d;
}

} // namespace mye::ecs
