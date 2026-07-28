// ScriptDdcTests.cpp — 데이터드리븐 컴포넌트 Lua 바인딩 (엔진 확장성)
//
// 게임이 엔진 재컴파일 없이: (1) JSON 으로 컴포넌트를 정의(mye.ddc.define),
// (2) Lua 로 인스턴스를 생성·조작(new/get/set). 플러그인·Lua·데이터 삼위일체 검증.
#include "TestFramework.h"

#include "mye/script/ScriptRuntime.h"
#include "mye/script/bindings/EngineBindings.h"
#include "mye/ddc/SchemaRegistry.h"

#include <sol/sol.hpp>

#include <cmath>
#include <string>

using namespace mye;
using namespace mye::script;

namespace {
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
void SetupDdc(ScriptRuntime& rt) {
    StdLibPolicy p;
    rt.Initialize(p, nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<DdcBindingModule>());
}
}

MYE_TEST(ScriptDdcDefineAndUse) {
    ScriptRuntime rt; SetupDdc(rt);
    sol::state& lua = rt.State();

    // 게임이 Lua 에서 JSON 으로 컴포넌트 스키마 정의 → 인스턴스 생성·조작.
    std::int64_t hp = lua.script(R"(
        local ok = mye.ddc.define([[
            { "name":"Stats", "fields":[
                { "name":"hp","type":"i32","default":100 },
                { "name":"crit","type":"f32","default":0.1 },
                { "name":"name","type":"string","default":"용사" },
                { "name":"alive","type":"bool","default":true }
            ] }
        ]])
        assert(ok, "define failed")
        local c = mye.ddc.new("Stats")
        assert(c:type_name() == "Stats")
        assert(c:get("hp") == 100)
        c:set("hp", 55)
        return c:get("hp")
    )");
    MYE_EXPECT(hp == 55);

    // 타입별 get.
    double crit = lua.script(R"(return mye.ddc.new("Stats"):get("crit"))");
    MYE_EXPECT(Near(crit, 0.1));
    std::string nm = lua.script(R"(local c = mye.ddc.new("Stats"); c:set("name","마법사"); return c:get("name"))");
    MYE_EXPECT(nm == "마법사");
    bool alive = lua.script(R"(return mye.ddc.new("Stats"):get("alive"))");
    MYE_EXPECT(alive);

    // has_schema / has.
    bool hasSchema = lua.script(R"(return mye.ddc.has_schema("Stats") and not mye.ddc.has_schema("Nope"))");
    MYE_EXPECT(hasSchema);
    bool hasField = lua.script(R"(return mye.ddc.new("Stats"):has("hp") and not mye.ddc.new("Stats"):has("zzz"))");
    MYE_EXPECT(hasField);
}

MYE_TEST(ScriptDdcSafeOnMisuse) {
    ScriptRuntime rt; SetupDdc(rt);
    sol::state& lua = rt.State();

    // 미등록 스키마 new → nil, 잘못된 define → false (크래시/행 없음).
    bool safe = lua.script(R"(
        local none = mye.ddc.new("Missing")
        local bad = mye.ddc.define("{ not valid json ")
        return none == nil and bad == false
    )");
    MYE_EXPECT(safe);

    // 없는 필드 get/set 안전(nil/false).
    bool fieldSafe = lua.script(R"(
        mye.ddc.define([[{ "name":"C", "fields":[ { "name":"v","type":"i32" } ] }]])
        local c = mye.ddc.new("C")
        local g = c:get("nope")           -- nil
        local s = c:set("nope", 1)        -- false
        local wrong = c:set("v", "text")  -- 타입 불일치 → false
        return g == nil and s == false and wrong == false and c:set("v", 7) == true and c:get("v") == 7
    )");
    MYE_EXPECT(fieldSafe);
}

// 앱이 C++ 에서 미리 로드한 스키마도 Lua 에서 사용 가능.
MYE_TEST(ScriptDdcPreloadedSchema) {
    ScriptRuntime rt;
    StdLibPolicy p;
    rt.Initialize(p, nullptr, nullptr);
    auto mod = std::make_unique<DdcBindingModule>();
    // C++ 에서 스키마 등록.
    ddc::ComponentSchema s{"Loot"};
    s.AddField({"gold", ddc::FieldType::I64, 500});
    (void)mod->Registry().Register(s);
    rt.AddBindingModule(std::move(mod));

    sol::state& lua = rt.State();
    std::int64_t gold = lua.script(R"(return mye.ddc.new("Loot"):get("gold"))");
    MYE_EXPECT(gold == 500);
}
