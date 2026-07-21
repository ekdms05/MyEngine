// mye/core/Json.h — 코어 내장 최소 JSON 리더 (docs/01 설정 시스템 전용)
//
// 목적: ConfigSystem 부트스트랩용. 04의 리플렉션 직렬화(L2)를 코어(L0)가 쓸 수 없어
// 의존성 역전을 피하기 위해 최소 파서를 자체 보유한다. 쓰기(직렬화)는 Save에 필요한
// 최소한만 제공. 범위: RFC 8259 서브셋 (주석 미지원 — 포맷 완화는 오픈 이슈 A3).
#pragma once

#include "mye/core/Base.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mye::json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

class Value {
public:
    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;   // 키 순서 불보장

    Value() = default;
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    Value(double n) : m_type(Type::Number), m_number(n) {}
    Value(int64_t n) : m_type(Type::Number), m_number(static_cast<double>(n)) {}
    Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {}
    Value(Array a) : m_type(Type::Array), m_array(std::move(a)) {}
    Value(Object o) : m_type(Type::Object), m_object(std::move(o)) {}

    Type GetType() const { return m_type; }
    bool IsNull()   const { return m_type == Type::Null; }
    bool IsBool()   const { return m_type == Type::Bool; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsArray()  const { return m_type == Type::Array; }
    bool IsObject() const { return m_type == Type::Object; }

    // 타입 불일치 시 fallback 반환 (관용적 소비 — 설정 로드 용도)
    bool        AsBool(bool fallback = false) const { return IsBool() ? m_bool : fallback; }
    double      AsDouble(double fallback = 0.0) const { return IsNumber() ? m_number : fallback; }
    int64_t     AsInt(int64_t fallback = 0) const {
        return IsNumber() ? static_cast<int64_t>(m_number) : fallback;
    }
    std::string_view AsString(std::string_view fallback = {}) const {
        return IsString() ? std::string_view(m_string) : fallback;
    }

    const Array&  AsArray()  const { return m_array; }    // Array 아니면 빈 배열
    const Object& AsObject() const { return m_object; }   // Object 아니면 빈 오브젝트

    // Object 멤버 조회. 없으면 nullptr
    const Value* Find(std::string_view key) const;

private:
    Type        m_type = Type::Null;
    bool        m_bool = false;
    double      m_number = 0.0;
    std::string m_string;
    Array       m_array;
    Object      m_object;
};

// UTF-8 텍스트 파싱. 실패 시 Error.message에 위치·원인 포함.  구현: src/Json.cpp
Expected<Value, Error> Parse(std::string_view utf8Text);

// 최소 직렬화 (ConfigSystem::Save 용도 — pretty print 고정)
std::string Stringify(const Value& value, int indent = 2);

} // namespace mye::json
