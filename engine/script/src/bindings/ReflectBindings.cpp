// mye/script/src/bindings/ReflectBindings.cpp — 리플렉션 기반 범용 Lua 바인딩 (엔진 확장성)
//
// mye.reflect.new(name) 로 레지스트리 등록 타입 인스턴스를 만들고, ReflectedRef 의 get/set/call 로
// 필드(원시형)·메서드(MethodInfo.Invoke)에 접근한다. per-타입 C++ 바인딩 없이 임의 타입 노출.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/core/Log.h"

#include <sol/sol.hpp>

#include <cstdint>
#include <new>
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
sol::object PrimToLua(sol::state_view lua, const TypeInfo* t, const void* p) {
    const std::string_view n = t->Name();
    if (n == "bool")   return sol::make_object(lua, *static_cast<const bool*>(p));
    if (n == "i8")     return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::int8_t*>(p)));
    if (n == "i16")    return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::int16_t*>(p)));
    if (n == "i32")    return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::int32_t*>(p)));
    if (n == "i64")    return sol::make_object(lua, *static_cast<const std::int64_t*>(p));
    if (n == "u8")     return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::uint8_t*>(p)));
    if (n == "u16")    return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::uint16_t*>(p)));
    if (n == "u32")    return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::uint32_t*>(p)));
    if (n == "u64")    return sol::make_object(lua, static_cast<std::int64_t>(*static_cast<const std::uint64_t*>(p)));
    if (n == "f32")    return sol::make_object(lua, static_cast<double>(*static_cast<const float*>(p)));
    if (n == "f64")    return sol::make_object(lua, *static_cast<const double*>(p));
    if (n == "string") return sol::make_object(lua, *static_cast<const std::string*>(p));
    return sol::make_object(lua, sol::nil);
}

// Lua → 원시형 필드/값. 성공 시 true. 타입 불일치는 예외 대신 false(throw 금지 — 아래 [주의]).
//   [주의] sol 은 SOL_EXCEPTIONS_SAFE_PROPAGATION 로 C++ 예외를 Lua 프레임 통과 전파하는데,
//   C 로 빌드된 Lua 에선 바인딩 throw 가 안전하지 않다. 그래서 여기선 절대 throw 하지 않고
//   is<>() 로 검사 후 변환한다(불일치 = false 반환, 호출부가 nil/no-op 처리).
bool LuaToPrim(const TypeInfo* t, void* p, const sol::stack_object& v) {
    const std::string_view n = t->Name();
    if (n == "bool")   { if (!v.is<bool>())         return false; *static_cast<bool*>(p) = v.as<bool>(); return true; }
    if (n == "string") { if (!v.is<std::string>())  return false; *static_cast<std::string*>(p) = v.as<std::string>(); return true; }
    if (n == "f32")    { if (!v.is<double>())        return false; *static_cast<float*>(p)  = static_cast<float>(v.as<double>()); return true; }
    if (n == "f64")    { if (!v.is<double>())        return false; *static_cast<double*>(p) = v.as<double>(); return true; }
    // 정수 계열(Lua 정수).
    if (!v.is<std::int64_t>()) return false;
    const std::int64_t iv = v.as<std::int64_t>();
    if (n == "i8")     { *static_cast<std::int8_t*>(p)  = static_cast<std::int8_t>(iv);  return true; }
    if (n == "i16")    { *static_cast<std::int16_t*>(p) = static_cast<std::int16_t>(iv); return true; }
    if (n == "i32")    { *static_cast<std::int32_t*>(p) = static_cast<std::int32_t>(iv); return true; }
    if (n == "i64")    { *static_cast<std::int64_t*>(p) = iv; return true; }
    if (n == "u8")     { *static_cast<std::uint8_t*>(p)  = static_cast<std::uint8_t>(iv);  return true; }
    if (n == "u16")    { *static_cast<std::uint16_t*>(p) = static_cast<std::uint16_t>(iv); return true; }
    if (n == "u32")    { *static_cast<std::uint32_t*>(p) = static_cast<std::uint32_t>(iv); return true; }
    if (n == "u64")    { *static_cast<std::uint64_t*>(p) = static_cast<std::uint64_t>(iv); return true; }
    return false;   // 미지원 원시형(struct 등)
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

// Lua 에 노출하는 리플렉션 인스턴스 핸들. owns=true 면 GC 시 파괴/해제(RAII, 이동 전용).
struct LuaReflected {
    const TypeInfo* type = nullptr;
    void*           ptr = nullptr;
    bool            owns = false;

    LuaReflected() = default;
    LuaReflected(const TypeInfo* t, void* p, bool o) : type(t), ptr(p), owns(o) {}
    LuaReflected(const LuaReflected&) = delete;
    LuaReflected& operator=(const LuaReflected&) = delete;
    LuaReflected(LuaReflected&& o) noexcept : type(o.type), ptr(o.ptr), owns(o.owns) {
        o.type = nullptr; o.ptr = nullptr; o.owns = false;
    }
    LuaReflected& operator=(LuaReflected&& o) noexcept {
        if (this != &o) { Free(); type = o.type; ptr = o.ptr; owns = o.owns; o.type=nullptr; o.ptr=nullptr; o.owns=false; }
        return *this;
    }
    ~LuaReflected() { Free(); }
    void Free() {
        if (owns && type && ptr) { type->Destruct(ptr); AlignedFree(ptr, type->Align()); }
        owns = false; ptr = nullptr;
    }
};

// 메서드 호출: Lua 가변인자 → 파라미터 임시 인스턴스(원시형) → Invoke → 반환 marshal.
//   [주의] throw 금지(위 LuaToPrim 주석). 오류 시 로그 후 nil 반환(크래시/행 방지).
//   ok=false 로 실패를 알린다(호출부가 원하면 에러 처리).
sol::object CallMethod(sol::state_view lua, LuaReflected& self, const std::string& name,
                       sol::variadic_args va, bool& ok) {
    ok = false;
    if (!self.type || !self.ptr) return sol::make_object(lua, sol::nil);
    const refl::MethodInfo* m = self.type->FindMethod(name);
    if (!m) { MYE_LOG_WARN("Script", "reflect: 메서드 없음 '{}'", name); return sol::make_object(lua, sol::nil); }
    if (va.size() != m->Arity()) {
        MYE_LOG_WARN("Script", "reflect: '{}' 인자 개수 불일치({} != {})", name, va.size(), m->Arity());
        return sol::make_object(lua, sol::nil);
    }

    // 파라미터 임시값(정확한 타입 홀더). vector 를 미리 크기 확정해 Ptr() 안정성 보장.
    std::vector<PrimHolder> holders(m->Arity());
    std::vector<void*>      args(m->Arity(), nullptr);
    for (std::size_t i = 0; i < m->Arity(); ++i) {
        const TypeInfo* pt = m->ParamTypes()[i];
        if (!IsPrimitive(pt)) { MYE_LOG_WARN("Script", "reflect: '{}' 비원시형 파라미터 미지원", name); return sol::make_object(lua, sol::nil); }
        holders[i].kind = pt->Name();
        void* p = holders[i].Ptr();
        if (!p) return sol::make_object(lua, sol::nil);
        sol::stack_object arg = va[i];
        if (!LuaToPrim(pt, p, arg)) { MYE_LOG_WARN("Script", "reflect: '{}' 파라미터 {} 변환 실패", name, i); return sol::make_object(lua, sol::nil); }
        args[i] = p;
    }

    // 반환 홀더(있으면).
    const TypeInfo* rt = m->ReturnType();
    PrimHolder retHolder;
    void* retPtr = nullptr;
    if (rt) {
        if (!IsPrimitive(rt)) { MYE_LOG_WARN("Script", "reflect: '{}' 비원시형 반환 미지원", name); return sol::make_object(lua, sol::nil); }
        retHolder.kind = rt->Name();
        retPtr = retHolder.Ptr();
        if (!retPtr) return sol::make_object(lua, sol::nil);
    }

    m->Invoke(self.ptr, args.empty() ? nullptr : args.data(), retPtr);
    ok = true;
    if (rt) return PrimToLua(lua, rt, retPtr);
    return sol::make_object(lua, sol::nil);
}

} // namespace

void ReflectBindingModule::Register(sol::state& lua) {
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- mye.Reflected usertype (임의 리플렉션 인스턴스 핸들) ----
    mye.new_usertype<LuaReflected>(
        "Reflected",
        sol::no_constructor,

        "type_name",
        [](const LuaReflected& r) { return std::string(r.type ? r.type->Name() : std::string_view{}); },

        // 필드 get: 원시형은 값, 구조체는 (비소유) 하위 Reflected. 없으면 nil.
        "get",
        [](sol::this_state ts, LuaReflected& r, const std::string& field) -> sol::object {
            sol::state_view lua(ts);
            if (!r.type || !r.ptr) return sol::make_object(lua, sol::nil);
            const refl::FieldInfo* f = r.type->FindField(field);
            if (!f) return sol::make_object(lua, sol::nil);
            void* fp = f->GetPtr(r.ptr);
            if (IsPrimitive(&f->Type())) return PrimToLua(lua, &f->Type(), fp);
            if (f->Type().GetKind() == Kind::Struct)
                return sol::make_object(lua, LuaReflected(&f->Type(), fp, false));   // 비소유 borrow
            return sol::make_object(lua, sol::nil);
        },

        // 필드 set: 원시형만. 오류는 throw 대신 로그+무시(크래시/행 방지).
        "set",
        [](LuaReflected& r, const std::string& field, sol::stack_object v) -> bool {
            if (!r.type || !r.ptr) return false;
            const refl::FieldInfo* f = r.type->FindField(field);
            if (!f || !IsPrimitive(&f->Type())) {
                MYE_LOG_WARN("Script", "reflect: set 대상 필드 없음/비원시형 '{}'", field);
                return false;
            }
            return LuaToPrim(&f->Type(), f->GetPtr(r.ptr), v);
        },

        // 메서드 호출. 반환값(원시형) 또는 실패 시 nil.
        "call",
        [](sol::this_state ts, LuaReflected& r, const std::string& method, sol::variadic_args va) -> sol::object {
            bool ok = false;
            return CallMethod(sol::state_view(ts), r, method, va, ok);
        },

        // 메서드 존재 질의(스크립트가 안전하게 사전 확인).
        "has_method",
        [](const LuaReflected& r, const std::string& method) {
            return r.type && r.type->FindMethod(method) != nullptr;
        },
        "has_field",
        [](const LuaReflected& r, const std::string& field) {
            return r.type && r.type->FindField(field) != nullptr;
        });

    // ---- mye.reflect 테이블 ----
    sol::table reflectTbl = mye["reflect"].get_or_create<sol::table>();

    // 등록된 struct 타입 인스턴스 생성(소유). 미등록/비struct 면 nil.
    reflectTbl.set_function("new", [](sol::this_state ts, const std::string& typeName) -> sol::object {
        sol::state_view lua(ts);
        const TypeInfo* t = refl::TypeRegistry::Get().Find(typeName);
        if (!t || t->GetKind() != Kind::Struct) return sol::make_object(lua, sol::nil);
        void* buf = AlignedAlloc(t->Size(), t->Align());
        t->Construct(buf);
        return sol::make_object(lua, LuaReflected(t, buf, true));
    });

    // 타입 존재 질의.
    reflectTbl.set_function("has_type", [](const std::string& typeName) {
        return refl::TypeRegistry::Get().Find(typeName) != nullptr;
    });
}

} // namespace mye::script
