// mye/script/src/bindings/EventBindings.cpp — Lua 커스텀 이벤트 발행·구독 (docs/05, M3-C)
//
// mye.events.on(name, fn)    — 문자열 이름 이벤트에 핸들러 등록. 반환 = 해제용 id.
// mye.events.off(name, id)   — 등록 해제.
// mye.events.emit(name, ...) — 등록된 핸들러를 순서대로 즉시 호출(가변 인자 페이로드).
//
// 계약(IBindingModule): 모듈은 sol 객체를 멤버로 저장하면 안 되므로(VM 재생성마다 Register),
//   디스패치 테이블을 Lua 스테이트 내부(mye 하위 은닉 테이블)에 둔다 — VM 수명과 정확히 일치한다.
//
// 범위: 여기서는 Lua↔Lua 커스텀 이벤트만 담당한다. C++ 이벤트(AnimationEvent·Trigger*)는
//   ScriptSystem 이 각 스크립트의 on_event 로 라우팅한다(01 raw 버스 POD→Lua 자동 변환은 후속).
#include "mye/script/bindings/EngineBindings.h"

#include "mye/core/Log.h"

#include <sol/sol.hpp>

#include <string>

namespace mye::script {

void EventBindingModule::Register(sol::state& lua) {
    sol::table mye = lua["mye"].get_or_create<sol::table>();
    sol::table ev = mye["events"].get_or_create<sol::table>();

    // 은닉 디스패치 저장소: mye._event_handlers[name] = { [id]=fn, ... }, mye._event_next_id.
    //   Lua 스테이트에 두어 VM 재생성 시 함께 초기화된다(sol 객체 멤버 저장 금지 계약 준수).
    sol::table store = mye["_event_handlers"].get_or_create<sol::table>();
    if (!mye["_event_next_id"].valid()) mye["_event_next_id"] = 1;

    // on(name, fn) → id. 잘못된 인자(문자열/함수 아님)는 Lua 에러(sol2 경계).
    ev.set_function("on", [](sol::this_state ts, const std::string& name,
                             sol::protected_function fn) -> int64_t {
        sol::state_view lv(ts);
        sol::table mtbl = lv["mye"];
        sol::table handlers = mtbl["_event_handlers"];
        sol::table forName = handlers[name].get_or_create<sol::table>();
        int64_t id = mtbl["_event_next_id"].get<int64_t>();
        mtbl["_event_next_id"] = id + 1;
        forName[id] = fn;
        return id;
    });

    // off(name, id) — 해당 핸들러 제거.
    ev.set_function("off", [](sol::this_state ts, const std::string& name, int64_t id) {
        sol::state_view lv(ts);
        sol::table handlers = lv["mye"]["_event_handlers"];
        sol::object forNameObj = handlers[name];
        if (forNameObj.get_type() == sol::type::table) {
            sol::table forName = forNameObj.as<sol::table>();
            forName[id] = sol::nil;
        }
    });

    // emit(name, ...) — 등록 핸들러 즉시 디스패치. 핸들러 에러는 로그로 격리하고 다음으로 진행.
    ev.set_function("emit", [](sol::this_state ts, const std::string& name,
                               sol::variadic_args args) {
        sol::state_view lv(ts);
        sol::table handlers = lv["mye"]["_event_handlers"];
        sol::object forNameObj = handlers[name];
        if (forNameObj.get_type() != sol::type::table) return;
        sol::table forName = forNameObj.as<sol::table>();
        // 스냅샷 순회(핸들러가 on/off 호출로 테이블을 변경해도 안전).
        std::vector<sol::protected_function> snapshot;
        for (auto& kv : forName) {
            if (kv.second.get_type() == sol::type::function)
                snapshot.push_back(kv.second.as<sol::protected_function>());
        }
        for (auto& fn : snapshot) {
            sol::protected_function_result r = fn(sol::as_args(args));
            if (!r.valid()) {
                sol::error e = r;
                MYE_LOG_ERROR("Script", "event handler '{}' failed: {}", name, e.what());
            }
        }
    });
}

} // namespace mye::script
