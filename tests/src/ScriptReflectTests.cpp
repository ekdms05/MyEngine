// ScriptReflectTests.cpp — 리플렉션 기반 Lua 범용 바인딩 (엔진 확장성)
//
// per-타입 C++ 바인딩 없이, 레지스트리에 등록된 임의 리플렉션 타입을 Lua 에서 생성·조작한다:
//   mye.reflect.new(name) → :get/:set(필드), :call(메서드). 게임/플러그인이 GetType<T>() 만 하면 노출.
#include "TestFramework.h"

#include "mye/refl/TypeBuilder.h"
#include "mye/refl/TypeRegistry.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/bindings/EngineBindings.h"

#include "LuaTest.h"

#include <cstdint>
#include <string>

using namespace mye;
using namespace mye::script;

// ---- Lua 에서 다룰 게임/플러그인 타입(엔진에 없는 확장 타입) ----
namespace scriptrefltest {
struct Widget {
    std::int32_t value = 0;
    bool         flag = false;
    std::string  label;
    std::int32_t AddAndGet(std::int32_t d) { value += d; return value; }   // 반환+파라미터
    void         Toggle() { flag = !flag; }                               // void
    bool         GetFlag() const { return flag; }                         // const 반환
    void         SetLabel(std::string s) { label = std::move(s); }        // string 파라미터
};
struct Owner {
    static inline int destroyed = 0;
    Widget child;
    ~Owner() { ++destroyed; }
};
}
MYE_REFLECT(scriptrefltest::Widget);
MYE_REFLECT(scriptrefltest::Owner);
template <> void mye::refl::Reflect(TypeBuilder<scriptrefltest::Widget>& b) {
    b.Version(1)
     .Field("value", &scriptrefltest::Widget::value)
     .Field("flag",  &scriptrefltest::Widget::flag)
     .Field("label", &scriptrefltest::Widget::label)
     .Method<&scriptrefltest::Widget::AddAndGet>("AddAndGet")
     .Method<&scriptrefltest::Widget::Toggle>("Toggle")
     .Method<&scriptrefltest::Widget::GetFlag>("GetFlag")
     .Method<&scriptrefltest::Widget::SetLabel>("SetLabel");
}
template <> void mye::refl::Reflect(TypeBuilder<scriptrefltest::Owner>& b) {
    b.Field("child", &scriptrefltest::Owner::child);
}

namespace {
void SetupRuntime(ScriptRuntime& rt) {
    StdLibPolicy p;
    rt.Initialize(p, nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<ReflectBindingModule>());
}
}

MYE_TEST(ScriptReflectFieldGetSet) {
    // 타입 등록 보장(플러그인/게임이 하는 일과 동치).
    (void)refl::GetType<scriptrefltest::Widget>();

    ScriptRuntime rt; SetupRuntime(rt);
    lua_State* lua = rt.State();

    // 타입 존재 질의.
    bool has = luatest::Eval<bool>(lua, "return mye.reflect.has_type('scriptrefltest::Widget')");
    MYE_EXPECT(has);
    bool noType = luatest::Eval<bool>(lua, "return mye.reflect.has_type('nope::Missing')");
    MYE_EXPECT(!noType);

    // 원시형 필드 get/set(int·bool·string).
    std::int64_t v = luatest::Eval<std::int64_t>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:set('value', 42)
        w:set('flag', true)
        w:set('label', '검')
        return w:get('value')
    )");
    MYE_EXPECT(v == 42);

    bool flag = luatest::Eval<bool>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:set('flag', true)
        return w:get('flag')
    )");
    MYE_EXPECT(flag);

    std::string label = luatest::Eval<std::string>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:set('label', 'hello')
        return w:get('label')
    )");
    MYE_EXPECT(label == "hello");

    // type_name + 없는 필드 → nil.
    std::string tn = luatest::Eval<std::string>(lua, "return mye.reflect.new('scriptrefltest::Widget'):type_name()");
    MYE_EXPECT(tn == "scriptrefltest::Widget");
    bool nilGet = luatest::Eval<bool>(lua, "return mye.reflect.new('scriptrefltest::Widget'):get('nope') == nil");
    MYE_EXPECT(nilGet);
}

MYE_TEST(ScriptReflectMethodCall) {
    (void)refl::GetType<scriptrefltest::Widget>();
    ScriptRuntime rt; SetupRuntime(rt);
    lua_State* lua = rt.State();

    // 반환+파라미터 메서드.
    std::int64_t r = luatest::Eval<std::int64_t>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:set('value', 10)
        return w:call('AddAndGet', 5)    -- value=15, 반환 15
    )");
    MYE_EXPECT(r == 15);

    // void 메서드 + const 반환 메서드.
    bool toggled = luatest::Eval<bool>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:call('Toggle')                 -- flag=true
        return w:call('GetFlag')
    )");
    MYE_EXPECT(toggled);

    // string 파라미터 메서드 → 필드 반영.
    std::string lbl = luatest::Eval<std::string>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:call('SetLabel', '전설검')
        return w:get('label')
    )");
    MYE_EXPECT(lbl == "전설검");

    // 상태 누적(같은 인스턴스에 연속 호출).
    std::int64_t acc = luatest::Eval<std::int64_t>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        w:call('AddAndGet', 1)
        w:call('AddAndGet', 2)
        return w:call('AddAndGet', 3)    -- 1+2+3=6
    )");
    MYE_EXPECT(acc == 6);
}

MYE_TEST(ScriptReflectSafeOnMisuse) {
    (void)refl::GetType<scriptrefltest::Widget>();
    ScriptRuntime rt; SetupRuntime(rt);
    lua_State* lua = rt.State();

    // 없는 타입 → nil (크래시 아님).
    MYE_EXPECT(luatest::Eval<bool>(lua, "return mye.reflect.new('does::not::Exist') == nil"));

    // 없는 메서드/필드 → 안전(throw/행 없음): call→nil, set→false, has_*→false.
    bool safe = luatest::Eval<bool>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        local a = w:call('NoSuchMethod')          -- nil (경고 로그, 크래시 아님)
        local ok = w:set('nope', 1)               -- false
        return a == nil and ok == false and w:has_method('NoSuchMethod') == false
               and w:has_method('AddAndGet') == true and w:has_field('value') == true
    )");
    MYE_EXPECT(safe);

    // 인자 개수/타입 불일치도 안전(nil).
    bool argSafe = luatest::Eval<bool>(lua, R"(
        local w = mye.reflect.new('scriptrefltest::Widget')
        return w:call('AddAndGet') == nil          -- 인자 부족 → nil
    )");
    MYE_EXPECT(argSafe);

    // 진짜 Lua 런타임 에러는 DoString 이 격리(호스트 크래시 없음).
    auto err = rt.DoString("error('boom from script')", "@reflect_err");
    MYE_EXPECT(!err);
}

MYE_TEST(ScriptReflectBorrowedFieldRetainsOwner) {
    (void)refl::GetType<scriptrefltest::Owner>();
    scriptrefltest::Owner::destroyed = 0;
    ScriptRuntime runtime; SetupRuntime(runtime);
    MYE_EXPECT(runtime.DoString(R"(
        local owner = mye.reflect.new('scriptrefltest::Owner')
        _child = owner:get('child')
        assert(getmetatable(owner) == false and owner.__gc == nil)
        assert(getmetatable(_child) == false and _child.__gc == nil)
        assert(_child:set('label', string.rep('x', 200)))
    )", "owner.lua").HasValue());
    lua_gc(runtime.State(), LUA_GCCOLLECT);
    MYE_EXPECT(scriptrefltest::Owner::destroyed == 0);
    MYE_EXPECT(luatest::Eval<int>(runtime.State(), "return #_child:get('label')") == 200);
    MYE_EXPECT(runtime.DoString("_child = nil", "release.lua").HasValue());
    lua_gc(runtime.State(), LUA_GCCOLLECT);
    MYE_EXPECT(scriptrefltest::Owner::destroyed == 1);
}
