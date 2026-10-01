#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"
#include "mye/ddc/DynamicComponentStore.h"
#include "mye/core/Json.h"
#include "mye/core/Log.h"

#include <new>
#include <string>
#include <vector>

namespace mye::script {
namespace {
constexpr const char* kComponent = "mye.DynComponent";
constexpr const char* kRef = "mye.DynRef";
struct LuaDynRef {
    ddc::DynamicComponentStore* store = nullptr;
    ddc::EntityId entity = 0;
    std::string schema;
    ddc::DynamicComponent* Resolve() const { return store ? store->Get(entity, schema) : nullptr; }
};
ddc::DynamicComponent* Component(lua_State* L) {
    if (auto* value = luaL_testudata(L, 1, kComponent)) return static_cast<ddc::DynamicComponent*>(value);
    return static_cast<LuaDynRef*>(luaL_checkudata(L, 1, kRef))->Resolve();
}
void PushRef(lua_State* L, ddc::DynamicComponentStore* store, ddc::EntityId entity, const std::string& schema) {
    new (lua_newuserdatauv(L, sizeof(LuaDynRef), 0)) LuaDynRef{store, entity, schema};
    luaL_setmetatable(L, kRef);
}
int ComponentCollect(lua_State* L) { static_cast<ddc::DynamicComponent*>(luaL_checkudata(L, 1, kComponent))->~DynamicComponent(); return 0; }
int RefCollect(lua_State* L) { static_cast<LuaDynRef*>(luaL_checkudata(L, 1, kRef))->~LuaDynRef(); return 0; }
int Valid(lua_State* L) { lua_pushboolean(L, Component(L) != nullptr); return 1; }
int Entity(lua_State* L) { const auto* value = static_cast<LuaDynRef*>(luaL_checkudata(L, 1, kRef)); lua_pushinteger(L, static_cast<lua_Integer>(value->entity)); return 1; }
int TypeName(lua_State* L) {
    if (auto* value = luaL_testudata(L, 1, kRef)) {
        const auto& schema = static_cast<LuaDynRef*>(value)->schema; lua_pushlstring(L, schema.data(), schema.size()); return 1;
    }
    const auto* component = Component(L); const std::string name = component && component->Schema() ? component->Schema()->Name() : std::string{};
    lua_pushlstring(L, name.data(), name.size()); return 1;
}
int Has(lua_State* L) { const auto* component = Component(L); const char* field = luaL_checkstring(L, 2); lua_pushboolean(L, component && component->Has(field)); return 1; }
int Get(lua_State* L) {
    const auto* component = Component(L); const char* name = luaL_checkstring(L, 2);
    const auto* field = component && component->Schema() ? component->Schema()->FindField(name) : nullptr;
    if (!field) { lua_pushnil(L); return 1; }
    switch (field->type) {
        case ddc::FieldType::Bool: lua_pushboolean(L, component->GetBool(name)); break;
        case ddc::FieldType::F32:
        case ddc::FieldType::F64: lua_pushnumber(L, component->GetFloat(name)); break;
        case ddc::FieldType::String: { const auto str = component->GetString(name); lua_pushlstring(L, str.data(), str.size()); break; }
        default: lua_pushinteger(L, component->GetInt(name)); break;
    }
    return 1;
}
int Set(lua_State* L) {
    auto* component = Component(L); const char* name = luaL_checkstring(L, 2);
    const auto* field = component && component->Schema() ? component->Schema()->FindField(name) : nullptr;
    bool changed = false;
    if (field) {
        switch (field->type) {
            case ddc::FieldType::Bool: changed = lua_isboolean(L, 3) && component->SetBool(name, lua_toboolean(L, 3) != 0); break;
            case ddc::FieldType::F32:
            case ddc::FieldType::F64: changed = lua_type(L, 3) == LUA_TNUMBER && component->SetFloat(name, lua_tonumber(L, 3)); break;
            case ddc::FieldType::String: {
                if (lua_type(L, 3) == LUA_TSTRING) { size_t size = 0; const char* str = lua_tolstring(L, 3, &size); changed = component->SetString(name, std::string(str, size)); }
                break;
            }
            default: { int valid = 0; const auto value = lua_tointegerx(L, 3, &valid); changed = valid && component->SetInt(name, value); break; }
        }
    }
    lua_pushboolean(L, changed); return 1;
}
int System(lua_State* L) {
    luaL_checktype(L, 1, LUA_TSTRING); luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "__ddc_systems");
    const int systems = lua_gettop(L); lua_pushvalue(L, 1); lua_rawget(L, systems);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, 1); lua_pushvalue(L, -2); lua_rawset(L, systems); }
    lua_pushvalue(L, 2); lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
    return 0;
}
} // namespace

DdcBindingModule::DdcBindingModule() : m_registry(std::make_unique<ddc::SchemaRegistry>()), m_store(std::make_unique<ddc::DynamicComponentStore>()) {}
DdcBindingModule::~DdcBindingModule() = default;
ddc::SchemaRegistry& DdcBindingModule::Registry() { return *m_registry; }
ddc::DynamicComponentStore& DdcBindingModule::Store() { return *m_store; }
void DdcBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    const luaL_Reg methods[] = {{"type_name", TypeName}, {"has", Has}, {"get", Get}, {"set", Set}, {nullptr, nullptr}};
    luaL_newmetatable(L, kComponent);
    lua_pushcfunction(L, ComponentCollect); lua_setfield(L, -2, "__gc");
    // Finalizers belong to the VM, not the public method table: calling one
    // manually would destroy a C++ object again during garbage collection.
    lua_pushboolean(L, false); lua_setfield(L, -2, "__metatable");
    lua_newtable(L); luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index"); lua_pop(L, 1);
    luaL_newmetatable(L, kRef);
    lua_pushcfunction(L, RefCollect); lua_setfield(L, -2, "__gc");
    lua_pushboolean(L, false); lua_setfield(L, -2, "__metatable");
    lua_newtable(L); luaL_setfuncs(L, methods, 0);
    lua_pushcfunction(L, Valid); lua_setfield(L, -2, "valid");
    lua_pushcfunction(L, Entity); lua_setfield(L, -2, "entity");
    lua_setfield(L, -2, "__index"); lua_pop(L, 1);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "__ddc_systems"); m_systems = std::make_unique<LuaReference>(L, -1); lua_pop(L, 1);
    lua_pushcfunction(L, System); lua_setfield(L, -2, "system");
    EnsureTable(L, -1, "ddc");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<DdcBindingModule>(L); const char* source = luaL_checkstring(L, 1);
        auto parsed = json::Parse(source);
        if (!parsed) { MYE_LOG_WARN("Script", "ddc.define: JSON parse failed"); lua_pushboolean(L, false); return 1; }
        auto schema = ddc::ComponentSchema::FromJson(parsed.Value());
        if (!schema) { MYE_LOG_WARN("Script", "ddc.define: {}", schema.GetError().message); lua_pushboolean(L, false); return 1; }
        auto registered = self->m_registry->Register(schema.Value());
        if (!registered) MYE_LOG_WARN("Script", "ddc.define: {}", registered.GetError().message);
        lua_pushboolean(L, static_cast<bool>(registered)); return 1;
    }, this); lua_setfield(L, -2, "define");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* schema = Context<DdcBindingModule>(L)->m_registry->Find(luaL_checkstring(L, 1));
        if (!schema) { lua_pushnil(L); return 1; }
        new (lua_newuserdatauv(L, sizeof(ddc::DynamicComponent), 0)) ddc::DynamicComponent(schema->Instantiate());
        luaL_setmetatable(L, kComponent); return 1;
    }, this); lua_setfield(L, -2, "new");
    PushFunction(L, [](lua_State* L) -> int { lua_pushboolean(L, Context<DdcBindingModule>(L)->m_registry->Find(luaL_checkstring(L, 1)) != nullptr); return 1; }, this); lua_setfield(L, -2, "has_schema");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<DdcBindingModule>(L); const auto entity = CheckEntity(L, 1).Packed(); const std::string name = luaL_checkstring(L, 2);
        if (!self->m_store->Add(entity, name, *self->m_registry)) lua_pushnil(L); else PushRef(L, self->m_store.get(), entity, name);
        return 1;
    }, this); lua_setfield(L, -2, "attach");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<DdcBindingModule>(L); const auto entity = CheckEntity(L, 1).Packed(); const std::string name = luaL_checkstring(L, 2);
        if (!self->m_store->Get(entity, name)) lua_pushnil(L); else PushRef(L, self->m_store.get(), entity, name);
        return 1;
    }, this); lua_setfield(L, -2, "get");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<DdcBindingModule>(L); const auto entity = CheckEntity(L, 1).Packed(); const char* name = luaL_checkstring(L, 2);
        lua_pushboolean(L, self->m_store->Remove(entity, name)); return 1;
    }, this); lua_setfield(L, -2, "remove");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<DdcBindingModule>(L); const auto entity = CheckEntity(L, 1).Packed(); const char* name = luaL_checkstring(L, 2);
        lua_pushboolean(L, self->m_store->Has(entity, name)); return 1;
    }, this); lua_setfield(L, -2, "has");
}
void DdcBindingModule::Tick(float dt) {
    if (!m_systems || !m_systems->Valid()) return;
    lua_State* L = m_systems->State(); LuaStackGuard stack(L);
    m_systems->Push(); const int systems = lua_gettop(L);
    struct SystemBatch { std::string name; std::vector<LuaReference> functions; };
    std::vector<SystemBatch> batches;
    // Snapshot registrations before running scripts; registering during a tick starts next tick.
    lua_pushnil(L);
    while (lua_next(L, systems)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
            SystemBatch batch{lua_tostring(L, -2), {}};
            const int list = lua_gettop(L); lua_pushnil(L);
            while (lua_next(L, list)) { if (lua_isfunction(L, -1)) batch.functions.emplace_back(L, -1); lua_pop(L, 1); }
            if (!batch.functions.empty()) batches.push_back(std::move(batch));
        }
        lua_pop(L, 1);
    }
    for (const auto& batch : batches) {
        std::vector<ddc::EntityId> entities;
        m_store->ForEach(batch.name, [&](ddc::EntityId entity, ddc::DynamicComponent&) { entities.push_back(entity); });
        for (auto entity : entities) {
            for (const auto& function : batch.functions) {
                if (!m_store->Has(entity, batch.name)) break;
                function.Push(L); lua_pushinteger(L, static_cast<lua_Integer>(entity)); PushRef(L, m_store.get(), entity, batch.name); lua_pushnumber(L, dt);
                if (ProtectedCall(L, 3, 0) != LUA_OK) { MYE_LOG_ERROR("Script", "system '{}' failed: {}", batch.name, lua_tostring(L, -1)); lua_pop(L, 1); }
            }
        }
    }
}
} // namespace mye::script
