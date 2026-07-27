// mye/refl/detail/TypeInfoAccess.h — TypeInfo/FieldInfo private 멤버 채우기 헬퍼
//
// TypeInfo·FieldInfo·EnumInfo 의 필드는 private(계약 헤더)이고 TypeInfoAccess 만 friend 로
// 열려 있다. 빌더(TypeBuilder<T>·RegisterVector 등)는 이 헬퍼를 경유해 메타를 채운다.
// 이렇게 하면 mutation API 를 공개 헤더에 노출하지 않으면서 free-function 등록 경로도 지원한다.
#pragma once

#include "mye/refl/Attribute.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"

#include <cstddef>
#include <string_view>
#include <utility>

namespace mye::refl {

struct TypeInfoAccess {
    static void Init(TypeInfo& t, std::string_view name, Kind kind,
                     std::size_t size, std::size_t align) {
        t.m_name = name;
        t.m_id = TypeIdFromName(name);
        t.m_kind = kind;
        t.m_size = size;
        t.m_align = align;
    }

    static void SetVersion(TypeInfo& t, std::uint32_t v) { t.m_version = v; }
    static void SetElement(TypeInfo& t, const TypeInfo* e) { t.m_element = e; }
    static void SetCustomSerialize(TypeInfo& t, CustomSerializeFn fn) { t.m_customSerialize = fn; }

    static void SetConstruct(TypeInfo& t, ConstructFn c, DestructFn d) {
        t.m_construct = c;
        t.m_destruct = d;
    }

    static void SetVectorHooks(TypeInfo& t,
                               std::size_t (*sz)(const void*),
                               void (*rs)(void*, std::size_t),
                               void* (*at)(void*, std::size_t),
                               const void* (*atc)(const void*, std::size_t)) {
        t.m_vecSize = sz;
        t.m_vecResize = rs;
        t.m_vecAt = at;
        t.m_vecAtConst = atc;
    }

    static void AddField(TypeInfo& t, std::string_view name,
                         const TypeInfo* type, std::size_t offset) {
        FieldInfo f;
        f.m_name = name;
        f.m_type = type;
        f.m_offset = offset;
        t.m_fields.push_back(std::move(f));
    }

    static void AddAttrToLastField(TypeInfo& t, Attribute a) {
        if (!t.m_fields.empty()) t.m_fields.back().m_attributes.push_back(std::move(a));
    }

    static void AddMethod(TypeInfo& t, std::string_view name, const TypeInfo* ret,
                          std::vector<const TypeInfo*> params, MethodInfo::InvokeFn fn, bool isConst) {
        MethodInfo m;
        m.m_name = name;
        m.m_returnType = ret;
        m.m_paramTypes = std::move(params);
        m.m_invoke = fn;
        m.m_isConst = isConst;
        t.m_methods.push_back(std::move(m));
    }

    static void SetLastFieldRename(TypeInfo& t, std::uint32_t since, std::string_view oldName) {
        if (!t.m_fields.empty()) {
            t.m_fields.back().m_renamedFrom = oldName;
            t.m_fields.back().m_renamedSince = since;
        }
    }

    // 베이스 타입의 필드를 이 타입 앞에 평탄화(단일 상속·표준 레이아웃 전제, 오프셋 0 기준).
    static void PrependBaseFields(TypeInfo& t, const TypeInfo* base) {
        if (!base) return;
        auto baseFields = base->Fields();
        t.m_fields.insert(t.m_fields.begin(), baseFields.begin(), baseFields.end());
    }

    static Expected<const TypeInfo*, Error> CommitStruct(std::unique_ptr<TypeInfo> info) {
        return TypeRegistry::Get().Register(std::move(info));
    }
};

} // namespace mye::refl
