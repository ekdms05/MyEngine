// mye/refl/TypeBuilder.h — 비침투적 등록 빌더 + Reflect<T> 특수화 규약 (docs/04 §등록)
//
// 선택된 전략(docs/04): A 기반 — 비침투적 Reflect<T> 템플릿 특수화 + 얇은 보조 매크로.
//   각 타입은 `template <> void Reflect(TypeBuilder<T>&)` 를 특수화해 필드를 등록한다.
//   MYE_REFLECT(T)는 전방선언 + 자동 등록 스텁을 심는다(GetType<T>() 최초 호출 시 1회 등록).
//
// M3-A 범위: struct 필드 등록·버전·어트리뷰트·RenamedFrom(최소 마이그레이션)·Base<U> 상속
//   평탄화·enum 등록·std::vector<E> 자동 지원. 커스텀 Migrate 훅 고도화는 후속(첫 스키마 변경 시).
#pragma once

#include "mye/refl/Attribute.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/core/Base.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mye::refl {

template <typename T> class TypeBuilder;   // 아래에서 정의

// 각 타입이 특수화한다(선언만 — 본문은 소비 .cpp의 등록 코드). 기본은 미정의.
template <typename T> void Reflect(TypeBuilder<T>& b);

// 커스텀 마이그레이션 훅(M3-A: 계약만 — 실사용은 첫 실제 스키마 변경 시점).
//   fromVersion 데이터를 현재 필드 레이아웃으로 끌어올린다.
using MigrationFn = void (*)(void* instance, std::uint32_t fromVersion);

// -----------------------------------------------------------------------------
// struct 타입 빌더 — Reflect<T> 특수화 안에서 체이닝 호출.
//   b.Version(2).Field("x", &Foo::x).Attr(Attribute::MakeRange(0,1)).Field(...);
// -----------------------------------------------------------------------------
template <typename T>
class TypeBuilder {
public:
    // name 은 정본 이름(TypeId 계산 근거). 보통 MYE_REFLECT가 전달한다.
    explicit TypeBuilder(std::string_view name);

    TypeBuilder& Version(std::uint32_t v);

    // 멤버 포인터로 필드 등록 — 오프셋·타입을 컴파일타임에 확정. 필드 타입 F는 리플렉션
    //   등록 타입이거나 원시형/enum/std::vector/AssetRef 여야 한다(GetType<F>()로 해석).
    template <typename F>
    TypeBuilder& Field(std::string_view name, F T::* member);

    // 직전 Field에 어트리뷰트 부착(여러 개 체이닝 가능). Field 이전 호출은 무시(로그 경고).
    TypeBuilder& Attr(Attribute a);

    // 직전 Field의 옛 이름(버전드 rename) — 구버전 JSON 키 폴백. M3-A 최소 마이그레이션.
    TypeBuilder& RenamedFrom(std::uint32_t sinceVersion, std::string_view oldName);

    // 커스텀 마이그레이션 훅(계약만 — M3-A는 기록만 하고 실행 로직은 후속).
    TypeBuilder& Migrate(std::uint32_t fromVersion, MigrationFn fn);

    // 상속 평탄화 — 베이스 U의 필드를 이 타입 앞에 포함(U도 리플렉션 등록 필요).
    template <typename U>
    TypeBuilder& Base();

    // 커스텀 직렬화 훅 — 기본 필드 순회 대신 사용(예: Vec2를 배열로). 선택.
    TypeBuilder& CustomSerialize(CustomSerializeFn fn);

    // 빌드 완료 → TypeRegistry에 등록. GetType<T>() 자동 등록 스텁이 호출한다.
    Expected<const TypeInfo*, Error> Commit();

private:
    std::unique_ptr<TypeInfo> m_info;
    bool m_hasField = false;   // Attr/RenamedFrom 대상 존재 여부
};

// -----------------------------------------------------------------------------
// enum 빌더 — 정수 하부 타입 + 상수 등록.
//   EnumBuilder<Dir> b("mye::Dir"); b.Value("North", Dir::North). ... .Commit();
// -----------------------------------------------------------------------------
class EnumBuilderBase {
public:
    EnumBuilderBase(std::string_view name, std::size_t size, std::size_t align);
    EnumBuilderBase& Value(std::string_view name, std::int64_t value);
    Expected<const TypeInfo*, Error> Commit();
protected:
    std::unique_ptr<TypeInfo> m_info;
};

template <typename E>
class EnumBuilder final : public EnumBuilderBase {
public:
    explicit EnumBuilder(std::string_view name)
        : EnumBuilderBase(name, sizeof(E), alignof(E)) {}
    EnumBuilder& Value(std::string_view n, E v) {
        EnumBuilderBase::Value(n, static_cast<std::int64_t>(v));
        return *this;
    }
};

} // namespace mye::refl

// -----------------------------------------------------------------------------
// 보조 매크로 — 비침투 등록의 보일러플레이트 축소.
//   MYE_REFLECT(mye::asset::SpriteSheet)  // 헤더/소스 전역 스코프. TypeTag 이름·자동 등록 스텁.
//   template<> void mye::refl::Reflect(mye::refl::TypeBuilder<T>& b){ b.Version(1).Field(...); }
//
// GetType<T>() 최초 호출 시 Reflect<T> 를 돌려 1회 등록(스레드 세이프 함수-로컬 static).
// -----------------------------------------------------------------------------
#define MYE_REFLECT(Type)                                                        \
    MYE_REFLECT_NAME(Type, #Type);                                               \
    namespace mye::refl {                                                        \
        template <> void Reflect<Type>(TypeBuilder<Type>& b);                    \
    }

// 템플릿 진입점(GetType<T>·TypeBuilder<T> 본문) 정의. 계약 헤더는 선언만, 구현은 detail.
#include "mye/refl/detail/GetType.h"
