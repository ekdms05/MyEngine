// mye/script/src/bindings/DdcBindings.cpp — 데이터드리븐 컴포넌트 Lua 바인딩 (엔진 확장성)
//
// 게임 로직 전체를 데이터+Lua 로: 컴포넌트를 데이터로 정의(mye.ddc.define), 엔티티에 부착
// (mye.ddc.attach), Lua 함수 시스템 등록(mye.system) → Tick 이 매 프레임 실행. 엔진 무수정.
// throw 금지(ReflectBindings 와 동일한 안전 규약: 오류는 nil/false/no-op + 로그).
#include "mye/script/bindings/EngineBindings.h"

#include "mye/ddc/DynamicComponentStore.h"
#include "mye/core/Json.h"
#include "mye/core/Log.h"

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace mye::script {

namespace {

// 필드 get/set 공용 헬퍼(값 인스턴스·엔티티 참조 양쪽 재사용).
sol::object GetField(sol::state_view lua, const ddc::DynamicComponent* c, const std::string& field) {
    if (!c || !c->Schema()) return sol::make_object(lua, sol::nil);
    const ddc::FieldDef* f = c->Schema()->FindField(field);
    if (!f) return sol::make_object(lua, sol::nil);
    switch (f->type) {
        case ddc::FieldType::Bool:   return sol::make_object(lua, c->GetBool(field));
        case ddc::FieldType::F32:
        case ddc::FieldType::F64:    return sol::make_object(lua, c->GetFloat(field));
        case ddc::FieldType::String: return sol::make_object(lua, c->GetString(field));
        default:                     return sol::make_object(lua, static_cast<std::int64_t>(c->GetInt(field)));
    }
}

bool SetField(ddc::DynamicComponent* c, const std::string& field, const sol::stack_object& v) {
    if (!c || !c->Schema()) return false;
    const ddc::FieldDef* f = c->Schema()->FindField(field);
    if (!f) { MYE_LOG_WARN("Script", "ddc: 필드 없음 '{}'", field); return false; }
    switch (f->type) {
        case ddc::FieldType::Bool:   return v.is<bool>()        && c->SetBool(field, v.as<bool>());
        case ddc::FieldType::F32:
        case ddc::FieldType::F64:    return v.is<double>()      && c->SetFloat(field, v.as<double>());
        case ddc::FieldType::String: return v.is<std::string>() && c->SetString(field, v.as<std::string>());
        default:                     return v.is<std::int64_t>() ? c->SetInt(field, v.as<std::int64_t>())
                                            : (v.is<double>() && c->SetInt(field, static_cast<int64_t>(v.as<double>())));
    }
}

// 엔티티 부착 컴포넌트 참조 — 매 접근마다 스토어에서 재해석(삭제/재할당 안전).
struct LuaDynRef {
    ddc::DynamicComponentStore* store = nullptr;
    ddc::EntityId               e = 0;
    std::string                 schema;
    ddc::DynamicComponent* Resolve() const { return store ? store->Get(e, schema) : nullptr; }
};

} // namespace

DdcBindingModule::DdcBindingModule()
    : m_registry(std::make_unique<ddc::SchemaRegistry>()),
      m_store(std::make_unique<ddc::DynamicComponentStore>()) {}
DdcBindingModule::~DdcBindingModule() = default;
ddc::SchemaRegistry&        DdcBindingModule::Registry() { return *m_registry; }
ddc::DynamicComponentStore& DdcBindingModule::Store()    { return *m_store; }

void DdcBindingModule::Register(sol::state& lua) {
    m_lua = &lua;
    ddc::SchemaRegistry*        reg   = m_registry.get();
    ddc::DynamicComponentStore* store = m_store.get();
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- DynComponent usertype (값 의미 — 독립 인스턴스) ----
    mye.new_usertype<ddc::DynamicComponent>(
        "DynComponent", sol::no_constructor,
        "type_name", [](const ddc::DynamicComponent& c) { return std::string(c.Schema() ? c.Schema()->Name() : std::string{}); },
        "has", [](const ddc::DynamicComponent& c, const std::string& field) { return c.Has(field); },
        "get", [](sol::this_state ts, const ddc::DynamicComponent& c, const std::string& field) -> sol::object {
            return GetField(sol::state_view(ts), &c, field);
        },
        "set", [](ddc::DynamicComponent& c, const std::string& field, sol::stack_object v) -> bool {
            return SetField(&c, field, v);
        });

    // ---- DynRef usertype (엔티티 부착 참조 — 시스템이 제자리 변경) ----
    mye.new_usertype<LuaDynRef>(
        "DynRef", sol::no_constructor,
        "valid",     [](const LuaDynRef& r) { return r.Resolve() != nullptr; },
        "entity",    [](const LuaDynRef& r) { return static_cast<std::int64_t>(r.e); },
        "type_name", [](const LuaDynRef& r) { return r.schema; },
        "has",       [](const LuaDynRef& r, const std::string& field) { auto* c = r.Resolve(); return c && c->Has(field); },
        "get",       [](sol::this_state ts, const LuaDynRef& r, const std::string& field) -> sol::object {
            return GetField(sol::state_view(ts), r.Resolve(), field);
        },
        "set",       [](const LuaDynRef& r, const std::string& field, sol::stack_object v) -> bool {
            return SetField(r.Resolve(), field, v);
        });

    // ---- mye.ddc 테이블 ----
    sol::table ddcTbl = mye["ddc"].get_or_create<sol::table>();

    ddcTbl.set_function("define", [reg](const std::string& jsonStr) -> bool {
        auto parsed = json::Parse(jsonStr);
        if (!parsed) { MYE_LOG_WARN("Script", "ddc.define: JSON 파싱 실패"); return false; }
        auto schema = ddc::ComponentSchema::FromJson(parsed.Value());
        if (!schema) { MYE_LOG_WARN("Script", "ddc.define: 스키마 오류 {}", schema.GetError().message); return false; }
        auto r = reg->Register(schema.Value());
        if (!r) { MYE_LOG_WARN("Script", "ddc.define: 등록 실패 {}", r.GetError().message); return false; }
        return true;
    });

    ddcTbl.set_function("new", [reg](sol::this_state ts, const std::string& name) -> sol::object {
        sol::state_view lua(ts);
        const ddc::ComponentSchema* s = reg->Find(name);
        if (!s) return sol::make_object(lua, sol::nil);
        return sol::make_object(lua, s->Instantiate());
    });

    ddcTbl.set_function("has_schema", [reg](const std::string& name) { return reg->Find(name) != nullptr; });

    // 엔티티에 컴포넌트 부착 → 참조 핸들(스키마 미등록이면 nil).
    ddcTbl.set_function("attach", [reg, store](sol::this_state ts, std::int64_t e, const std::string& name) -> sol::object {
        sol::state_view lua(ts);
        ddc::DynamicComponent* c = store->Add(static_cast<ddc::EntityId>(e), name, *reg);
        if (!c) return sol::make_object(lua, sol::nil);
        return sol::make_object(lua, LuaDynRef{ store, static_cast<ddc::EntityId>(e), name });
    });

    // 엔티티의 컴포넌트 참조(없으면 nil).
    ddcTbl.set_function("get", [store](sol::this_state ts, std::int64_t e, const std::string& name) -> sol::object {
        sol::state_view lua(ts);
        if (!store->Get(static_cast<ddc::EntityId>(e), name)) return sol::make_object(lua, sol::nil);
        return sol::make_object(lua, LuaDynRef{ store, static_cast<ddc::EntityId>(e), name });
    });

    ddcTbl.set_function("remove", [store](std::int64_t e, const std::string& name) {
        return store->Remove(static_cast<ddc::EntityId>(e), name);
    });
    ddcTbl.set_function("has", [store](std::int64_t e, const std::string& name) {
        return store->Has(static_cast<ddc::EntityId>(e), name);
    });

    // ---- mye.system(name, fn) — Lua 시스템 등록(Lua 소유 테이블에 보관; sol 객체 C++ 저장 금지) ----
    lua.safe_script(R"(
        mye.__ddc_systems = mye.__ddc_systems or {}
        function mye.system(name, fn)
            local t = mye.__ddc_systems[name]
            if not t then t = {}; mye.__ddc_systems[name] = t end
            t[#t + 1] = fn
        end
    )", sol::script_pass_on_error);
}

void DdcBindingModule::Tick(float dt) {
    if (!m_lua) return;
    sol::state_view lua(*m_lua);
    sol::object sysObj = lua["mye"]["__ddc_systems"];
    if (!sysObj.is<sol::table>()) return;
    sol::table systems = sysObj.as<sol::table>();

    for (auto& kv : systems) {
        if (!kv.first.is<std::string>() || !kv.second.is<sol::table>()) continue;
        const std::string name = kv.first.as<std::string>();

        // 시스템 함수 스냅샷(Tick 지역 — 상태 살아있는 동안만, C++ 멤버 저장 아님).
        std::vector<sol::protected_function> fns;
        for (auto& fkv : kv.second.as<sol::table>())
            if (fkv.second.is<sol::protected_function>()) fns.push_back(fkv.second.as<sol::protected_function>());
        if (fns.empty()) continue;

        // 엔티티 스냅샷(시스템이 부착/제거해도 반복 무효화 없음).
        std::vector<ddc::EntityId> ids;
        m_store->ForEach(name, [&](ddc::EntityId e, ddc::DynamicComponent&) { ids.push_back(e); });

        for (ddc::EntityId e : ids) {
            for (sol::protected_function& fn : fns) {
                LuaDynRef ref{ m_store.get(), e, name };
                sol::protected_function_result r = fn(static_cast<std::int64_t>(e), ref, dt);
                if (!r.valid()) {
                    sol::error err = r;
                    MYE_LOG_ERROR("Script", "system '{}' 실행 오류: {}", name, err.what());
                }
            }
        }
    }
}

} // namespace mye::script
