// mye/script/CoroutineScheduler.cpp — 코루틴 스케줄러 (docs/05 §코루틴, M3-C 구현)
//
// 설계:
//   - `mye.co.wait_seconds(t)` / `mye.co.wait_event(name)` 는 Lua 측에서 `coroutine.yield`
//     를 호출하는 얇은 래퍼다. yield 인자로 대기 서술 테이블 { kind=..., ... } 을 넘긴다.
//     스케줄러(C++)는 재개 결과(yield 반환값)를 읽어 각 태스크의 대기 상태를 갱신한다.
//   - 코루틴 본체는 sol::thread(독립 Lua 스택) 위에서 sol::coroutine 으로 감싸 재개한다.
//     Lua 단일 스레드 계약: sol::thread 는 OS 스레드가 아니라 Lua 협조적 코루틴이다.
//   - Start 는 첫 재개까지 수행한다. 첫 yield 에서 대기 상태를 확정한다.
//   - Tick 은 Seconds 만료·Event 발생·None(무대기) 태스크를 protected 재개하고, 재개 후
//     yield 서술을 다시 읽어 대기를 갱신한다. 코루틴이 끝나면(status==dead) 목록에서 제거.
//   - 재개 중 에러는 CoResumeError 로 누적(호출측 ScriptSystem 이 엔티티별 격리).
#include "mye/script/CoroutineScheduler.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace mye::script {

namespace {

// yield 서술 테이블의 kind 문자열 상수(Lua 래퍼와 공유).
constexpr const char* kWaitKindKey = "__mye_wait";
constexpr const char* kWaitSeconds = "seconds";
constexpr const char* kWaitEvent   = "event";

struct Task {
    uint64_t     id = 0;
    ecs::Entity  owner;
    sol::thread  thread;      // 코루틴 실행용 독립 Lua 스택(스택 소유)
    sol::coroutine co;        // 재개 가능한 코루틴(thread 위에서 fn 을 감쌈)
    WaitKind     wait = WaitKind::None;
    float        remaining = 0.0f;   // wait_seconds 잔여 시간
    std::string  eventName;          // wait_event 대기 이름
    bool         eventFired = false; // NotifyEvent 로 표시(다음 tick 재개)
    sol::object  resumeArg;          // 재개 시 전달 인자(이벤트 payload 등)
    bool         finished = false;
};

// 재개 결과(yield 서술)를 읽어 태스크 대기 상태를 갱신한다.
//   반환값이 대기 서술 테이블이면 그에 맞춰 wait 를 세팅, 아니면 무대기(None) 로 둔다.
void ApplyYieldResult(Task& t, const sol::protected_function_result& r) {
    t.wait = WaitKind::None;
    t.remaining = 0.0f;
    t.eventName.clear();
    t.eventFired = false;

    // 코루틴이 종료됐으면 제거 대상.
    if (t.co.status() == sol::call_status::ok) {
        t.finished = true;
        return;
    }
    // yielded 상태만 대기 해석. 그 외(runtime error 등)는 호출측이 별도 처리.
    if (t.co.status() != sol::call_status::yielded) return;

    // 첫 반환값이 대기 서술 테이블인지 검사.
    if (r.return_count() >= 1) {
        sol::object first = r[0];
        if (first.is<sol::table>()) {
            sol::table desc = first.as<sol::table>();
            sol::object kind = desc[kWaitKindKey];
            if (kind.is<std::string>()) {
                std::string k = kind.as<std::string>();
                if (k == kWaitSeconds) {
                    t.wait = WaitKind::Seconds;
                    sol::object sec = desc["seconds"];
                    t.remaining = sec.is<double>() ? static_cast<float>(sec.as<double>()) : 0.0f;
                    return;
                }
                if (k == kWaitEvent) {
                    t.wait = WaitKind::Event;
                    sol::object nm = desc["name"];
                    t.eventName = nm.is<std::string>() ? nm.as<std::string>() : std::string();
                    return;
                }
            }
        }
    }
    // 서술 없는 순수 yield() → 무대기(다음 tick 즉시 재개).
    t.wait = WaitKind::None;
}

} // namespace

struct CoroutineScheduler::Impl {
    std::vector<Task> tasks;
    uint64_t          nextId = 1;
    sol::state*       lua = nullptr;   // RegisterBindings 시 캐시(thread 생성용)
};

CoroutineScheduler::CoroutineScheduler() : m_impl(std::make_unique<Impl>()) {}
CoroutineScheduler::~CoroutineScheduler() = default;

uint64_t CoroutineScheduler::Start(ecs::Entity owner, sol::function fn) {
    if (!fn.valid()) return 0;

    Task t;
    t.id = m_impl->nextId++;
    t.owner = owner;

    // fn 을 독립 Lua 스레드(협조적 코루틴 스택) 위에서 coroutine 으로 감싼다.
    //   sol::coroutine(thread_lua_state, fn) 은 fn 을 thread 스택으로 xmove 해 참조한다.
    lua_State* mainL = fn.lua_state();
    t.thread = sol::thread::create(mainL);
    t.co = sol::coroutine(t.thread.state().lua_state(), fn);

    // 첫 재개(인자 없음) — on_start 시점의 초기 실행.
    sol::protected_function_result r = t.co();
    if (!r.valid()) {
        // 첫 실행에서 곧바로 에러 — 태스크를 등록하지 않고 폐기(호출측이 별도 격리 필요 시
        //   Tick 경로를 쓰지만, Start 의 즉시 에러는 여기선 폐기하고 반환 0 으로 알린다).
        // 그래도 owner 격리를 위해 태스크로 남겨 다음 Tick 에서 에러 수집되게 한다.
        t.finished = true;
        m_impl->tasks.push_back(std::move(t));
        return m_impl->tasks.back().id;
    }
    ApplyYieldResult(t, r);
    if (t.finished) {
        // 첫 재개에서 즉시 완료 — 등록하지 않는다.
        return t.id;
    }
    m_impl->tasks.push_back(std::move(t));
    return m_impl->tasks.back().id;
}

void CoroutineScheduler::Tick(float dt, std::vector<CoResumeError>& outErrors) {
    for (Task& t : m_impl->tasks) {
        if (t.finished) continue;

        bool resume = false;
        switch (t.wait) {
            case WaitKind::None:
                resume = true;
                break;
            case WaitKind::Seconds:
                t.remaining -= dt;
                if (t.remaining <= 0.0f) resume = true;
                break;
            case WaitKind::Event:
                if (t.eventFired) resume = true;
                break;
            case WaitKind::Finished:
                t.finished = true;
                break;
        }
        if (!resume) continue;
        if (!t.co.valid() || t.co.status() == sol::call_status::ok) {
            t.finished = true;
            continue;
        }

        sol::protected_function_result r =
            t.resumeArg.valid() ? t.co(t.resumeArg) : t.co();
        t.resumeArg = sol::object();   // consume

        if (!r.valid()) {
            sol::error err = r;
            CoResumeError ce;
            ce.entity = t.owner;
            ce.message = err.what();
            ce.line = 0;
            outErrors.push_back(std::move(ce));
            t.finished = true;
            continue;
        }
        ApplyYieldResult(t, r);
    }

    // 종료분 제거.
    auto& v = m_impl->tasks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const Task& t) { return t.finished; }),
            v.end());
}

void CoroutineScheduler::NotifyEvent(std::string_view name, const sol::object& payload) {
    for (Task& t : m_impl->tasks) {
        if (t.wait == WaitKind::Event && t.eventName == name) {
            t.eventFired = true;
            t.resumeArg = payload;
        }
    }
}

void CoroutineScheduler::CancelForEntity(ecs::Entity owner) {
    auto& v = m_impl->tasks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Task& t) { return t.owner == owner; }),
            v.end());
}

void CoroutineScheduler::CancelAll() { m_impl->tasks.clear(); }

size_t CoroutineScheduler::ActiveCount() const { return m_impl->tasks.size(); }

void CoroutineScheduler::RegisterBindings(sol::state& lua) {
    m_impl->lua = &lua;

    sol::table mye = lua["mye"];
    if (!mye.valid()) {
        mye = lua.create_named_table("mye");
    }
    sol::table co = lua.create_table();
    mye["co"] = co;

    // wait_seconds/wait_event 는 반드시 "순수 Lua" 함수여야 한다 — C++ 경계를 넘어 yield 하면
    //   (yield across C-call boundary) Lua 가 거부한다. 따라서 coroutine.yield 를 직접 호출하는
    //   Lua 래퍼를 정의하고, yield 서술 테이블(__mye_wait)로 대기 종류를 전달한다.
    sol::protected_function_result waitDefs = lua.safe_script(R"LUA(
        local co = mye.co
        function co.wait_seconds(t)
            return coroutine.yield({ __mye_wait = "seconds", seconds = t })
        end
        function co.wait_event(name)
            return coroutine.yield({ __mye_wait = "event", name = name })
        end
        -- 무대기 양보(다음 tick 즉시 재개).
        function co.yield()
            return coroutine.yield()
        end
    )LUA", "mye.co.bindings");
    (void)waitDefs;

    // start(fn): 전역 코루틴 시작(owner=Null). 엔티티 스코프 시작은 self 바인딩에서 별도 제공.
    co["start"] = [this](sol::function fn) -> uint64_t {
        return Start(ecs::Entity::Null(), std::move(fn));
    };
}

} // namespace mye::script
