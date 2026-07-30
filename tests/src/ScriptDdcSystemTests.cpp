// ScriptDdcSystemTests.cpp — Lua 정의 시스템(데이터 컴포넌트 위) (엔진 확장성 캡스톤)
//
// 게임 로직 전체가 데이터+Lua: 컴포넌트를 데이터로 정의, 엔티티에 부착, Lua 함수 시스템 등록 →
// 모듈 Tick 이 매 프레임 해당 컴포넌트 엔티티마다 실행. 엔진 코어 무수정.
#include "TestFramework.h"

#include "mye/script/ScriptRuntime.h"
#include "mye/script/bindings/EngineBindings.h"

#include <sol/sol.hpp>

#include <memory>
#include <string>

using namespace mye;
using namespace mye::script;

namespace {
// 런타임 + DdcBindingModule 구성. 모듈 raw 포인터 반환(Tick 호출용, 소유는 런타임).
DdcBindingModule* SetupDdcSystem(ScriptRuntime& rt) {
    StdLibPolicy p;
    rt.Initialize(p, nullptr, nullptr);
    auto mod = std::make_unique<DdcBindingModule>();
    DdcBindingModule* raw = mod.get();
    rt.AddBindingModule(std::move(mod));
    return raw;
}
}

MYE_TEST(ScriptDdcLuaSystemRegen) {
    ScriptRuntime rt;
    DdcBindingModule* mod = SetupDdcSystem(rt);
    sol::state& lua = rt.State();

    // 게임: 데이터로 컴포넌트 정의 + 엔티티 부착 + Lua 시스템 등록.
    auto setup = rt.DoString(R"(
        mye.ddc.define([[{ "name":"Health", "fields":[
            {"name":"cur","type":"i32","default":1},
            {"name":"max","type":"i32","default":10},
            {"name":"regen","type":"i32","default":3}
        ] }]])
        mye.ddc.attach(1, "Health")
        mye.ddc.attach(2, "Health"):set("cur", 10)   -- 이미 만렙
        mye.system("Health", function(e, c, dt)
            local n = c:get("cur") + c:get("regen")
            local m = c:get("max")
            if n > m then n = m end
            c:set("cur", n)
        end)
    )", "@game_setup");
    MYE_EXPECT(static_cast<bool>(setup));

    // 초기.
    std::int64_t cur1 = lua.script(R"(return mye.ddc.get(1,"Health"):get("cur"))");
    MYE_EXPECT(cur1 == 1);

    // 한 틱: 엔티티1 1→4(+3), 엔티티2 10→10(클램프).
    mod->Tick(0.016f);
    cur1 = lua.script(R"(return mye.ddc.get(1,"Health"):get("cur"))");
    std::int64_t cur2 = lua.script(R"(return mye.ddc.get(2,"Health"):get("cur"))");
    MYE_EXPECT(cur1 == 4);
    MYE_EXPECT(cur2 == 10);

    // 여러 틱 → 엔티티1 만렙 클램프(4→7→10→10).
    mod->Tick(0.016f);   // 7
    mod->Tick(0.016f);   // 10
    mod->Tick(0.016f);   // 10
    cur1 = lua.script(R"(return mye.ddc.get(1,"Health"):get("cur"))");
    MYE_EXPECT(cur1 == 10);
}

MYE_TEST(ScriptDdcSystemAutoDrivenByRuntime) {
    // 앱은 rt.UpdateBindings(dt) 만 호출 — 모듈 Tick 을 직접 안 불러도 Lua 시스템이 구동된다.
    ScriptRuntime rt;
    (void)SetupDdcSystem(rt);   // 모듈 소유는 런타임
    sol::state& lua = rt.State();

    auto setup = rt.DoString(R"(
        mye.ddc.define([[{ "name":"Counter", "fields":[ {"name":"n","type":"i32","default":0} ] }]])
        mye.ddc.attach(1, "Counter")
        mye.system("Counter", function(e, c, dt) c:set("n", c:get("n") + 1) end)
    )", "@counter");
    MYE_EXPECT(static_cast<bool>(setup));

    rt.UpdateBindings(1.0f / 60.0f);
    rt.UpdateBindings(1.0f / 60.0f);
    rt.UpdateBindings(1.0f / 60.0f);
    std::int64_t n = lua.script(R"(return mye.ddc.get(1,"Counter"):get("n"))");
    MYE_EXPECT(n == 3);
}

MYE_TEST(ScriptDdcSystemSafeAndDynamic) {
    ScriptRuntime rt;
    DdcBindingModule* mod = SetupDdcSystem(rt);
    sol::state& lua = rt.State();

    // 컴포넌트 없는 상태에서 Tick → 안전(no-op).
    mod->Tick(0.016f);

    // 시스템이 런타임에 엔티티를 제거해도 반복 무효화 없이 안전(스냅샷).
    auto setup = rt.DoString(R"(
        mye.ddc.define([[{ "name":"Timer", "fields":[ {"name":"t","type":"i32","default":2} ] }]])
        mye.ddc.attach(10, "Timer")
        mye.ddc.attach(11, "Timer")
        mye.system("Timer", function(e, c, dt)
            local t = c:get("t") - 1
            c:set("t", t)
            if t <= 0 then mye.ddc.remove(e, "Timer") end   -- 반복 중 구조 변경
        end)
    )", "@timer");
    MYE_EXPECT(static_cast<bool>(setup));

    mod->Tick(0.016f);   // t: 2→1 (둘 다 유지)
    MYE_EXPECT((bool)lua.script(R"(return mye.ddc.has(10,"Timer") and mye.ddc.has(11,"Timer"))"));
    mod->Tick(0.016f);   // t: 1→0 → 제거
    MYE_EXPECT((bool)lua.script(R"(return mye.ddc.has(10,"Timer") == false and mye.ddc.has(11,"Timer") == false)"));

    // 시스템 함수가 에러를 던져도 Tick 이 격리(크래시/행 없음).
    auto bad = rt.DoString(R"(
        mye.ddc.define([[{ "name":"Bad", "fields":[ {"name":"v","type":"i32"} ] }]])
        mye.ddc.attach(20, "Bad")
        mye.system("Bad", function(e, c, dt) error("boom in system") end)
    )", "@bad");
    MYE_EXPECT(static_cast<bool>(bad));
    mod->Tick(0.016f);   // 에러 로그만, 크래시 없음
    MYE_EXPECT((bool)lua.script(R"(return mye.ddc.has(20,"Bad"))"));   // 여전히 존재(정상 진행)
}
