// mye/ddc/ComponentSchema.h — 데이터드리븐 컴포넌트 스키마·인스턴스 (엔진 확장성)
//
// 게임이 엔진 재컴파일 없이 "컴포넌트 타입"을 데이터로 정의한다. 스키마는 이름 + 타입드 필드 목록
// (i32/i64/f32/f64/bool/string, 리플렉션 원시형 이름과 동일). 인스턴스(DynamicComponent)는 스키마의
// 각 필드 값을 보관하고 이름으로 get/set 한다. JSON 로드/저장으로 콘텐츠 파이프라인에 얹힌다.
//
// 고정 str/agi/int/vit 대신, 게임이 원하는 스탯/컴포넌트를 스키마로 선언 → 엔진은 범용 저장·직렬화만.
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mye::json { class Value; }

namespace mye::ddc {

using SchemaId = uint64_t;   // HashFnv1a64(schema name)

// 필드 타입(리플렉션 원시형 이름 규약 공유). Count 는 개수 마커.
enum class FieldType : uint8_t { I32, I64, F32, F64, Bool, String, Count };

// 타입 이름 ↔ enum (직렬화·스키마 파싱).
std::string_view FieldTypeName(FieldType t);
bool             FieldTypeFromName(std::string_view name, FieldType& out);

struct FieldDef {
    std::string name;
    FieldType   type = FieldType::I32;
    // 기본값(타입에 맞는 슬롯만 의미 있음).
    int64_t     defI = 0;
    double      defD = 0.0;
    std::string defS;
};

// 인스턴스 한 칸의 값(스키마 필드 타입에 따라 활성 슬롯 결정).
struct FieldValue {
    int64_t     i = 0;    // I32/I64/Bool
    double      d = 0.0;  // F32/F64
    std::string s;        // String
};

class ComponentSchema;

// 스키마 기반 런타임 컴포넌트 인스턴스. 이름으로 타입드 get/set(숫자는 관용 변환).
class DynamicComponent {
public:
    DynamicComponent() = default;
    const ComponentSchema* Schema() const { return m_schema; }

    // 타입드 접근(필드 없음/타입 불일치면 set=false, get=fallback).
    bool    SetInt(std::string_view field, int64_t v);
    int64_t GetInt(std::string_view field, int64_t fallback = 0) const;
    bool    SetFloat(std::string_view field, double v);
    double  GetFloat(std::string_view field, double fallback = 0.0) const;
    bool    SetBool(std::string_view field, bool v);
    bool    GetBool(std::string_view field, bool fallback = false) const;
    bool    SetString(std::string_view field, std::string_view v);
    std::string GetString(std::string_view field, std::string_view fallback = {}) const;

    bool Has(std::string_view field) const;

    // ---- 인스턴스 직렬화(월드 세이브) ----
    // 필드값을 { name: value } 오브젝트로. 스키마 없으면 null.
    json::Value ToJson() const;
    // 오브젝트에서 필드값 반영(스키마에 있는 필드만, 타입 맞춰). 없는 키는 무시.
    void ApplyJson(const json::Value& obj);

private:
    friend class ComponentSchema;
    const ComponentSchema*  m_schema = nullptr;
    std::vector<FieldValue> m_values;   // 스키마 필드와 1:1
};

// 데이터로 정의된 컴포넌트 타입.
class ComponentSchema {
public:
    ComponentSchema() = default;
    explicit ComponentSchema(std::string name);

    const std::string& Name() const { return m_name; }
    SchemaId           Id() const { return m_id; }
    const std::vector<FieldDef>& Fields() const { return m_fields; }

    // 필드 추가(빌더). 이름 중복은 무시(첫 정의 유지).
    ComponentSchema& AddField(const FieldDef& f);
    int  FieldIndex(std::string_view name) const;   // -1 = 없음
    const FieldDef* FindField(std::string_view name) const;

    // 기본값이 적용된 인스턴스 생성.
    DynamicComponent Instantiate() const;

    // ---- 직렬화 ----
    // { "name": "...", "fields": [ { "name","type","default" } ] }
    static Expected<ComponentSchema, Error> FromJson(const json::Value& v);
    json::Value ToJson() const;

private:
    std::string           m_name;
    SchemaId              m_id = 0;
    std::vector<FieldDef> m_fields;
};

} // namespace mye::ddc
