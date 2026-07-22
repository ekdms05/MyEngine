// mye/script/ScriptRuntime.cpp — 게임 Lua 스테이트 소유·안전 실행 (docs/05, M3-C 구현)
//
// 책임:
//   - 단일 sol::state 소유·초기화(StdLibPolicy 비트별 선별 open_libraries).
//   - print → 엔진 로그 리다이렉트, io/os/package 위험 표면 차단(docs/05 은닉).
//   - `mye` 전역 테이블 + `mye.log` 진단 함수 + 코루틴 스케줄러 배선(mye.co.*).
//   - DoString: protected 실행 + 에러 file/line 파싱(ScriptError).
//   - UpdateCoroutines: 코루틴 tick 후 재개 에러를 ScriptErrorEvent 로 발행(엔티티별 격리).
//   - CollectGarbageStep: 프레임당 인크리멘털 GC step 예산.
#include "mye/script/ScriptRuntime.h"

#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/script/CoroutineScheduler.h"
#include "mye/script/IBindingModule.h"

#include "ScriptErrorParse.h"

#include <sol/sol.hpp>

#include <memory>
#include <string>
#include <vector>

namespace mye::script {

struct ScriptRuntime::Impl {
    sol::state                                    lua;
    StdLibPolicy                                  policy;
    EventBus*                                     events = nullptr;
    asset::AssetManager*                          assets = nullptr;
    bool                                          initialized = false;
    std::vector<IBindingModule*>                  borrowed;   // 비소유 모듈
    std::vector<std::unique_ptr<IBindingModule>>  owned;      // 소유 모듈
    // 현재 sol::state에 이미 Register된 모듈 수 — ReapplyBindings 멱등성 보장
    // (같은 usertype 이중 new_usertype 방지). VM 재생성(Initialize) 시 0으로 리셋.
    std::size_t                                   registeredBorrowed = 0;
    std::size_t                                   registeredOwned = 0;
    std::unique_ptr<CoroutineScheduler>           coroutines;
    // step_gc(n): 이번 프레임에 "n KB 할당된 것처럼" 인크리멘털 GC 를 전진(Lua 5.4 LUA_GCSTEP).
    //   4KB 는 게임 VM 부하 대비 너무 작아 GC 가 할당을 따라가지 못한다(메모리 성장). 프레임당
    //   의미 있는 진행을 하도록 64KB 로 조정(스파이크 없이 꾸준히 수거). 이름은 실제 의미(스텝
    //   데이터 크기)를 반영.
    int                                           gcStepSizeKB = 64;
};

namespace {

// 위험/미노출 표준 라이브러리 전역을 스테이트에서 제거해 스크립트에서 접근 불가로 만든다.
//   (open_libraries 를 안 부르면 애초에 등록되지 않지만, base 를 통해 새는 os/io/require 관례를
//    방어적으로 nil 처리한다.)
void HardenGlobals(sol::state& lua, const StdLibPolicy& policy) {
    if (!policy.io)      lua["io"] = sol::lua_nil;
    if (!policy.os)      lua["os"] = sol::lua_nil;
    if (!policy.package) {
        lua["require"] = sol::lua_nil;
        lua["package"] = sol::lua_nil;
        lua["dofile"]  = sol::lua_nil;
        lua["loadfile"]= sol::lua_nil;
    }
    if (!policy.debug)   lua["debug"] = sol::lua_nil;
    // 파일 로드/실행 관례 함수는 항상 차단(스크립트는 에셋 파이프라인으로만 로드).
    lua["load"]       = sol::lua_nil;
    lua["loadstring"] = sol::lua_nil;
    lua["collectgarbage"] = sol::lua_nil;   // GC 는 엔진이 예산 제어(CollectGarbageStep).
}

// print → 엔진 로그. mye.log(...) 진단 함수도 함께 심는다.
void InstallLogRedirect(sol::state& lua) {
    auto toLine = [](sol::variadic_args va) -> std::string {
        std::string line;
        bool first = true;
        for (auto v : va) {
            if (!first) line += '\t';
            first = false;
            sol::object o = v;
            // tostring 관례로 문자열화(숫자·불린·nil 포함).
            switch (o.get_type()) {
                case sol::type::string:  line += o.as<std::string>(); break;
                case sol::type::number:  line += std::to_string(o.as<double>()); break;
                case sol::type::boolean: line += (o.as<bool>() ? "true" : "false"); break;
                case sol::type::nil:     line += "nil"; break;
                default:                 line += "<" + std::string(sol::type_name(o.lua_state(), o.get_type())) + ">"; break;
            }
        }
        return line;
    };

    lua.set_function("print", [toLine](sol::variadic_args va) {
        MYE_LOG_INFO("Lua", "{}", toLine(va));
    });

    sol::table mye = lua["mye"];
    if (mye.valid()) {
        mye.set_function("log", [toLine](sol::variadic_args va) {
            MYE_LOG_INFO("Lua", "{}", toLine(va));
        });
        mye.set_function("log_error", [toLine](sol::variadic_args va) {
            MYE_LOG_ERROR("Lua", "{}", toLine(va));
        });
    }
}

} // namespace

ScriptRuntime::ScriptRuntime() : m_impl(std::make_unique<Impl>()) {}
ScriptRuntime::~ScriptRuntime() { Shutdown(); }

void ScriptRuntime::Initialize(const StdLibPolicy& policy, EventBus* events,
                               asset::AssetManager* assets) {
    m_impl->policy = policy;
    m_impl->events = events;
    m_impl->assets = assets;

    // 표준 lib 선별 오픈(정책 기반). io/os/package 는 기본 비노출(docs/05 은닉).
    if (policy.base)      m_impl->lua.open_libraries(sol::lib::base);
    if (policy.table)     m_impl->lua.open_libraries(sol::lib::table);
    if (policy.string)    m_impl->lua.open_libraries(sol::lib::string);
    if (policy.math)      m_impl->lua.open_libraries(sol::lib::math);
    if (policy.coroutine) m_impl->lua.open_libraries(sol::lib::coroutine);
    if (policy.utf8)      m_impl->lua.open_libraries(sol::lib::utf8);
    if (policy.io)        m_impl->lua.open_libraries(sol::lib::io);
    if (policy.os)        m_impl->lua.open_libraries(sol::lib::os);
    if (policy.package)   m_impl->lua.open_libraries(sol::lib::package);
    if (policy.debug)     m_impl->lua.open_libraries(sol::lib::debug);

    // `mye` 전역 테이블 생성(바인딩 모듈의 루트).
    m_impl->lua["mye"] = m_impl->lua.create_table();

    // 위험 표면 차단 + print/log 리다이렉트.
    HardenGlobals(m_impl->lua, policy);
    InstallLogRedirect(m_impl->lua);

    // 코루틴 스케줄러 배선 + `mye.co` 바인딩.
    m_impl->coroutines = std::make_unique<CoroutineScheduler>();
    m_impl->coroutines->RegisterBindings(m_impl->lua);

    // GC 는 인크리멘털 모드로 두고 엔진이 프레임당 step 예산으로 제어(스파이크 방지).
    m_impl->lua.change_gc_mode_incremental(200, 100, 13);

    m_impl->initialized = true;
    // 새 VM — 이전 등록 기록 무효화 후 누적된 모듈 전부 등록.
    m_impl->registeredBorrowed = 0;
    m_impl->registeredOwned = 0;
    ReapplyBindings();
}

void ScriptRuntime::Shutdown() {
    if (!m_impl) return;
    if (m_impl->coroutines) m_impl->coroutines->CancelAll();
    m_impl->coroutines.reset();
    m_impl->owned.clear();
    m_impl->borrowed.clear();
    m_impl->initialized = false;
}

void ScriptRuntime::AddBindingModule(IBindingModule* module) {
    if (!module) return;
    m_impl->borrowed.push_back(module);
    if (m_impl->initialized) {
        module->Register(m_impl->lua);
        m_impl->registeredBorrowed = m_impl->borrowed.size();
    }
}

void ScriptRuntime::AddBindingModule(std::unique_ptr<IBindingModule> module) {
    if (!module) return;
    IBindingModule* raw = module.get();
    m_impl->owned.push_back(std::move(module));
    if (m_impl->initialized) {
        raw->Register(m_impl->lua);
        m_impl->registeredOwned = m_impl->owned.size();
    }
}

// 아직 등록되지 않은 모듈만 Register — 멱등. (Initialize가 VM 생성 후 1회 호출하고,
// 초기화 후 AddBindingModule은 즉시 등록하므로, 명시적 재호출은 무해한 no-op가 된다.)
void ScriptRuntime::ReapplyBindings() {
    if (!m_impl->initialized) return;
    for (std::size_t i = m_impl->registeredBorrowed; i < m_impl->borrowed.size(); ++i)
        m_impl->borrowed[i]->Register(m_impl->lua);
    m_impl->registeredBorrowed = m_impl->borrowed.size();
    for (std::size_t i = m_impl->registeredOwned; i < m_impl->owned.size(); ++i)
        m_impl->owned[i]->Register(m_impl->lua);
    m_impl->registeredOwned = m_impl->owned.size();
}

Expected<void, ScriptError> ScriptRuntime::DoString(std::string_view source,
                                                    std::string_view chunkName) {
    std::string named = "@";
    named += chunkName;
    sol::protected_function_result r =
        m_impl->lua.safe_script(source, sol::script_pass_on_error, named);
    if (!r.valid()) {
        sol::error err = r;
        detail::ParsedError pe = detail::ParseLuaError(err.what(), chunkName);
        return ScriptError{pe.file, pe.line, pe.message};
    }
    return {};
}

CoroutineScheduler& ScriptRuntime::Coroutines() { return *m_impl->coroutines; }

void ScriptRuntime::UpdateCoroutines(float dt) {
    if (!m_impl->coroutines) return;
    std::vector<CoResumeError> errors;
    m_impl->coroutines->Tick(dt, errors);

    // 재개 에러 → ScriptErrorEvent(엔티티별 격리). 코루틴은 이미 종료 처리됨(Tick).
    if (!errors.empty() && m_impl->events) {
        for (const CoResumeError& e : errors) {
            detail::ParsedError pe = detail::ParseLuaError(e.message, "coroutine");
            ScriptErrorEvent ev;
            ev.entity = e.entity;
            ev.file = pe.file;
            ev.line = pe.line;
            ev.message = pe.message;
            ev.callback = "coroutine";
            m_impl->events->Publish(ev);   // 즉시 디스패치(std::string 포함 → Enqueue 불가).
            MYE_LOG_ERROR("Script", "coroutine resume failed: {}", e.message);
        }
    } else {
        for (const CoResumeError& e : errors) {
            MYE_LOG_ERROR("Script", "coroutine resume failed: {}", e.message);
        }
    }
}

void ScriptRuntime::CollectGarbageStep() {
    // 프레임당 인크리멘털 GC step(스파이크 방지, docs/05 성능 가이드).
    m_impl->lua.step_gc(m_impl->gcStepSizeKB);
}

sol::state& ScriptRuntime::State() { return m_impl->lua; }
bool ScriptRuntime::IsInitialized() const { return m_impl->initialized; }
EventBus* ScriptRuntime::Events() const { return m_impl->events; }
asset::AssetManager* ScriptRuntime::Assets() const { return m_impl->assets; }

} // namespace mye::script
