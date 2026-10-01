#include "mye/script/CoroutineScheduler.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace mye::script {
namespace {
struct Task {
    uint64_t id = 0;
    ecs::Entity owner;
    LuaReference thread;
    lua_State* stack = nullptr;
    WaitKind wait = WaitKind::None;
    float remaining = 0;
    std::string eventName;
    std::deque<LuaReference> payloads;
    bool finished = false;
    std::string error;
};

void ReadYield(Task& task, int status, int results) {
    const std::string previousEvent = task.eventName;
    task.wait = WaitKind::None;
    task.remaining = 0;
    task.eventName.clear();
    lua_State* lua = task.stack;
    if (status == LUA_OK) {
        task.finished = true;
        task.payloads.clear();
    } else if (results > 0 && lua_istable(lua, -results)) {
        const int descriptor = lua_absindex(lua, -results);
        lua_getfield(lua, descriptor, "__mye_wait");
        const char* kind = lua_tostring(lua, -1);
        if (kind && std::string_view(kind) == "seconds") {
            task.wait = WaitKind::Seconds;
            lua_getfield(lua, descriptor, "seconds");
            task.remaining = static_cast<float>(lua_tonumber(lua, -1));
            lua_pop(lua, 1);
        } else if (kind && std::string_view(kind) == "event") {
            task.wait = WaitKind::Event;
            lua_getfield(lua, descriptor, "name");
            size_t length = 0;
            const char* name = lua_tolstring(lua, -1, &length);
            if (name) task.eventName.assign(name, length);
            lua_pop(lua, 1);
        }
        lua_pop(lua, 1);
    }
    if (task.wait != WaitKind::Event || task.eventName != previousEvent) task.payloads.clear();
    // Yielded values are consumed; a suspended Lua frame keeps its own locals.
    lua_settop(lua, 0);
}

void Resume(Task& task, lua_State* main, int arguments) {
    int results = 0;
    const int status = lua_resume(task.stack, main, arguments, &results);
    if (status == LUA_OK || status == LUA_YIELD) { ReadYield(task, status, results); return; }
    LuaStackGuard guard(main);
    const char* message = lua_tostring(task.stack, -1);
    luaL_traceback(main, task.stack, message ? message : "coroutine failed", 1);
    task.error = lua_tostring(main, -1);
    lua_settop(task.stack, 0);
}
}

struct CoroutineScheduler::Impl {
    lua_State* lua = nullptr;
    uint64_t nextId = 1;
    std::vector<Task> tasks;
    std::vector<Task> deferred;
    bool ticking = false;
};
CoroutineScheduler::CoroutineScheduler() : m_impl(std::make_unique<Impl>()) {}
CoroutineScheduler::~CoroutineScheduler() = default;

uint64_t CoroutineScheduler::Start(ecs::Entity owner, LuaReference function) {
    if (!m_impl->lua || !function.Valid()) return 0;
    lua_State* main = m_impl->lua;
    LuaStackGuard guard(main);
    function.Push(main);
    if (!lua_isfunction(main, -1)) return 0;
    Task task;
    task.id = m_impl->nextId++;
    task.owner = owner;
    task.stack = lua_newthread(main);
    task.thread = LuaReference(main, -1);
    lua_pop(main, 1);
    lua_xmove(main, task.stack, 1);
    Resume(task, main, 0);
    const uint64_t id = task.id;
    if (!task.finished) {
        auto& destination = m_impl->ticking ? m_impl->deferred : m_impl->tasks;
        destination.push_back(std::move(task));
    }
    return id;
}

void CoroutineScheduler::Tick(float dt, std::vector<CoResumeError>& errors) {
    m_impl->ticking = true;
    for (Task& task : m_impl->tasks) {
        if (task.finished) continue;
        if (task.error.empty()) {
            if (task.wait == WaitKind::Seconds) {
                task.remaining -= dt;
                if (task.remaining > 0) continue;
            } else if (task.wait == WaitKind::Event && task.payloads.empty()) continue;
            int arguments = 0;
            if (!task.payloads.empty()) {
                task.payloads.front().Push(task.stack);
                task.payloads.pop_front();
                arguments = 1; // nil is a meaningful event payload.
            }
            Resume(task, m_impl->lua, arguments);
        }
        if (!task.error.empty()) {
            errors.push_back({task.owner, std::move(task.error), 0});
            task.finished = true;
        }
    }
    m_impl->ticking = false;
    std::erase_if(m_impl->tasks, [](const Task& task) { return task.finished; });
    for (Task& task : m_impl->deferred) m_impl->tasks.push_back(std::move(task));
    m_impl->deferred.clear();
}
void CoroutineScheduler::NotifyEvent(std::string_view name, const LuaReference& payload) {
    for (Task& task : m_impl->tasks)
        if (task.wait == WaitKind::Event && task.eventName == name) task.payloads.push_back(payload);
}
void CoroutineScheduler::CancelForEntity(ecs::Entity owner) {
    auto cancel = [owner](auto& tasks) {
        for (Task& task : tasks) if (task.owner == owner) task.finished = true;
    };
    cancel(m_impl->tasks); cancel(m_impl->deferred);
    if (!m_impl->ticking) {
        std::erase_if(m_impl->tasks, [](const Task& task) { return task.finished; });
        std::erase_if(m_impl->deferred, [](const Task& task) { return task.finished; });
    }
}
void CoroutineScheduler::CancelAll() {
    if (m_impl->ticking) for (Task& task : m_impl->tasks) task.finished = true;
    else m_impl->tasks.clear();
    m_impl->deferred.clear();
}
size_t CoroutineScheduler::ActiveCount() const { return m_impl->tasks.size() + m_impl->deferred.size(); }

void CoroutineScheduler::RegisterBindings(lua_State* lua) {
    m_impl->lua = lua;
    LuaStackGuard guard(lua);
    lua_getglobal(lua, "mye");
    EnsureTable(lua, -1, "co");
    PushFunction(lua, [](lua_State* state) -> int {
        luaL_checktype(state, 1, LUA_TFUNCTION);
        lua_pushinteger(state, static_cast<lua_Integer>(Context<CoroutineScheduler>(state)->Start(
            ecs::Entity::Null(), LuaReference(state, 1))));
        return 1;
    }, this);
    lua_setfield(lua, -2, "start");
    PushFunction(lua, [](lua_State* state) -> int {
        const ecs::Entity owner = CheckEntity(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);
        lua_pushinteger(state, static_cast<lua_Integer>(Context<CoroutineScheduler>(state)->Start(
            owner, LuaReference(state, 2))));
        return 1;
    }, this);
    lua_setfield(lua, -2, "start_for");
    // Pure Lua wrappers may yield without crossing a non-yieldable native call.
    constexpr const char* wrappers = R"LUA(
        local co = mye.co
        function co.wait_seconds(t)
            assert(type(t) == "number" and t >= 0 and t < math.huge, "seconds must be finite and nonnegative")
            return coroutine.yield({ __mye_wait = "seconds", seconds = t })
        end
        function co.wait_event(name)
            assert(type(name) == "string", "event name must be a string")
            return coroutine.yield({ __mye_wait = "event", name = name })
        end
        function co.yield() return coroutine.yield() end
    )LUA";
    int status = luaL_loadbufferx(lua, wrappers, std::char_traits<char>::length(wrappers), "@mye.co.bindings", "t");
    if (status == LUA_OK) status = ProtectedCall(lua, 0, 0);
    if (status != LUA_OK) MYE_LOG_ERROR("Script", "coroutine bindings failed: {}", lua_tostring(lua, -1));
}
} // namespace mye::script
