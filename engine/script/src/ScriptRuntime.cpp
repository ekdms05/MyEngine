#include "mye/script/ScriptRuntime.h"

#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/script/CoroutineScheduler.h"
#include "mye/script/IBindingModule.h"

#include "ScriptErrorParse.h"

#include "mye/script/LuaApi.h"

#include <memory>
#include <string>
#include <vector>

namespace mye::script {

struct ScriptRuntime::Impl {
    lua_State*                                    lua = nullptr;
    std::shared_ptr<LuaLifetime>                    lifetime;
    StdLibPolicy                                  policy;
    EventBus*                                     events = nullptr;
    asset::AssetManager*                          assets = nullptr;
    bool                                          initialized = false;
    std::vector<IBindingModule*>                  borrowed;   // 비소유 모듈
    std::vector<std::unique_ptr<IBindingModule>>  owned;      // 소유 모듈
    // Register each module once per VM; Initialize resets these cursors.
    std::size_t                                   registeredBorrowed = 0;
    std::size_t                                   registeredOwned = 0;
    std::unique_ptr<CoroutineScheduler>           coroutines;
    // LUA_GCSTEP advances collection as if this many KB were allocated.
    int                                           gcStepSizeKB = 64;
};

namespace {

// Expose only the selected libraries; source loading and GC belong to the engine.
void HardenGlobals(lua_State* lua, const StdLibPolicy& policy) {
    auto hide = [lua](const char* name) { lua_pushnil(lua); lua_setglobal(lua, name); };
    if (!policy.io) hide("io");
    if (!policy.os) hide("os");
    if (!policy.package) { hide("require"); hide("package"); }
    if (!policy.debug) hide("debug");
    // Source loading always goes through the engine asset boundary.
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "collectgarbage"}) hide(name);
}

int LogMessage(lua_State* lua) {
    std::string line;
    const int count = lua_gettop(lua);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) line += '\t';
        switch (lua_type(lua, i)) {
            case LUA_TSTRING: case LUA_TNUMBER: {
                size_t size = 0;
                const char* text = lua_tolstring(lua, i, &size);
                line.append(text, size); break;
            }
            case LUA_TBOOLEAN: line += lua_toboolean(lua, i) ? "true" : "false"; break;
            case LUA_TNIL: line += "nil"; break;
            default: line += "<" + std::string(luaL_typename(lua, i)) + ">"; break;
        }
    }
    if (lua_toboolean(lua, lua_upvalueindex(1))) MYE_LOG_ERROR("Lua", "{}", line);
    else MYE_LOG_INFO("Lua", "{}", line);
    return 0;
}
void InstallLogRedirect(lua_State* lua) {
    lua_pushboolean(lua, false); lua_pushcclosure(lua, LogMessage, 1); lua_setglobal(lua, "print");
    lua_getglobal(lua, "mye");
    lua_pushboolean(lua, false); lua_pushcclosure(lua, LogMessage, 1); lua_setfield(lua, -2, "log");
    lua_pushboolean(lua, true); lua_pushcclosure(lua, LogMessage, 1); lua_setfield(lua, -2, "log_error");
    lua_pop(lua, 1);
}
void OpenLibrary(lua_State* lua, bool enabled, const char* name, lua_CFunction open) {
    if (!enabled) return;
    luaL_requiref(lua, name, open, 1);
    lua_pop(lua, 1);
}

} // namespace

ScriptRuntime::ScriptRuntime() : m_impl(std::make_unique<Impl>()) {}
ScriptRuntime::~ScriptRuntime() { Shutdown(); }

void ScriptRuntime::Initialize(const StdLibPolicy& policy, EventBus* events,
                               asset::AssetManager* assets) {
    m_impl->policy = policy;
    m_impl->events = events;
    m_impl->assets = assets;

    if (m_impl->coroutines) m_impl->coroutines->CancelAll();
    if (m_impl->lua) {
        m_impl->lifetime->state = nullptr;
        lua_close(m_impl->lua);
    }
    m_impl->coroutines.reset();
    m_impl->lua = luaL_newstate();
    if (!m_impl->lua) throw std::bad_alloc();
    m_impl->lifetime = std::make_shared<LuaLifetime>();
    m_impl->lifetime->state = m_impl->lua;
    InstallLuaLifetime(m_impl->lua, m_impl->lifetime);
    OpenLibrary(m_impl->lua, policy.base, LUA_GNAME, luaopen_base);
    OpenLibrary(m_impl->lua, policy.table, LUA_TABLIBNAME, luaopen_table);
    OpenLibrary(m_impl->lua, policy.string, LUA_STRLIBNAME, luaopen_string);
    OpenLibrary(m_impl->lua, policy.math, LUA_MATHLIBNAME, luaopen_math);
    OpenLibrary(m_impl->lua, policy.coroutine, LUA_COLIBNAME, luaopen_coroutine);
    OpenLibrary(m_impl->lua, policy.utf8, LUA_UTF8LIBNAME, luaopen_utf8);
    OpenLibrary(m_impl->lua, policy.io, LUA_IOLIBNAME, luaopen_io);
    OpenLibrary(m_impl->lua, policy.os, LUA_OSLIBNAME, luaopen_os);
    OpenLibrary(m_impl->lua, policy.package, LUA_LOADLIBNAME, luaopen_package);
    OpenLibrary(m_impl->lua, policy.debug, LUA_DBLIBNAME, luaopen_debug);
    lua_newtable(m_impl->lua);
    lua_setglobal(m_impl->lua, "mye");

    // 위험 표면 차단 + print/log 리다이렉트.
    HardenGlobals(m_impl->lua, policy);
    InstallLogRedirect(m_impl->lua);

    // 코루틴 스케줄러 배선 + `mye.co` 바인딩.
    m_impl->coroutines = std::make_unique<CoroutineScheduler>();
    m_impl->coroutines->RegisterBindings(m_impl->lua);

    // GC 는 인크리멘털 모드로 두고 엔진이 프레임당 step 예산으로 제어(스파이크 방지).
    lua_gc(m_impl->lua, LUA_GCINC, 200, 100, 13);

    m_impl->initialized = true;
    // 새 VM — 이전 등록 기록 무효화 후 누적된 모듈 전부 등록.
    m_impl->registeredBorrowed = 0;
    m_impl->registeredOwned = 0;
    ReapplyBindings();
}

void ScriptRuntime::Shutdown() {
    if (!m_impl) return;
    if (m_impl->coroutines) m_impl->coroutines->CancelAll();
    m_impl->initialized = false;
    // Native closure contexts stay alive while Lua runs finalizers in lua_close.
    if (m_impl->lua) {
        m_impl->lifetime->state = nullptr;
        lua_close(m_impl->lua);
        m_impl->lua = nullptr;
    }
    m_impl->coroutines.reset();
    m_impl->owned.clear();
    m_impl->borrowed.clear();
    m_impl->lifetime.reset();
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

void ScriptRuntime::UpdateBindings(float dt) {
    if (!m_impl->initialized) return;
    for (IBindingModule* m : m_impl->borrowed) m->OnUpdate(dt);
    for (auto& m : m_impl->owned)              m->OnUpdate(dt);
}

Expected<void, ScriptError> ScriptRuntime::DoString(std::string_view source,
                                                    std::string_view chunkName) {
    if (!m_impl->initialized) return ScriptError{std::string(chunkName), 0, "Lua runtime is not initialized"};
    lua_State* lua = m_impl->lua;
    LuaStackGuard stack(lua);
    const std::string named = "@" + std::string(chunkName);
    int status = luaL_loadbufferx(lua, source.data(), source.size(), named.c_str(), "t");
    if (status == LUA_OK) status = ProtectedCall(lua, 0, 0);
    if (status != LUA_OK) {
        const char* message = lua_tostring(lua, -1);
        const auto error = detail::ParseLuaError(message ? message : "Lua execution failed", chunkName);
        return ScriptError{error.file, error.line, error.message};
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
    // Advance incremental GC once per simulation update.
    if (m_impl->lua) lua_gc(m_impl->lua, LUA_GCSTEP, m_impl->gcStepSizeKB);
}

lua_State* ScriptRuntime::State() { return m_impl->lua; }
bool ScriptRuntime::IsInitialized() const { return m_impl->initialized; }
EventBus* ScriptRuntime::Events() const { return m_impl->events; }
asset::AssetManager* ScriptRuntime::Assets() const { return m_impl->assets; }

} // namespace mye::script
