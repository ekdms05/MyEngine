// mye/refl/detail/GetType.h — GetType<T>() 컴파일타임 디스패치 + TypeBuilder<T> 템플릿 본문
//
// 이 헤더는 TypeBuilder.h / TypeRegistry.h 의 템플릿 진입점 구현을 담는다(계약 헤더는
// 선언만 노출). 소비자(.cpp)가 Reflect<T> 특수화를 작성한 뒤 GetType<T>()를 부르면
// 여기서 컴파일타임에 종류를 판정해 1회 등록한다:
//   - 원시형(bool/정수/부동/string)      → Kind::Primitive 지연 등록
//   - enum                               → Reflect(EnumBuilder<E>&) 특수화 호출
//   - std::vector<E>                     → Kind::Vector + 원소 타입 링크 + 타입소거 훅
//   - asset::AssetRef                    → Kind::AssetRef (전방선언만, 링크 없음)
//   - 그 외 struct                        → Reflect(TypeBuilder<T>&) 특수화 호출
//
// 스레드 세이프: 각 T의 등록은 함수-로컬 static 으로 1회. TypeRegistry::Register 는 중복
// TypeId 를 안전하게 병합(같은 이름이면 기존 반환)하므로 재진입에도 안정.
#pragma once

#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/refl/detail/TypeInfoAccess.h"

#include <cstdint>
#include <new>          // placement new
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>      // index_sequence
#include <vector>

namespace mye::asset { struct AssetRef; }

namespace mye::refl {

// enum 등록 커스터마이제이션 포인트 — MYE_REFLECT_ENUM 이 특수화 전방선언을 심는다.
template <typename E> void Reflect(EnumBuilder<E>& b);

namespace detail {

// ---- 종류 판정 트레이트 ----
template <typename T>
inline constexpr bool kIsPrimitive =
    std::is_same_v<T, bool> || std::is_same_v<T, std::int8_t> ||
    std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::int32_t> ||
    std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint8_t> ||
    std::is_same_v<T, std::uint16_t> || std::is_same_v<T, std::uint32_t> ||
    std::is_same_v<T, std::uint64_t> || std::is_same_v<T, float> ||
    std::is_same_v<T, double> || std::is_same_v<T, std::string>;

template <typename T> struct IsStdVector : std::false_type {};
template <typename E> struct IsStdVector<std::vector<E>> : std::true_type { using Element = E; };

// 원시형 TypeInfo 를 지연 생성해 레지스트리에 등록(Kind::Primitive).
template <typename T>
const TypeInfo* RegisterPrimitive() {
    auto info = std::make_unique<TypeInfo>();
    TypeInfoAccess::Init(*info, TypeTag<T>::Name(), Kind::Primitive, sizeof(T), alignof(T));
    auto r = TypeRegistry::Get().Register(std::move(info));
    return r ? r.Value() : nullptr;
}

// std::vector<E> TypeInfo 를 지연 생성(Kind::Vector + 원소 타입 + 타입소거 훅).
template <typename Vec>
const TypeInfo* RegisterVector() {
    using E = typename IsStdVector<Vec>::Element;
    const TypeInfo* elem = GetType<E>();   // 원소 타입 선등록(없으면 nullptr — Commit 시 진단)
    auto info = std::make_unique<TypeInfo>();
    // 이름은 "vector<원소이름>" — 안정 식별.
    static const std::string kName = std::string("vector<") + std::string(TypeTag<E>::Name()) + ">";
    TypeInfoAccess::Init(*info, kName, Kind::Vector, sizeof(Vec), alignof(Vec));
    TypeInfoAccess::SetElement(*info, elem);
    TypeInfoAccess::SetVectorHooks(*info,
        [](const void* v) -> std::size_t { return static_cast<const Vec*>(v)->size(); },
        [](void* v, std::size_t n) { static_cast<Vec*>(v)->resize(n); },
        [](void* v, std::size_t i) -> void* { return static_cast<void*>(&(*static_cast<Vec*>(v))[i]); },
        [](const void* v, std::size_t i) -> const void* {
            return static_cast<const void*>(&(*static_cast<const Vec*>(v))[i]);
        });
    TypeInfoAccess::SetConstruct(*info,
        [](void* mem) -> void* { return new (mem) Vec(); },
        [](void* obj) { static_cast<Vec*>(obj)->~Vec(); });
    auto r = TypeRegistry::Get().Register(std::move(info));
    return r ? r.Value() : nullptr;
}

// asset::AssetRef TypeInfo (Kind::AssetRef) — 전방선언만 사용(크기/정렬은 알려진 고정값).
//   AssetRef = { AssetGuid(16B) + AssetTypeId(8B) } = 24B, align 8. 링크 없이 값 의미만.
const TypeInfo* RegisterAssetRef();

// ---- 메서드 리플렉션: 멤버함수 포인터(NTTP) → 타입소거 호출 훅 ----
// 멤버함수 시그니처 분해(비-const/const 오버로드).
template <typename M> struct MemFnTraits;
template <typename C, typename R, typename... A>
struct MemFnTraits<R (C::*)(A...)> {
    using Class = C; using Ret = R; using ArgsTuple = std::tuple<A...>;
    static constexpr std::size_t Arity = sizeof...(A);
    static constexpr bool IsConst = false;
};
template <typename C, typename R, typename... A>
struct MemFnTraits<R (C::*)(A...) const> {
    using Class = C; using Ret = R; using ArgsTuple = std::tuple<A...>;
    static constexpr std::size_t Arity = sizeof...(A);
    static constexpr bool IsConst = true;
};

// 파라미터 값 타입(참조·cv 제거) — 호출 시 args[i] 를 이 타입 포인터로 역참조.
template <typename A>
using ArgValueT = std::remove_cv_t<std::remove_reference_t<A>>;

template <auto M, std::size_t... I>
void InvokeMethodImpl(void* inst, void** args, void* ret, std::index_sequence<I...>) {
    using Tr = MemFnTraits<decltype(M)>;
    using C  = typename Tr::Class;
    C* self = static_cast<C*>(inst);
    if constexpr (std::is_void_v<typename Tr::Ret>) {
        (void)ret;
        (self->*M)(*static_cast<ArgValueT<std::tuple_element_t<I, typename Tr::ArgsTuple>>*>(args[I])...);
    } else {
        *static_cast<ArgValueT<typename Tr::Ret>*>(ret) =
            (self->*M)(*static_cast<ArgValueT<std::tuple_element_t<I, typename Tr::ArgsTuple>>*>(args[I])...);
    }
}

template <auto M>
void InvokeMethodThunk(void* inst, void** args, void* ret) {
    using Tr = MemFnTraits<decltype(M)>;
    InvokeMethodImpl<M>(inst, args, ret, std::make_index_sequence<Tr::Arity>{});
}

// 파라미터 TypeInfo* 목록 수집(리플렉션 등록 타입/원시형).
template <typename Tuple, std::size_t... I>
std::vector<const TypeInfo*> CollectParamTypes(std::index_sequence<I...>) {
    std::vector<const TypeInfo*> v;
    v.reserve(sizeof...(I));
    (v.push_back(GetType<ArgValueT<std::tuple_element_t<I, Tuple>>>()), ...);
    return v;
}

} // namespace detail

// -----------------------------------------------------------------------------
// GetType<T>() — 컴파일타임 종류 판정 후 1회 등록. 03/05/06/07 표준 진입점.
// -----------------------------------------------------------------------------
template <typename T>
const TypeInfo* GetType() {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, mye::asset::AssetRef>) {
        static const TypeInfo* info = detail::RegisterAssetRef();
        return info;
    } else if constexpr (detail::kIsPrimitive<U>) {
        static const TypeInfo* info = detail::RegisterPrimitive<U>();
        return info;
    } else if constexpr (detail::IsStdVector<U>::value) {
        static const TypeInfo* info = detail::RegisterVector<U>();
        return info;
    } else if constexpr (std::is_enum_v<U>) {
        static const TypeInfo* info = [] {
            EnumBuilder<U> b(TypeTag<U>::Name());
            Reflect<U>(b);
            auto r = b.Commit();
            return r ? r.Value() : nullptr;
        }();
        return info;
    } else {
        static const TypeInfo* info = [] {
            TypeBuilder<U> b(TypeTag<U>::Name());
            Reflect<U>(b);
            auto r = b.Commit();
            return r ? r.Value() : nullptr;
        }();
        return info;
    }
}

// -----------------------------------------------------------------------------
// TypeBuilder<T> 템플릿 메서드 본문(계약 헤더는 선언만).
// -----------------------------------------------------------------------------
template <typename T>
TypeBuilder<T>::TypeBuilder(std::string_view name)
    : m_info(std::make_unique<TypeInfo>()) {
    TypeInfoAccess::Init(*m_info, name, Kind::Struct, sizeof(T), alignof(T));
    TypeInfoAccess::SetConstruct(*m_info,
        [](void* mem) -> void* { return new (mem) T(); },
        [](void* obj) { static_cast<T*>(obj)->~T(); });
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::Version(std::uint32_t v) {
    TypeInfoAccess::SetVersion(*m_info, v);
    return *this;
}

template <typename T>
template <typename F>
TypeBuilder<T>& TypeBuilder<T>::Field(std::string_view name, F T::* member) {
    const TypeInfo* fieldType = GetType<F>();
    const std::size_t offset =
        reinterpret_cast<std::size_t>(&(reinterpret_cast<T const volatile*>(0)->*member));
    TypeInfoAccess::AddField(*m_info, name, fieldType, offset);
    m_hasField = true;
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::Attr(Attribute a) {
    if (m_hasField) TypeInfoAccess::AddAttrToLastField(*m_info, std::move(a));
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::RenamedFrom(std::uint32_t sinceVersion, std::string_view oldName) {
    if (m_hasField) TypeInfoAccess::SetLastFieldRename(*m_info, sinceVersion, oldName);
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::Migrate(std::uint32_t /*fromVersion*/, MigrationFn /*fn*/) {
    // M3-A: 계약만 — 기록/실행 로직은 첫 실제 스키마 변경 시점(후속).
    return *this;
}

template <typename T>
template <auto M>
TypeBuilder<T>& TypeBuilder<T>::Method(std::string_view name) {
    using Tr = detail::MemFnTraits<decltype(M)>;
    static_assert(std::is_base_of_v<typename Tr::Class, T> || std::is_same_v<typename Tr::Class, T>,
                  "TypeBuilder::Method<M>(): M must be a member function of T (or a base)");
    const TypeInfo* ret = nullptr;
    if constexpr (!std::is_void_v<typename Tr::Ret>)
        ret = GetType<detail::ArgValueT<typename Tr::Ret>>();
    std::vector<const TypeInfo*> params =
        detail::CollectParamTypes<typename Tr::ArgsTuple>(std::make_index_sequence<Tr::Arity>{});
    TypeInfoAccess::AddMethod(*m_info, name, ret, std::move(params),
                              &detail::InvokeMethodThunk<M>, Tr::IsConst);
    return *this;
}

template <typename T>
template <typename U>
TypeBuilder<T>& TypeBuilder<T>::Base() {
    const TypeInfo* base = GetType<U>();
    // 베이스 U 의 필드를 이 타입 앞에 평탄화 포함(오프셋은 U 서브오브젝트 기준 = 0 가정:
    //   단일 상속·표준 레이아웃 전제. static_assert 로 가드).
    static_assert(std::is_base_of_v<U, T>, "TypeBuilder::Base<U>(): U must be a base of T");
    TypeInfoAccess::PrependBaseFields(*m_info, base);
    return *this;
}

template <typename T>
TypeBuilder<T>& TypeBuilder<T>::CustomSerialize(CustomSerializeFn fn) {
    TypeInfoAccess::SetCustomSerialize(*m_info, fn);
    return *this;
}

template <typename T>
Expected<const TypeInfo*, Error> TypeBuilder<T>::Commit() {
    return TypeInfoAccess::CommitStruct(std::move(m_info));
}

} // namespace mye::refl

// -----------------------------------------------------------------------------
// enum 등록 보조 매크로 — 비침투 등록의 보일러플레이트 축소(struct용 MYE_REFLECT의 짝).
//   MYE_REFLECT_ENUM(mye::Dir)  // 전역 스코프. TypeTag 이름 + Reflect(EnumBuilder<E>&) 전방선언.
//   template<> void mye::refl::Reflect(mye::refl::EnumBuilder<Dir>& b){ b.Value("N", Dir::N)... ; }
// -----------------------------------------------------------------------------
#define MYE_REFLECT_ENUM(Type)                                                   \
    MYE_REFLECT_NAME(Type, #Type);                                               \
    namespace mye::refl {                                                        \
        template <> void Reflect<Type>(EnumBuilder<Type>& b);                    \
    }
