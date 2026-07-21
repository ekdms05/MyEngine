// JobsTests.cpp — 잡 시스템(01, M2-A) 단위 테스트
//
// 검증 대상(임무 명세):
//   - ParallelFor 합산 정확성 + 경쟁 없음(atomic 누적 == 기대값)
//   - Wait 조인(모든 배치 완료 후 반환, IsComplete true)
//   - RunOnMainThread가 메인(=Pump 호출) 스레드에서만 실행
//   - ScheduleAfter 의존성 순서(논블로킹 continuation)
//   - 셧다워 무릭(소멸자에서 스레드 조인, 미실행 잡/메인작업 폐기 시 크래시 없음)
#include "TestFramework.h"

#include "mye/core/Jobs.h"

#include <atomic>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

using namespace mye;

// ---------------------------------------------------------------------------
// ParallelFor: [0,N) 합산 — 경쟁 없이 정확한 합을 내는지.
// ---------------------------------------------------------------------------
MYE_TEST(JobsParallelForSum) {
    JobSystem jobs;
    constexpr size_t N = 100000;
    std::vector<uint64_t> data(N);
    std::iota(data.begin(), data.end(), 1ull); // 1..N

    std::atomic<uint64_t> total{0};
    JobHandle h = jobs.ParallelFor(N, 1000, [&](size_t begin, size_t end) {
        uint64_t local = 0;
        for (size_t i = begin; i < end; ++i) local += data[i];
        total.fetch_add(local, std::memory_order_relaxed);
    });
    jobs.Wait(h);

    const uint64_t expected = static_cast<uint64_t>(N) * (N + 1) / 2;
    MYE_EXPECT(total.load() == expected);
    MYE_EXPECT(jobs.IsComplete(h));
}

// ParallelFor: 각 인덱스가 정확히 한 번만 방문되는지(중복·누락 없음).
MYE_TEST(JobsParallelForCoverage) {
    JobSystem jobs;
    constexpr size_t N = 4096;
    std::vector<std::atomic<int>> visits(N);
    for (auto& v : visits) v.store(0);

    JobHandle h = jobs.ParallelFor(N, 64, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            visits[i].fetch_add(1, std::memory_order_relaxed);
    });
    jobs.Wait(h);

    int mismatches = 0;
    for (auto& v : visits) if (v.load() != 1) ++mismatches;
    MYE_EXPECT(mismatches == 0);
}

// ParallelFor: count가 batchSize의 배수가 아닌 경우 경계 처리.
MYE_TEST(JobsParallelForRaggedBatch) {
    JobSystem jobs;
    constexpr size_t N = 1003; // 소수에 가까운, 비배수
    std::atomic<uint64_t> count{0};
    JobHandle h = jobs.ParallelFor(N, 100, [&](size_t begin, size_t end) {
        count.fetch_add(end - begin, std::memory_order_relaxed);
    });
    jobs.Wait(h);
    MYE_EXPECT(count.load() == N);
}

// ---------------------------------------------------------------------------
// Wait: Schedule한 잡이 반드시 완료된 뒤에 반환되는지.
// ---------------------------------------------------------------------------
MYE_TEST(JobsWaitJoins) {
    JobSystem jobs;
    std::atomic<bool> ran{false};
    JobHandle h = jobs.Schedule([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ran.store(true, std::memory_order_release);
    });
    MYE_EXPECT(!jobs.IsComplete(h) || ran.load()); // 아직 대기 전
    jobs.Wait(h);
    MYE_EXPECT(ran.load());
    MYE_EXPECT(jobs.IsComplete(h));
}

// 많은 잡을 스케줄 후 하나의 배치 카운터로 조인 — 경쟁·유실 없음.
MYE_TEST(JobsManyScheduleJoin) {
    JobSystem jobs;
    constexpr int K = 500;
    std::atomic<int> done{0};
    std::vector<JobHandle> handles;
    handles.reserve(K);
    for (int i = 0; i < K; ++i)
        handles.push_back(jobs.Schedule([&] { done.fetch_add(1, std::memory_order_relaxed); }));
    for (auto& h : handles) jobs.Wait(h);
    MYE_EXPECT(done.load() == K);
}

// ---------------------------------------------------------------------------
// ScheduleAfter: 의존성이 끝난 뒤에 continuation이 실행되는지(순서 보장).
// ---------------------------------------------------------------------------
namespace {
struct AfterState {
    std::atomic<int>  order{0};
    std::atomic<int>  firstDoneAt{-1};
    std::atomic<int>  secondDoneAt{-1};
};
AfterState* g_after = nullptr;

void FirstJob(void*) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    g_after->firstDoneAt.store(g_after->order.fetch_add(1, std::memory_order_acq_rel));
}
void SecondJob(void*) {
    g_after->secondDoneAt.store(g_after->order.fetch_add(1, std::memory_order_acq_rel));
}
} // namespace

MYE_TEST(JobsScheduleAfterOrders) {
    JobSystem jobs;
    AfterState state;
    g_after = &state;

    JobHandle first = jobs.Schedule(&FirstJob, nullptr, "first");
    JobHandle second = jobs.ScheduleAfter(first, &SecondJob, nullptr, "second");
    jobs.Wait(second);

    MYE_EXPECT(state.firstDoneAt.load() >= 0);
    MYE_EXPECT(state.secondDoneAt.load() >= 0);
    // second는 first가 완료된 뒤에 시작 → order 값이 first보다 크다.
    MYE_EXPECT(state.secondDoneAt.load() > state.firstDoneAt.load());
    g_after = nullptr;
}

// ---------------------------------------------------------------------------
// RunOnMainThread: 큐잉된 작업은 오직 PumpMainThreadTasks 호출 스레드에서만 실행.
// ---------------------------------------------------------------------------
MYE_TEST(JobsRunOnMainThreadOnMainOnly) {
    JobSystem jobs;
    const std::thread::id mainId = std::this_thread::get_id();

    std::atomic<int> ran{0};
    std::atomic<bool> wrongThread{false};

    // 워커 잡 안에서 메인 스레드 작업을 큐잉(04 Finalize 패턴 모사).
    JobHandle h = jobs.Schedule([&] {
        jobs.RunOnMainThread([&, mainId] {
            if (std::this_thread::get_id() != mainId) wrongThread.store(true);
            ran.fetch_add(1, std::memory_order_relaxed);
        });
    });
    jobs.Wait(h);

    // Pump 전에는 실행되지 않아야 한다.
    MYE_EXPECT(ran.load() == 0);
    jobs.PumpMainThreadTasks();
    MYE_EXPECT(ran.load() == 1);
    MYE_EXPECT(!wrongThread.load());
}

// 여러 개 큐잉 후 한 번의 Pump로 모두 소진(FIFO 소진).
MYE_TEST(JobsRunOnMainThreadDrainsAll) {
    JobSystem jobs;
    std::atomic<int> counter{0};
    for (int i = 0; i < 32; ++i)
        jobs.RunOnMainThread([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    jobs.PumpMainThreadTasks();
    MYE_EXPECT(counter.load() == 32);
    // 두 번째 Pump는 no-op(큐 비었음).
    jobs.PumpMainThreadTasks();
    MYE_EXPECT(counter.load() == 32);
}

// ---------------------------------------------------------------------------
// IO 큐: IO 잡이 실행되고 조인되는지(전용 IO 스레드 경로).
// ---------------------------------------------------------------------------
MYE_TEST(JobsIoQueueRuns) {
    JobSystem jobs;
    std::atomic<bool> ran{false};
    JobHandle h = jobs.Schedule([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ran.store(true, std::memory_order_release);
    }, QueueKind::IO);
    jobs.Wait(h);
    MYE_EXPECT(ran.load());
    MYE_EXPECT(jobs.IsComplete(h));
}

// ---------------------------------------------------------------------------
// 셧다운 무릭: 미실행 잡·미소진 메인 작업이 남아도 소멸자가 깔끔히 조인.
// ---------------------------------------------------------------------------
MYE_TEST(JobsShutdownNoLeak) {
    std::atomic<int> ran{0};
    {
        JobSystem jobs;
        // 실행될 수도 안 될 수도 있는 잡을 대량 투입 후 즉시 파괴.
        for (int i = 0; i < 128; ++i)
            jobs.Schedule([&] { ran.fetch_add(1, std::memory_order_relaxed); });
        // 소진하지 않은 메인 작업.
        jobs.RunOnMainThread([&] { ran.fetch_add(1, std::memory_order_relaxed); });
        // 명시 대기 없이 스코프 탈출 → 소멸자가 running=false, 스레드 조인.
    }
    // 크래시/행 없이 여기 도달하면 성공. ran은 0..128 사이 어디든 유효.
    MYE_EXPECT(true);
}

// 빈 ParallelFor / 무효 핸들 경계.
MYE_TEST(JobsEmptyAndInvalid) {
    JobSystem jobs;
    JobHandle empty = jobs.ParallelFor(0, 16, [](size_t, size_t) {});
    MYE_EXPECT(!empty.IsValid());
    MYE_EXPECT(jobs.IsComplete(empty)); // 무효 핸들 = 완료로 취급
    jobs.Wait(empty);                   // no-op, 행 없음

    JobHandle nul{};
    MYE_EXPECT(!nul.IsValid());
    MYE_EXPECT(jobs.IsComplete(nul));
    jobs.Wait(nul);
    MYE_EXPECT(jobs.WorkerCount() >= 1);
}
