// mye/script/src/bindings/DdcBindings.cpp — 데이터드리븐 컴포넌트 Lua 바인딩 (엔진 확장성)
//
// mye.ddc.define(json)/new(name) + DynComponent:get/set/has/type_name. 게임이 데이터로 컴포넌트를
// 정의하고 Lua 로 조작(엔진 무수정). throw 금지(ReflectBindings 와 동일한 안전 규약).
#include "mye/script/bindings/EngineBindings.h"

#include "mye/ddc/SchemaRegistry.h"
#include "mye/core/Json.h"
#include "mye/core/Log.h"

#include <sol/sol.hpp>

#include <string>

namespace mye::script {

DdcBindingModule::DdcBindingModule() : m_registry(std::make_unique<ddc::SchemaRegistry>()) {}
DdcBindingModule::~DdcBindingModule() = default;
ddc::SchemaRegistry& DdcBindingModule::Registry() { return *m_registry; }

void DdcBindingModule::Register(sol::state& lua) {
    ddc::SchemaRegistry* reg = m_registry.get();
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- DynComponent usertype (값 의미 — 각 Lua 핸들은 독립 인스턴스) ----
    mye.new_usertype<ddc::DynamicComponent>(
        "DynComponent",
        sol::no_constructor,

        "type_name",
        [](const ddc::DynamicComponent& c) {
            return std::string(c.Schema() ? c.Schema()->Name() : std::string{});
        },

        "has",
        [](const ddc::DynamicComponent& c, const std::string& field) { return c.Has(field); },

        // 필드 get: 필드 타입에 맞는 Lua 값. 없으면 nil.
        "get",
        [](sol::this_state ts, const ddc::DynamicComponent& c, const std::string& field) -> sol::object {
            sol::state_view lua(ts);
            const ddc::ComponentSchema* s = c.Schema();
            if (!s) return sol::make_object(lua, sol::nil);
            const ddc::FieldDef* f = s->FindField(field);
            if (!f) return sol::make_object(lua, sol::nil);
            switch (f->type) {
                case ddc::FieldType::Bool:   return sol::make_object(lua, c.GetBool(field));
                case ddc::FieldType::F32:
                case ddc::FieldType::F64:    return sol::make_object(lua, c.GetFloat(field));
                case ddc::FieldType::String: return sol::make_object(lua, c.GetString(field));
                default:                     return sol::make_object(lua, static_cast<std::int64_t>(c.GetInt(field)));
            }
        },

        // 필드 set: Lua 값 타입을 필드 타입에 맞춰 반영. 성공 시 true(오류는 false, throw 금지).
        "set",
        [](ddc::DynamicComponent& c, const std::string& field, sol::stack_object v) -> bool {
            const ddc::ComponentSchema* s = c.Schema();
            if (!s) return false;
            const ddc::FieldDef* f = s->FindField(field);
            if (!f) { MYE_LOG_WARN("Script", "ddc: 필드 없음 '{}'", field); return false; }
            switch (f->type) {
                case ddc::FieldType::Bool:   return v.is<bool>()        && c.SetBool(field, v.as<bool>());
                case ddc::FieldType::F32:
                case ddc::FieldType::F64:    return v.is<double>()      && c.SetFloat(field, v.as<double>());
                case ddc::FieldType::String: return v.is<std::string>() && c.SetString(field, v.as<std::string>());
                default:                     return v.is<std::int64_t>() ? c.SetInt(field, v.as<std::int64_t>())
                                                    : (v.is<double>() && c.SetInt(field, static_cast<int64_t>(v.as<double>())));
            }
        });

    // ---- mye.ddc 테이블 ----
    sol::table ddcTbl = mye["ddc"].get_or_create<sol::table>();

    // 스키마를 JSON 문자열로 정의(런타임 등록). 성공 시 true.
    ddcTbl.set_function("define", [reg](const std::string& jsonStr) -> bool {
        auto parsed = json::Parse(jsonStr);
        if (!parsed) { MYE_LOG_WARN("Script", "ddc.define: JSON 파싱 실패"); return false; }
        auto schema = ddc::ComponentSchema::FromJson(parsed.Value());
        if (!schema) { MYE_LOG_WARN("Script", "ddc.define: 스키마 오류 {}", schema.GetError().message); return false; }
        auto r = reg->Register(schema.Value());
        if (!r) { MYE_LOG_WARN("Script", "ddc.define: 등록 실패 {}", r.GetError().message); return false; }
        return true;
    });

    // 등록 스키마 인스턴스 생성. 미등록이면 nil.
    ddcTbl.set_function("new", [reg](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view lua(ts);
        const ddc::ComponentSchema* s = reg->Find(name);
        if (!s) return sol::make_object(lua, sol::nil);
        return sol::make_object(lua, s->Instantiate());
    });

    ddcTbl.set_function("has_schema", [reg](const std::string& name) { return reg->Find(name) != nullptr; });
}

} // namespace mye::script
