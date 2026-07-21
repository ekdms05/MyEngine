// TypeBuilder.cpp — 등록 빌더 비템플릿 부분 (M3-A 골격 스텁)
//
// TypeBuilder<T> 자체는 헤더의 템플릿(소비자 .cpp에서 인스턴스화). 여기서는 enum 빌더의
// 비템플릿 베이스만 구현해 링크를 만족시킨다. 필드 등록의 타입 해석 로직은 구현 에이전트가 채운다.
#include "mye/refl/TypeBuilder.h"
#include "mye/refl/detail/TypeInfoAccess.h"

namespace mye::refl {

namespace detail {

// asset::AssetRef TypeInfo (Kind::AssetRef). reflect 는 asset 을 링크하지 않으므로
// 계약 고정 레이아웃(AssetGuid{hi,lo}+type:u64 = 24B, align 8)으로 크기/정렬만 등록한다.
const TypeInfo* RegisterAssetRef() {
    auto info = std::make_unique<TypeInfo>();
    TypeInfoAccess::Init(*info, "mye::asset::AssetRef", Kind::AssetRef,
                         /*size*/ 24, /*align*/ 8);
    auto r = TypeRegistry::Get().Register(std::move(info));
    return r ? r.Value() : nullptr;
}

} // namespace detail

EnumBuilderBase::EnumBuilderBase(std::string_view name, std::size_t size, std::size_t align)
    : m_info(std::make_unique<TypeInfo>()) {
    m_info->m_name = name;
    m_info->m_id = TypeIdFromName(name);
    m_info->m_kind = Kind::Enum;
    m_info->m_size = size;
    m_info->m_align = align;
}

EnumBuilderBase& EnumBuilderBase::Value(std::string_view name, std::int64_t value) {
    m_info->m_enum.m_constants.push_back(EnumConstant{name, value});
    return *this;
}

Expected<const TypeInfo*, Error> EnumBuilderBase::Commit() {
    return TypeRegistry::Get().Register(std::move(m_info));
}

} // namespace mye::refl
