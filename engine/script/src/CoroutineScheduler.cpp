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
#include <deque>
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
    // 대기 이벤트 payload 큐 — 같은 이름 이벤트가 한 tick 사이 여러 번 도착해도 유실 없이
    //   순서대로 코루틴에 전달한다(재개 1회당 앞에서 1개 pop).
    std::deque<sol::object> resumeArgs;
    bool         finished = false;
    // Start 첫 재개에서 즉시 발생한 에러 — 등록 후 다음 Tick 에서 수집(격리 경로 통일).
    bool         startFailed = false;
    std::string  startError;
};

// 재개 결과(yield 서술)를 읽어 태스크 대기 상태를 갱신한다.
//   반환값이 대기 서술 테이블이면 그에 맞춰 wait 를 세팅, 아니면 무대기(None) 로 둔다.
void ApplyYieldResult(Task& t, const sol::protected_function_result& r) {
    const std::string prevEvent = t.eventName;
    t.wait = WaitKind::None;
    t.remaining = 0.0f;
    t.eventName.clear();
    t.eventFired = false;

    // 코루틴이 종료됐으면 제거 대상.
    if (t.co.status() == sol::call_status::ok) {
        t.finished = true;
        t.resumeArgs.clear();
        return;
    }
    // yielded 상태만 대기 해석. 그 외(runtime error 등)는 호출측이 별도 처리.
    if (t.co.status() != sol::call_status::yielded) { t.resumeArgs.clear(); return; }

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
                    t.resumeArgs.clear();
                    return;
                }
                if (k == kWaitEvent) {
                    t.wait = WaitKind::Event;
                    sol::object nm = desc["name"];
                    t.eventName = nm.is<std::string>() ? nm.as<std::string>() : std::string();
                    // 같은 이름 이벤트를 다시 대기하고 남은 payload 가 있으면 즉시 재개 예약
                    //   (한 tick 사이 여러 번 도착한 이벤트를 순서대로 전달). 다른 이벤트로
                    //   전이하면 이전 대기의 payload 는 폐기.
                    if (t.eventName == prevEvent && !t.resumeArgs.empty()) {
                        t.eventFired = true;
                    } else {
                        t.resumeArgs.clear();
                    }
                    return;
                }
            }
        }
    }
    // 서술 없는 순수 yield() → 무대기(다음 tick 즉시 재개).
    t.wait = WaitKind::None;
    t.resumeArgs.clear();
}

} // namespace

struct CoroutineScheduler::Impl {
    std::vector<Task> tasks;
    uint64_t          nextId = 1;
    sol::state*       lua = nullptr;   // RegisterBindings 시 캐시(thread 생성용)
    // Tick 재개 중 새로 시작된 코루틴(예: 코루틴이 mye.co.start 호출)은 즉시 tasks 에
    //   push_back 하면 순회 중인 참조가 무효화된다(UAF). Tick 중에는 여기 대기시켰다가
    //   순회 종료 후 병합한다.
    std::vector<Task> deferredAdds;
    bool              ticking = false;
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

    // Tick 재개 중 새 코루틴이 시작되면 tasks 순회 참조가 무효화되므로 deferredAdds 로 미룬다.
    std::vector<Task>& sink = m_impl->ticking ? m_impl->deferredAdds : m_impl->tasks;

    // 첫 재개(인자 없음) — on_start 시점의 초기 실행.
    sol::protected_function_result r = t.co();
    if (!r.valid()) {
        // 첫 실행에서 곧바로 에러 — 폐기하지 않고 startFailed 로 표시해 등록한다.
        //   다음 Tick 이 이 태스크에서 CoResumeError 를 산출해 호출측(ScriptSystem)이 owner
        //   기준으로 격리·발행하도록 한다(에러 유실 방지).
        sol::error err = r;
        t.startFailed = true;
        t.startError = err.what();
        const uint64_t id = t.id;
        sink.push_back(std::move(t));
        return id;
    }
    ApplyYieldResult(t, r);
    if (t.finished) {
        // 첫 재개에서 즉시 완료 — 등록하지 않는다.
        return t.id;
    }
    const uint64_t id = t.id;
    sink.push_back(std::move(t));
    return id;
}

void CoroutineScheduler::Tick(float dt, std::vector<CoResumeError>& outErrors) {
    m_impl->ticking = true;

    // 인덱스 순회 — 재개 중 deferredAdds 로 미뤄지므로 tasks 는 이 순회 동안 재할당되지 않는다.
    for (std::size_t i = 0; i < m_impl->tasks.size(); ++i) {
        Task& t = m_impl->tasks[i];
        if (t.finished) continue;

        // Start 첫 재개에서 즉시 실패한 태스크 — 여기서 에러를 산출하고 종료.
        if (t.startFailed) {
            CoResumeError ce;
            ce.entity = t.owner;
            ce.message = std::move(t.startError);
            ce.line = 0;
            outErrors.push_back(std::move(ce));
            t.finished = true;
            continue;
        }

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

        // 대기 이벤트 payload 큐에서 하나 소비(있으면). 없으면 인자 없이 재개.
        sol::object arg;
        if (!t.resumeArgs.empty()) {
            arg = std::move(t.resumeArgs.front());
            t.resumeArgs.pop_front();
        }
        sol::protected_function_result r = arg.valid() ? t.co(arg) : t.co();

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

    m_impl->ticking = false;

    // 종료분 제거.
    auto& v = m_impl->tasks;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const Task& t) { return t.finished; }),
            v.end());

    // Tick 중 시작된 코루틴을 이제 병합(참조 무효화 없이).
    if (!m_impl->deferredAdds.empty()) {
        for (Task& a : m_impl->deferredAdds) v.push_back(std::move(a));
        m_impl->deferredAdds.clear();
    }
}

void CoroutineScheduler::NotifyEvent(std::string_view name, const sol::object& payload) {
    for (Task& t : m_impl->tasks) {
        if (t.wait == WaitKind::Event && t.eventName == name) {
            t.eventFired = true;
            t.resumeArgs.push_back(payload);   // payload 큐잉(연속 이벤트 유실 방지)
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

    // start(fn): 전역 코루틴 시작(owner=Null). 엔티티 파괴 시 자동 취소 대상이 아니다.
    co["start"] = [this](sol::function fn) -> uint64_t {
        return Start(ecs::Entity::Null(), std::move(fn));
    };

    // start_for(entityPacked, fn): 엔티티 스코프 코루틴 — 소유 엔티티 파괴 시 자동 취소된다
    //   (ScriptSystem 이 CancelForEntity 호출). 스크립트에서는 self.entity 를 넘긴다.
    co["start_for"] = [this](uint64_t packed, sol::function fn) -> uint64_t {
        return Start(ecs::Entity::FromPacked(packed), std::move(fn));
    };
}

} // namespace mye::script
