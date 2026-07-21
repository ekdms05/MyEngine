// mye/core/Jobs.cpp — 잡 시스템 구현 (docs/01 §잡 시스템, Phase 1)
//
// 구조: 메인 스레드 + 컴퓨트 워커 (hardware_concurrency-1)개 + IO 전용 스레드 1~2개.
//   MVP는 단일 전역 MPMC 큐(Compute/IO 분리) — work-stealing 데크는 Phase 2(인터페이스 불변).
//   파이버 없음: Wait는 busy-help(대기 중 다른 컴퓨트 잡을 훔쳐 실행).
//
// 카운터 조인 모델: 각 잡/배치는 JobCounter(remaining)를 갖고, 배치의 모든 조각이 끝나면
//   0에 도달한다. 후속 잡(ScheduleAfter)은 카운터가 0이 되는 순간 논블로킹으로 대기 큐에서
//   실행 큐로 이동한다(continuation). 이로써 워커 스레드에서 ScheduleAfter가 스택을 쌓거나
//   교착되지 않는다.
//
// 계약(docs/01):
//   - IO 잡은 컴퓨트 워커에서 실행되지 않는다(전용 IO 스레드만 소비).
//   - busy-help Wait는 IO 큐 잡을 훔치지 않는다(워커가 통째로 IO에 묶이는 것 방지).
//   - RunOnMainThread는 메인 스레드(PumpMainThreadTasks 호출 스레드)에서만 실행된다.
//   - Lua/스크립트는 워커에서 실행 금지 — 잡은 순수 C++ 데이터 병렬만(호출자 규약).
#include "mye/core/Jobs.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mye {

namespace {

struct JobItem {
    std::function<void()> fn;
    uint64_t              counterId = 0; // 완료 시 감소시킬 카운터 (0 = 없음)
};

// 카운터는 배치의 미완료 조각 수(remaining)를 추적한다. remaining==0 이면 완료.
// waiters: 이 카운터가 0이 될 때 컴퓨트/IO 큐로 풀어줄 continuation 잡들.
struct JobCounter {
    std::atomic<int32_t> remaining{0};
    std::vector<std::pair<JobItem, QueueKind>> waiters; // counterMutex 보호
    bool                 done = false;                  // counterMutex 보호(회수 판단)
};

} // namespace

struct JobSystem::Impl {
    // ---- 큐 (MVP: 단일 전역 MPMC, Compute/IO 분리) ----
    std::mutex              computeMutex;
    std::condition_variable computeCv;
    std::deque<JobItem>     computeQueue;

    std::mutex              ioMutex;
    std::condition_variable ioCv;
    std::deque<JobItem>     ioQueue;

    // ---- 카운터 테이블 ----
    std::mutex                                                counterMutex;
    std::unordered_map<uint64_t, std::unique_ptr<JobCounter>> counters;
    std::atomic<uint64_t>                                     nextCounterId{1};

    // ---- 메인 스레드 작업 큐 ----
    std::mutex                         mainMutex;
    std::vector<std::function<void()>> mainTasks;
    std::atomic<std::thread::id>       mainThreadId{}; // PumpMainThreadTasks가 처음 갱신

    // ---- 워커 스레드 ----
    std::vector<std::thread> computeWorkers;
    std::vector<std::thread> ioThreads;
    std::atomic<bool>        running{true};
    uint32_t                 workerCount = 0;

    // 진행 중(idle 카운트) — Wait 스핀 시 CPU 양보용 힌트에는 쓰지 않고, 순수 조인만 사용.

    uint64_t MakeCounter(int32_t initial) {
        const uint64_t id = nextCounterId.fetch_add(1);
        auto counter = std::make_unique<JobCounter>();
        counter->remaining.store(initial, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(counterMutex);
        counters.emplace(id, std::move(counter));
        return id;
    }

    // remaining을 1 감소. 0에 도달하면 done 마킹 + waiters 방출.
    // 방출된 continuation을 잠금 밖에서 큐잉하기 위해 리스트로 수집해 반환한다.
    void SignalCounter(uint64_t id) {
        std::vector<std::pair<JobItem, QueueKind>> released;
        {
            std::lock_guard<std::mutex> lk(counterMutex);
            auto it = counters.find(id);
            if (it == counters.end()) return;
            JobCounter* c = it->second.get();
            const int32_t left = c->remaining.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (left > 0) return;
            c->done = true;
            released.swap(c->waiters);
        }
        // 잠금 밖에서 continuation을 각 큐로 푸시.
        for (auto& [item, queue] : released) {
            if (queue == QueueKind::IO) PushIo(std::move(item));
            else                        PushCompute(std::move(item));
        }
    }

    bool CounterDone(uint64_t id) {
        std::lock_guard<std::mutex> lk(counterMutex);
        auto it = counters.find(id);
        return it == counters.end() || it->second->done;
    }

    // dependency 완료 시 실행할 continuation 등록. 이미 완료됐으면 즉시 큐잉하도록 true 반환.
    // (호출자가 잠금 밖에서 즉시 푸시)
    bool RegisterWaiter(uint64_t dependencyId, JobItem item, QueueKind queue) {
        std::lock_guard<std::mutex> lk(counterMutex);
        auto it = counters.find(dependencyId);
        if (it == counters.end() || it->second->done) {
            return true; // 이미 완료 → 즉시 실행
        }
        it->second->waiters.emplace_back(std::move(item), queue);
        return false;
    }

    void PushCompute(JobItem item) {
        {
            std::lock_guard<std::mutex> lk(computeMutex);
            computeQueue.push_back(std::move(item));
        }
        computeCv.notify_one();
    }

    void PushIo(JobItem item) {
        {
            std::lock_guard<std::mutex> lk(ioMutex);
            ioQueue.push_back(std::move(item));
        }
        ioCv.notify_one();
    }

    // 컴퓨트 큐에서 하나 꺼내 실행. 성공 시 true(busy-help Wait가 사용).
    // IO 큐는 절대 건드리지 않는다(계약).
    bool TryRunOneCompute() {
        JobItem item;
        {
            std::lock_guard<std::mutex> lk(computeMutex);
            if (computeQueue.empty()) return false;
            item = std::move(computeQueue.front());
            computeQueue.pop_front();
        }
        RunItem(item);
        return true;
    }

    void RunItem(JobItem& item) {
        if (item.fn) item.fn();
        if (item.counterId) SignalCounter(item.counterId);
    }

    void ComputeWorkerLoop() {
        for (;;) {
            JobItem item;
            {
                std::unique_lock<std::mutex> lk(computeMutex);
                computeCv.wait(lk, [&] { return !computeQueue.empty() || !running.load(); });
                if (computeQueue.empty()) {
                    if (!running.load()) return;
                    continue;
                }
                item = std::move(computeQueue.front());
                computeQueue.pop_front();
            }
            RunItem(item);
        }
    }

    void IoThreadLoop() {
        for (;;) {
            JobItem item;
            {
                std::unique_lock<std::mutex> lk(ioMutex);
                ioCv.wait(lk, [&] { return !ioQueue.empty() || !running.load(); });
                if (ioQueue.empty()) {
                    if (!running.load()) return;
                    continue;
                }
                item = std::move(ioQueue.front());
                ioQueue.pop_front();
            }
            RunItem(item);
        }
    }
};

JobSystem::JobSystem(uint32_t workerCount, uint32_t ioThreadCount)
    : m_impl(std::make_unique<Impl>()) {
    uint32_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 2;
    uint32_t workers = workerCount != 0 ? workerCount : (hw > 1 ? hw - 1 : 1);
    if (workers == 0) workers = 1;
    if (ioThreadCount == 0) ioThreadCount = 1;
    m_impl->workerCount = workers;
    // 생성 스레드를 잠정 메인 스레드로 기록(PumpMainThreadTasks 첫 호출이 최종 확정).
    m_impl->mainThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);

    for (uint32_t i = 0; i < workers; ++i)
        m_impl->computeWorkers.emplace_back([this] { m_impl->ComputeWorkerLoop(); });
    for (uint32_t i = 0; i < ioThreadCount; ++i)
        m_impl->ioThreads.emplace_back([this] { m_impl->IoThreadLoop(); });
}

JobSystem::~JobSystem() {
    m_impl->running.store(false);
    m_impl->computeCv.notify_all();
    m_impl->ioCv.notify_all();
    for (auto& t : m_impl->computeWorkers) if (t.joinable()) t.join();
    for (auto& t : m_impl->ioThreads) if (t.joinable()) t.join();
    // 남은 메인 작업은 실행하지 않고 폐기(셧다운 정리) — 계약: 셧다운 무릭(스레드 조인).
}

JobHandle JobSystem::Schedule(void (*fn)(void*), void* data, const char* /*debugName*/,
                              QueueKind queue) {
    const uint64_t counterId = m_impl->MakeCounter(1);
    JobItem item;
    item.fn = [fn, data] { if (fn) fn(data); };
    item.counterId = counterId;
    if (queue == QueueKind::IO) m_impl->PushIo(std::move(item));
    else m_impl->PushCompute(std::move(item));
    return JobHandle{counterId};
}

JobHandle JobSystem::ScheduleAfter(JobHandle dependency, void (*fn)(void*), void* data,
                                   const char* /*debugName*/, QueueKind queue) {
    const uint64_t counterId = m_impl->MakeCounter(1);
    JobItem item;
    item.fn = [fn, data] { if (fn) fn(data); };
    item.counterId = counterId;

    if (!dependency.IsValid()) {
        // 의존성 없음 → 즉시 스케줄.
        if (queue == QueueKind::IO) m_impl->PushIo(std::move(item));
        else m_impl->PushCompute(std::move(item));
        return JobHandle{counterId};
    }

    // 논블로킹 continuation: 의존성 카운터가 0이 되면 큐로 방출된다(워커에서 안전).
    JobItem pending = item; // 복사(즉시 실행 경로용)
    if (m_impl->RegisterWaiter(dependency.opaque, std::move(item), queue)) {
        // 이미 완료 → 즉시 큐잉.
        if (queue == QueueKind::IO) m_impl->PushIo(std::move(pending));
        else m_impl->PushCompute(std::move(pending));
    }
    return JobHandle{counterId};
}

JobHandle JobSystem::Schedule(std::function<void()> fn, QueueKind queue) {
    const uint64_t counterId = m_impl->MakeCounter(1);
    JobItem item;
    item.fn = std::move(fn);
    item.counterId = counterId;
    if (queue == QueueKind::IO) m_impl->PushIo(std::move(item));
    else m_impl->PushCompute(std::move(item));
    return JobHandle{counterId};
}

JobHandle JobSystem::ParallelFor(size_t count, size_t batchSize,
                                 std::function<void(size_t, size_t)> body) {
    if (count == 0) return JobHandle{0};
    if (batchSize == 0) batchSize = 1;
    const size_t batches = (count + batchSize - 1) / batchSize;
    const uint64_t counterId = m_impl->MakeCounter(static_cast<int32_t>(batches));
    auto shared = std::make_shared<std::function<void(size_t, size_t)>>(std::move(body));
    for (size_t b = 0; b < batches; ++b) {
        const size_t begin = b * batchSize;
        const size_t end = (begin + batchSize < count) ? begin + batchSize : count;
        JobItem item;
        item.fn = [shared, begin, end] { (*shared)(begin, end); };
        item.counterId = counterId;
        m_impl->PushCompute(std::move(item));
    }
    return JobHandle{counterId};
}

void JobSystem::Wait(JobHandle h) {
    if (!h.IsValid()) return;
    // busy-help: 대기 중 컴퓨트 잡을 훔쳐 실행. IO 잡은 훔치지 않는다(계약).
    // 훔칠 컴퓨트 잡이 없으면 양보(다른 워커/IO 스레드가 진행하도록).
    while (!m_impl->CounterDone(h.opaque)) {
        if (!m_impl->TryRunOneCompute())
            std::this_thread::yield();
    }
}

bool JobSystem::IsComplete(JobHandle h) const {
    if (!h.IsValid()) return true;
    return m_impl->CounterDone(h.opaque);
}

void JobSystem::RunOnMainThread(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(m_impl->mainMutex);
    m_impl->mainTasks.push_back(std::move(fn));
}

void JobSystem::PumpMainThreadTasks() {
    // 호출 스레드를 메인 스레드로 확정 기록(04 Finalize·RunOnMainThread 실행 스레드).
    m_impl->mainThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lk(m_impl->mainMutex);
        tasks.swap(m_impl->mainTasks);
    }
    for (auto& t : tasks) if (t) t();
}

uint32_t JobSystem::WorkerCount() const { return m_impl->workerCount; }

} // namespace mye
