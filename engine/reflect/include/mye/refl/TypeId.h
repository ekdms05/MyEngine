// mye/refl/TypeId.h — 리플렉션 타입 식별자 (docs/04 §리플렉션, M3-A)
//
// refl::TypeId 는 리플렉션 타입 식별자의 **정본(canonical)**이다.
//   - "타입 이름 문자열의 FNV-1a 64bit 해시" (mye::HashFnv1a64와 동일 알고리즘) — DLL 경계 안정.
//   - asset::AssetTypeId 는 이 타입의 별칭으로 승격된다(M2의 자체 FNV placeholder를 대체).
//   - 03 ComponentTypeId·07 TypeId 도 이 타입의 인용/별칭이다(01 EventTypeId만 별개 소유).
//
// M3-A 범위: 식별자 타입과 이름→ID 헬퍼 + 컴파일타임 타입 이름 획득. 등록·조회는 TypeInfo.h.
#pragma once

#include "mye/core/Base.h"   // HashFnv1a64, EventTypeId 규약과 정렬

#include <cstdint>
#include <string_view>

namespace mye::refl {

// 리플렉션 타입 식별자 — 타입 이름 FNV-1a 64 해시. 0 = 미지정/무효.
using TypeId = std::uint64_t;

// 이름 문자열로부터 TypeId 를 계산(등록·조회 공용). core의 정본 해시를 재사용한다.
constexpr TypeId TypeIdFromName(std::string_view name) noexcept {
    return ::mye::HashFnv1a64(name);
}

// ---------------------------------------------------------------------------
// 컴파일타임 타입 이름 — TypeTag<T>::Name() 특수화가 정본 이름을 제공한다.
//   기본 구현은 없음(의도적): 리플렉션 등록 타입은 MYE_REFLECT_NAME 또는 TypeBuilder가
//   이름을 명시해야 한다. 원시형(bool/int/float/string 등)은 아래에서 특수화한다.
//   비침투 규약: 서드파티 타입도 TypeTag<T> 특수화만으로 안정 이름을 부여할 수 있다.
// ---------------------------------------------------------------------------
template <typename T>
struct TypeTag;   // 미정의 — 특수화 필요

// 원시형 표준 이름(직렬화 프리미티브 및 컨테이너 원소 식별에 사용).
#define MYE_REFL_PRIMITIVE_NAME(Type, NameLiteral)                         \
    template <> struct TypeTag<Type> {                                     \
        static constexpr std::string_view Name() noexcept { return NameLiteral; } \
    }

MYE_REFL_PRIMITIVE_NAME(bool,               "bool");
MYE_REFL_PRIMITIVE_NAME(std::int8_t,        "i8");
MYE_REFL_PRIMITIVE_NAME(std::int16_t,       "i16");
MYE_REFL_PRIMITIVE_NAME(std::int32_t,       "i32");
MYE_REFL_PRIMITIVE_NAME(std::int64_t,       "i64");
MYE_REFL_PRIMITIVE_NAME(std::uint8_t,       "u8");
MYE_REFL_PRIMITIVE_NAME(std::uint16_t,      "u16");
MYE_REFL_PRIMITIVE_NAME(std::uint32_t,      "u32");
MYE_REFL_PRIMITIVE_NAME(std::uint64_t,      "u64");
MYE_REFL_PRIMITIVE_NAME(float,              "f32");
MYE_REFL_PRIMITIVE_NAME(double,             "f64");
MYE_REFL_PRIMITIVE_NAME(std::string,        "string");

#undef MYE_REFL_PRIMITIVE_NAME

// T의 정본 이름을 얻는다(TypeTag<T> 특수화 필요). refl::TypeIdOf<T>() 와 짝.
template <typename T>
constexpr std::string_view TypeNameOf() noexcept { return TypeTag<T>::Name(); }

template <typename T>
constexpr TypeId TypeIdOf() noexcept { return TypeIdFromName(TypeTag<T>::Name()); }

// 리플렉션 이름을 비침투적으로 부여(TypeTag 특수화 생성). 네임스페이스 밖(전역)에서 사용.
//   예:  MYE_REFLECT_NAME(mye::asset::SpriteSheet, "mye::asset::SpriteSheet");
#define MYE_REFLECT_NAME(Type, NameLiteral)                                \
    template <> struct ::mye::refl::TypeTag<Type> {                        \
        static constexpr std::string_view Name() noexcept { return NameLiteral; } \
    }

} // namespace mye::refl
