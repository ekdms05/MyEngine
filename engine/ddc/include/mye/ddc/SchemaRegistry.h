// mye/ddc/SchemaRegistry.h — 데이터드리븐 컴포넌트 스키마 레지스트리 (엔진 확장성)
//
// 스키마를 이름/id 로 등록·조회한다. 인스턴스(DynamicComponent)가 스키마 포인터를 참조하므로
// 스키마는 안정 주소로 보관한다(unique_ptr 소유). 콘텐츠 JSON(스키마 배열) 로드 진입점.
#pragma once

#include "mye/ddc/ComponentSchema.h"

#include <memory>
#include <string_view>
#include <vector>

namespace mye::json { class Value; }

namespace mye::ddc {

class SchemaRegistry {
public:
    // 스키마 등록(복사 소유). 이름 중복이면 실패(첫 정의 유지). 성공 시 안정 포인터 반환.
    Expected<const ComponentSchema*, Error> Register(const ComponentSchema& schema);

    const ComponentSchema* Find(std::string_view name) const;
    const ComponentSchema* Find(SchemaId id) const;
    size_t Count() const { return m_schemas.size(); }

    // 스키마 배열 JSON 로드: [ { name, fields:[...] }, ... ]. 부분 실패 시 첫 오류 반환(그 전까지는 등록됨).
    Expected<int, Error> LoadFromJson(const json::Value& arr);   // 반환=등록 개수

    // 편의: 이름으로 인스턴스 생성(없으면 schema=null 인 빈 컴포넌트).
    DynamicComponent Instantiate(std::string_view name) const;

private:
    std::vector<std::unique_ptr<ComponentSchema>> m_schemas;   // 안정 주소 소유
};

} // namespace mye::ddc
