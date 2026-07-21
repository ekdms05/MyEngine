// mye/refl/TypeInfo.h — 리플렉션 메타데이터: TypeInfo·FieldInfo·EnumInfo (docs/04 §리플렉션)
//
// 비침투적 Reflect<T> 특수화가 TypeBuilder로 채운 결과를 TypeInfo로 굳혀 TypeRegistry에
// 등록한다. 소비자(03 프리팹·05 Lua·06 세이브·07 인스펙터)는 전부 이 하나의 메타데이터를
// 공유한다(지금은 계약만, 실사용은 후속). ser::IArchive 가 이 메타를 걷어 직렬화한다.
//
// M3-A 범위: 구조체 필드(오프셋·타입·어트리뷰트)·enum·기본 컨테이너(vector/AssetRef)·버전.
//   메서드 리플렉션(MethodInfo)은 05 Lua 착수 시점(후속)으로 미룬다 — 전방선언만 남긴다.
#pragma once

#include "mye/refl/Attribute.h"
#include "mye/refl/TypeId.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mye::ser { class IArchive; }   // 커스텀 Serialize 훅 서명에 사용

namespace mye::refl {

class TypeInfo;
struct TypeInfoAccess;   // detail/TypeInfoAccess.h — 빌더가 private 멤버를 채우는 접근 헬퍼

// 타입의 대분류 — 아카이브가 필드를 어떻게 순회할지 결정한다.
enum class Kind : std::uint8_t {
    Primitive,   // bool/정수/부동/string — IArchive::Value 프리미티브로 직결
    Enum,        // 하부 정수 + 이름↔값 테이블
    Struct,      // 필드 목록(리플렉션 순회)
    Vector,      // 가변 컨테이너(std::vector<E>) — element() 타입 반복
    AssetRef,    // asset::AssetRef 특별 취급(의존성 수집)
};

// enum 상수 하나(name↔정수 값). EnumInfo가 보유.
struct EnumConstant {
    std::string_view name;
    std::int64_t     value = 0;
};

// enum 메타 — 하부 정수 타입 + 상수 테이블. TypeInfo::AsEnum()으로 접근.
class EnumInfo {
public:
    std::span<const EnumConstant> Constants() const { return m_constants; }
    // 이름↔값 변환(직렬화가 JSON 문자열 ↔ 정수로 왕복). 실패 시 false.
    bool ValueOf(std::string_view name, std::int64_t& out) const;
    bool NameOf(std::int64_t value, std::string_view& out) const;

private:
    friend class TypeRegistry;
    friend class EnumBuilderBase;
    friend struct TypeInfoAccess;
    std::vector<EnumConstant> m_constants;
};

// 구조체 필드 하나 — 이름·타입·오프셋·어트리뷰트. 인스펙터·직렬화·Lua가 공용 사용.
class FieldInfo {
public:
    std::string_view Name() const { return m_name; }
    const TypeInfo&  Type() const { return *m_type; }
    std::span<const Attribute> Attributes() const { return m_attributes; }

    // 인스턴스 내 이 필드의 주소 — 인스펙터·직렬화·Lua 공용. instance는 소유 struct 포인터.
    void*       GetPtr(void* instance) const {
        return static_cast<void*>(static_cast<std::byte*>(instance) + m_offset);
    }
    const void* GetPtr(const void* instance) const {
        return static_cast<const void*>(static_cast<const std::byte*>(instance) + m_offset);
    }
    std::size_t Offset() const { return m_offset; }

    // 어트리뷰트 조회 헬퍼(07 인스펙터·05 Lua). 없으면 nullptr.
    const Attribute* FindAttribute(AttributeKind kind) const;
    bool HasAttribute(AttributeKind kind) const { return FindAttribute(kind) != nullptr; }

    // ---- 버전드 필드 마이그레이션(M3-A: 최소 — rename만) ----
    // 이 필드가 sinceVersion 이전 데이터에서 가졌던 옛 이름(JSON 키 폴백). 비어있으면 없음.
    std::string_view RenamedFrom() const { return m_renamedFrom; }
    std::uint32_t    RenamedSince() const { return m_renamedSince; }

private:
    friend class TypeRegistry;
    template <typename T> friend class TypeBuilder;
    friend struct TypeInfoAccess;

    std::string_view       m_name;
    const TypeInfo*        m_type = nullptr;
    std::size_t            m_offset = 0;
    std::vector<Attribute> m_attributes;
    std::string_view       m_renamedFrom;    // 빈 뷰 = 없음
    std::uint32_t          m_renamedSince = 0;
};

// 커스텀 직렬화 훅 서명 — 리플렉션 기본 순회를 대신하고 싶은 타입용(예: Vec2를 [x,y]로).
//   기본 필드 순회로 충분하면 등록하지 않는다.
using CustomSerializeFn = void (*)(ser::IArchive& ar, void* instance);

// 기본 생성 훅(프리팹·Lua 인스턴스화). placement new 로 mem에 T{} 구성.
using ConstructFn = void* (*)(void* mem);
using DestructFn  = void  (*)(void* obj);

// 하나의 리플렉션 타입 메타데이터. TypeRegistry가 소유(프로세스 수명).
class TypeInfo {
public:
    std::string_view Name() const { return m_name; }
    TypeId           Id() const { return m_id; }
    std::uint32_t    Version() const { return m_version; }
    Kind             GetKind() const { return m_kind; }
    std::size_t      Size() const { return m_size; }
    std::size_t      Align() const { return m_align; }

    // Struct 전용 — 상속 평탄화된 전체 필드(베이스 먼저, 파생 나중).
    std::span<const FieldInfo> Fields() const { return m_fields; }
    const FieldInfo* FindField(std::string_view name) const;

    // Enum 전용 — kind==Enum 이 아니면 nullptr.
    const EnumInfo* AsEnum() const { return m_kind == Kind::Enum ? &m_enum : nullptr; }

    // Vector 전용 — 원소 타입. kind!=Vector 이면 nullptr.
    const TypeInfo* ElementType() const { return m_element; }
    // Vector 런타임 조작(직렬화가 사용) — 미등록 시 nullptr 훅.
    std::size_t VectorSize(const void* vec) const;
    void        VectorResize(void* vec, std::size_t n) const;
    void*       VectorElementAt(void* vec, std::size_t i) const;
    const void* VectorElementAt(const void* vec, std::size_t i) const;

    // 기본 생성/파괴 — 프리팹·Lua·직렬화 임시 인스턴스. mem은 Size()/Align() 정합 필요.
    void* Construct(void* mem) const { return m_construct ? m_construct(mem) : nullptr; }
    void  Destruct(void* obj) const { if (m_destruct) m_destruct(obj); }

    // 커스텀 직렬화 훅(없으면 nullptr → 아카이브는 기본 필드 순회).
    CustomSerializeFn CustomSerialize() const { return m_customSerialize; }

private:
    friend class TypeRegistry;
    template <typename T> friend class TypeBuilder;
    friend class EnumBuilderBase;
    friend struct TypeInfoAccess;

    std::string_view       m_name;
    TypeId                 m_id = 0;
    std::uint32_t          m_version = 0;
    Kind                   m_kind = Kind::Struct;
    std::size_t            m_size = 0;
    std::size_t            m_align = 0;

    std::vector<FieldInfo> m_fields;      // Struct
    EnumInfo               m_enum;        // Enum
    const TypeInfo*        m_element = nullptr;  // Vector 원소

    ConstructFn            m_construct = nullptr;
    DestructFn             m_destruct = nullptr;
    CustomSerializeFn      m_customSerialize = nullptr;

    // Vector 타입 소거 훅(TypeBuilder<std::vector<E>> 가 채움)
    std::size_t (*m_vecSize)(const void*) = nullptr;
    void        (*m_vecResize)(void*, std::size_t) = nullptr;
    void*       (*m_vecAt)(void*, std::size_t) = nullptr;
    const void* (*m_vecAtConst)(const void*, std::size_t) = nullptr;
};

// 전방선언(구현 계약은 아직 미정 — 05 Lua 착수 시 확정).
class MethodInfo;

} // namespace mye::refl
