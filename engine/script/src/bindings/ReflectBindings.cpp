// mye/script/src/bindings/ReflectBindings.cpp — 리플렉션 기반 범용 Lua 바인딩 (엔진 확장성)
//
// mye.reflect.new(name) 로 레지스트리 등록 타입 인스턴스를 만들고, ReflectedRef 의 get/set/call 로
// 필드(원시형)·메서드(MethodInfo.Invoke)에 접근한다. per-타입 C++ 바인딩 없이 임의 타입 노출.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/core/Log.h"

#include "mye/script/LuaApi.h"

#include <cstdint>
#include <new>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace mye::script {

namespace {

using refl::TypeInfo;
using refl::Kind;

// 정렬 할당/해제 쌍(리플렉션 struct 인스턴스 소유 버퍼 — Construct/Destruct 훅과 짝).
void* AlignedAlloc(std::size_t size, std::size_t align) {
    return ::operator new(size, std::align_val_t(align));
}
void AlignedFree(void* p, std::size_t align) {
    ::operator delete(p, std::align_val_t(align));
}

// 원시형 필드/값 → Lua. 지원: bool/i8..i64/u8..u64/f32/f64/string. 그 외 nil.
void PushPrimitive(lua_State* L, const TypeInfo* type, const void* value) {
    const auto name = type->Name();
    if (name == "bool") { lua_pushboolean(L, static_cast<bool>(*static_cast<const bool*>(value))); return; }
    if (name == "i8") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::int8_t*>(value))); return; }
    if (name == "i16") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::int16_t*>(value))); return; }
    if (name == "i32") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::int32_t*>(value))); return; }
    if (name == "i64") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::int64_t*>(value))); return; }
    if (name == "u8") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::uint8_t*>(value))); return; }
    if (name == "u16") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::uint16_t*>(value))); return; }
    if (name == "u32") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::uint32_t*>(value))); return; }
    if (name == "u64") { lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const std::uint64_t*>(value))); return; }
    if (name == "f32") { lua_pushnumber(L, static_cast<lua_Number>(*static_cast<const float*>(value))); return; }
    if (name == "f64") { lua_pushnumber(L, static_cast<lua_Number>(*static_cast<const double*>(value))); return; }
    if (name == "string") { const auto& str = *static_cast<const std::string*>(value); lua_pushlstring(L, str.data(), str.size()); return; }
    lua_pushnil(L);
}

bool ReadPrimitive(lua_State* L, int index, const TypeInfo* type, void* value) {
    const auto name = type->Name();
    if (name == "bool") { if (!lua_isboolean(L, index)) return false; *static_cast<bool*>(value) = lua_toboolean(L, index) != 0; return true; }
    if (name == "string") { if (lua_type(L, index) != LUA_TSTRING) return false; size_t size = 0; const char* str = lua_tolstring(L, index, &size); static_cast<std::string*>(value)->assign(str, size); return true; }
    if (name == "f32") { if (lua_type(L, index) != LUA_TNUMBER) return false; *static_cast<float*>(value) = static_cast<float>(lua_tonumber(L, index)); return true; }
    if (name == "f64") { if (lua_type(L, index) != LUA_TNUMBER) return false; *static_cast<double*>(value) = lua_tonumber(L, index); return true; }
    if (!lua_isinteger(L, index)) return false;
    const lua_Integer number = lua_tointeger(L, index);
    if (name == "i8") { if (number < std::numeric_limits<std::int8_t>::min() || number > std::numeric_limits<std::int8_t>::max()) return false; *static_cast<std::int8_t*>(value) = static_cast<std::int8_t>(number); return true; }
    if (name == "i16") { if (number < std::numeric_limits<std::int16_t>::min() || number > std::numeric_limits<std::int16_t>::max()) return false; *static_cast<std::int16_t*>(value) = static_cast<std::int16_t>(number); return true; }
    if (name == "i32") { if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max()) return false; *static_cast<std::int32_t*>(value) = static_cast<std::int32_t>(number); return true; }
    if (name == "i64") { *static_cast<std::int64_t*>(value) = static_cast<std::int64_t>(number); return true; }
    if (name == "u8") { if (number < std::numeric_limits<std::uint8_t>::min() || number > std::numeric_limits<std::uint8_t>::max()) return false; *static_cast<std::uint8_t*>(value) = static_cast<std::uint8_t>(number); return true; }
    if (name == "u16") { if (number < std::numeric_limits<std::uint16_t>::min() || number > std::numeric_limits<std::uint16_t>::max()) return false; *static_cast<std::uint16_t*>(value) = static_cast<std::uint16_t>(number); return true; }
    if (name == "u32") { if (number < std::numeric_limits<std::uint32_t>::min() || number > std::numeric_limits<std::uint32_t>::max()) return false; *static_cast<std::uint32_t*>(value) = static_cast<std::uint32_t>(number); return true; }
    if (name == "u64") { if (number < 0) return false; *static_cast<std::uint64_t*>(value) = static_cast<std::uint64_t>(number); return true; }
    return false;
}

bool IsPrimitive(const TypeInfo* t) { return t && t->GetKind() == Kind::Primitive; }

// 메서드 인자·반환의 원시형 임시값 — "정확한 C++ 타입"으로 보관해야 Invoke 가 올바른 폭으로 읽는다.
//   (원시형 TypeInfo 에는 Construct/Destruct 훅이 없어 raw 버퍼+대입은 std::string 에서 UB.)
//   각 필드는 정확한 타입이고 str 은 실제 std::string 멤버(적법 생성/소멸). Ptr() 가 활성 멤버 주소.
struct PrimHolder {
    std::string_view kind;   // TypeInfo::Name()
    bool     b = false;
    std::int8_t   i8 = 0;  std::int16_t  i16 = 0;  std::int32_t  i32 = 0;  std::int64_t  i64 = 0;
    std::uint8_t  u8 = 0;  std::uint16_t u16 = 0;  std::uint32_t u32 = 0;  std::uint64_t u64 = 0;
    float    f32 = 0.0f;   double f64 = 0.0;
    std::string str;

    void* Ptr() {
        if (kind == "bool")   return &b;
        if (kind == "i8")     return &i8;
        if (kind == "i16")    return &i16;
        if (kind == "i32")    return &i32;
        if (kind == "i64")    return &i64;
        if (kind == "u8")     return &u8;
        if (kind == "u16")    return &u16;
        if (kind == "u32")    return &u32;
        if (kind == "u64")    return &u64;
        if (kind == "f32")    return &f32;
        if (kind == "f64")    return &f64;
        if (kind == "string") return &str;
        return nullptr;
    }
};

// Lua userdata owns the reflected buffer; borrowed children retain its owner.
struct LuaReflected {
    const TypeInfo* type = nullptr;
    void*           ptr = nullptr;
    bool            owns = false;

    LuaReflected(const TypeInfo* t, void* p, bool o) : type(t), ptr(p), owns(o) {}
    void Free() {
        if (owns && type && ptr) { type->Destruct(ptr); AlignedFree(ptr, type->Align()); }
        owns = false; ptr = nullptr;
    }
};

// 메서드 호출: Lua 가변인자 → 파라미터 임시 인스턴스(원시형) → Invoke → 반환 marshal.

constexpr const char* kReflected = "mye.Reflected";
LuaReflected& Reflected(lua_State* L) { return *static_cast<LuaReflected*>(luaL_checkudata(L, 1, kReflected)); }
void PushReflected(lua_State* L, const TypeInfo* type, void* value, bool owns, int parent = 0) {
    if (parent) parent = lua_absindex(L, parent);
    new (lua_newuserdatauv(L, sizeof(LuaReflected), 1)) LuaReflected(type, value, owns);
    luaL_setmetatable(L, kReflected);
    // Keep a borrowed field's owning userdata alive until this child is collected.
    if (parent) { lua_pushvalue(L, parent); lua_setiuservalue(L, -2, 1); }
}
int Collect(lua_State* L) { Reflected(L).Free(); return 0; }
int TypeName(lua_State* L) {
    const auto* type = Reflected(L).type;
    const auto name = type ? type->Name() : std::string_view{};
    lua_pushlstring(L, name.data(), name.size()); return 1;
}
int Get(lua_State* L) {
    auto& self = Reflected(L); const char* name = luaL_checkstring(L, 2);
    const auto* field = self.type && self.ptr ? self.type->FindField(name) : nullptr;
    if (!field) { lua_pushnil(L); return 1; }
    void* value = field->GetPtr(self.ptr);
    if (IsPrimitive(&field->Type())) PushPrimitive(L, &field->Type(), value);
    else if (field->Type().GetKind() == Kind::Struct) PushReflected(L, &field->Type(), value, false, 1);
    else lua_pushnil(L);
    return 1;
}
int Set(lua_State* L) {
    auto& self = Reflected(L); const char* name = luaL_checkstring(L, 2);
    const auto* field = self.type && self.ptr ? self.type->FindField(name) : nullptr;
    lua_pushboolean(L, field && IsPrimitive(&field->Type()) && ReadPrimitive(L, 3, &field->Type(), field->GetPtr(self.ptr)));
    return 1;
}
int HasField(lua_State* L) { const auto& self = Reflected(L); const char* name = luaL_checkstring(L, 2); lua_pushboolean(L, self.type && self.type->FindField(name)); return 1; }
int HasMethod(lua_State* L) { const auto& self = Reflected(L); const char* name = luaL_checkstring(L, 2); lua_pushboolean(L, self.type && self.type->FindMethod(name)); return 1; }
int Call(lua_State* L) {
    auto& self = Reflected(L); const char* name = luaL_checkstring(L, 2);
    const auto* method = self.type && self.ptr ? self.type->FindMethod(name) : nullptr;
    if (!method || static_cast<size_t>(lua_gettop(L) - 2) != method->Arity()) { lua_pushnil(L); return 1; }
    std::vector<PrimHolder> holders(method->Arity());
    std::vector<void*> args(method->Arity(), nullptr);
    for (size_t i = 0; i < holders.size(); ++i) {
        const auto* type = method->ParamTypes()[i];
        if (!IsPrimitive(type)) { lua_pushnil(L); return 1; }
        holders[i].kind = type->Name(); args[i] = holders[i].Ptr();
        if (!args[i] || !ReadPrimitive(L, static_cast<int>(i) + 3, type, args[i])) { lua_pushnil(L); return 1; }
    }
    PrimHolder result;
    const auto* type = method->ReturnType();
    void* output = nullptr;
    if (type) {
        if (!IsPrimitive(type)) { lua_pushnil(L); return 1; }
        result.kind = type->Name(); output = result.Ptr();
        if (!output) { lua_pushnil(L); return 1; }
    }
    try { method->Invoke(self.ptr, args.empty() ? nullptr : args.data(), output); }
    catch (const std::exception& error) { MYE_LOG_WARN("Script", "reflect.call '{}': {}", name, error.what()); lua_pushnil(L); return 1; }
    catch (...) { MYE_LOG_WARN("Script", "reflect.call '{}': native method failed", name); lua_pushnil(L); return 1; }
    if (type) PushPrimitive(L, type, output); else lua_pushnil(L);
    return 1;
}
int New(lua_State* L) {
    const auto* type = refl::TypeRegistry::Get().Find(luaL_checkstring(L, 1));
    if (!type || type->GetKind() != Kind::Struct) { lua_pushnil(L); return 1; }
    void* value = AlignedAlloc(type->Size(), type->Align());
    try { type->Construct(value); }
    catch (...) { AlignedFree(value, type->Align()); MYE_LOG_WARN("Script", "reflect.new: native constructor failed"); lua_pushnil(L); return 1; }
    PushReflected(L, type, value, true); return 1;
}
int HasType(lua_State* L) { lua_pushboolean(L, refl::TypeRegistry::Get().Find(luaL_checkstring(L, 1)) != nullptr); return 1; }
} // namespace

void ReflectBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    const luaL_Reg methods[] = {{"type_name", TypeName}, {"get", Get}, {"set", Set}, {"call", Call}, {"has_method", HasMethod}, {"has_field", HasField}, {nullptr, nullptr}};
    luaL_newmetatable(L, kReflected);
    lua_pushcfunction(L, Collect); lua_setfield(L, -2, "__gc");
    // Borrowed children retain this owner; scripts must not free it manually.
    lua_pushboolean(L, false); lua_setfield(L, -2, "__metatable");
    lua_newtable(L); luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index"); lua_pop(L, 1);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "reflect");
    lua_pushcfunction(L, New); lua_setfield(L, -2, "new");
    lua_pushcfunction(L, HasType); lua_setfield(L, -2, "has_type");
}
} // namespace mye::script
