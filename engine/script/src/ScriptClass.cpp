// mye/script/ScriptClass.cpp — 클래스 테이블 로드·인스턴스화 헬퍼 (docs/05, M3-C 구현)
#include "mye/script/ScriptClass.h"

#include "ScriptErrorParse.h"

#include <sol/sol.hpp>

namespace mye::script {

Expected<sol::table, ScriptError> LoadClass(ScriptRuntime& runtime,
                                            std::string_view source,
                                            std::string_view chunkName) {
    sol::state& lua = runtime.State();
    std::string named = "@";
    named += chunkName;
    sol::protected_function_result r =
        lua.safe_script(source, sol::script_pass_on_error, named);
    if (!r.valid()) {
        sol::error err = r;
        detail::ParsedError pe = detail::ParseLuaError(err.what(), chunkName);
        return ScriptError{pe.file, pe.line, pe.message};
    }
    sol::object ret = r;
    if (ret.get_type() != sol::type::table) {
        return ScriptError{std::string(chunkName), 0,
                           "script must return a class table (docs/05 규약)"};
    }
    return ret.as<sol::table>();
}

sol::table MakeInstance(ScriptRuntime& runtime, const sol::table& classTable,
                        ecs::Entity entity, const sol::table& props) {
    sol::state& lua = runtime.State();
    sol::table instance = lua.create_table();
    // setmetatable(instance, { __index = classTable })
    sol::table meta = lua.create_table();
    meta["__index"] = classTable;
    instance[sol::metatable_key] = meta;
    // self.entity 주입 — 엔티티 핸들은 패킹 정수로 최소 노출(usertype 바인딩은 후속).
    instance["entity"] = entity.Packed();
    // self.state 는 핫 리로드 생존 영역(스크립트가 on_init 에서 초기화). 여기선 빈 테이블 보장.
    instance["state"] = lua.create_table();
    // 프로퍼티 주입(있으면).
    if (props.valid()) {
        for (auto& kv : props) {
            instance[kv.first] = kv.second;
        }
    }
    return instance;
}

uint32_t ScanCallbacks(const sol::table& classTable) {
    uint32_t bits = 0;
    auto has = [&](std::string_view name) -> bool {
        sol::object o = classTable[name];
        return o.valid() && o.get_type() == sol::type::function;
    };
    if (has(callbacks::kOnInit))         bits |= static_cast<uint32_t>(CallbackBit::OnInit);
    if (has(callbacks::kOnStart))        bits |= static_cast<uint32_t>(CallbackBit::OnStart);
    if (has(callbacks::kOnUpdate))       bits |= static_cast<uint32_t>(CallbackBit::OnUpdate);
    if (has(callbacks::kOnLateUpdate))   bits |= static_cast<uint32_t>(CallbackBit::OnLateUpdate);
    if (has(callbacks::kOnEvent))        bits |= static_cast<uint32_t>(CallbackBit::OnEvent);
    if (has(callbacks::kOnTriggerEnter)) bits |= static_cast<uint32_t>(CallbackBit::OnTriggerEnter);
    if (has(callbacks::kOnTriggerExit))  bits |= static_cast<uint32_t>(CallbackBit::OnTriggerExit);
    if (has(callbacks::kOnHotReload))    bits |= static_cast<uint32_t>(CallbackBit::OnHotReload);
    if (has(callbacks::kOnDestroy))      bits |= static_cast<uint32_t>(CallbackBit::OnDestroy);
    return bits;
}

} // namespace mye::script
