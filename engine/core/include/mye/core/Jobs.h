// mye/core/Jobs.h — 잡 시스템 (docs/01 §잡 시스템, Phase 1)
//
// 구조: 메인 스레드 + 워커 (hardware_concurrency-1)개. MVP는 단일 전역 MPMC 큐로
//   시작(work-stealing 데크는 Phase 2 — 인터페이스 동일 유지). 잡 = "함수 포인터 +
//   데이터 포인터 + 카운터". 파이버 없음 — Wait는 busy-help(다른 잡을 훔쳐 실행).
//
// 의존성 모델: 각 잡은 JobCounter를 감소시키고, 후속 잡은 "카운터가 0이 되면 실행 가능"
//   조건으로 스케줄된다(카운터 기반 조인 모델).
//
// 컴퓨트/IO 큐 분리: Schedule은 QueueKind를 받는다. IO 큐는 전용 스레드 1~2개가 소비하며
//   블로킹 파일 IO가 컴퓨트 워커를 굶기는 것을 막는다. 계약: IO 잡은 컴퓨트 워커에서
//   실행되지 않으며, busy-help Wait는 IO 큐 잡을 훔치지 않는다.
//
// 메인 스레드 마샬링: RunOnMainThread(fn) 큐 — DX11 immediate context·win32 API처럼
//   메인 스레드 전용 작업용. 04 비동기 로딩의 "디코드는 워커, GPU 업로드는 메인" 패턴이 소비.
//   메인 루프(01)가 프레임마다 PumpMainThreadTasks()로 소진한다.
//
// M2-A 범위: 헤더 계약 동결 + 최소 스텁 구현(단일 전역 MPMC 큐, 워커 스레드, Schedule/
//   Wait/ParallelFor/RunOnMainThread). Lua는 워커 스레드에서 실행 금지 — 잡은 순수 C++
//   데이터 병렬만.
#pragma once

#include "mye/core/Base.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace mye {

// 잡 완료 추적 핸들 — 내부 카운터를 참조하는 불투명 값(opaque == 0 = null 핸들).
struct JobHandle {
    uint64_t opaque = 0;
    bool IsValid() const { return opaque != 0; }
};

// Compute: 워커 스레드 풀. IO: 전용 스레드 1~2개 — 블로킹 파일 IO는 반드시 IO 큐로.
// 계약: IO 잡은 컴퓨트 워커에서 실행되지 않으며, busy-help Wait는 IO 잡을 훔치지 않는다.
enum class QueueKind : uint8_t { Compute, IO };

class JobSystem {
public:
    MYE_SERVICE(JobSystem);

    // workerCount == 0 이면 hardware_concurrency-1 로 자동. ioThreadCount 기본 1.
    explicit JobSystem(uint32_t workerCount = 0, uint32_t ioThreadCount = 1);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // ---- 함수 포인터 + 데이터 경로 (플러그인·DLL 경계 안전, 캡처 없음) ----
    JobHandle Schedule(void (*fn)(void*), void* data, const char* debugName = nullptr,
                       QueueKind queue = QueueKind::Compute);
    // dependency 카운터가 0이 되면 실행 가능해지는 후속 잡.
    JobHandle ScheduleAfter(JobHandle dependency, void (*fn)(void*), void* data,
                            const char* debugName = nullptr,
                            QueueKind queue = QueueKind::Compute);

    // ---- 편의 오버로드: 캡처 람다 (내부적으로 힙/풀에 저장, 완료 시 해제) ----
    JobHandle Schedule(std::function<void()> fn, QueueKind queue = QueueKind::Compute);

    // parallel-for: [0,count)를 batchSize 단위로 분할해 병렬 실행.
    // body 시그니처: void(size_t begin, size_t end). 반환 핸들 Wait로 조인.
    JobHandle ParallelFor(size_t count, size_t batchSize,
                          std::function<void(size_t begin, size_t end)> body);

    // 완료 대기 — 대기 중 다른 컴퓨트 잡을 훔쳐 실행(busy-help). IO 잡은 훔치지 않는다.
    void Wait(JobHandle h);
    bool IsComplete(JobHandle h) const;

    // 메인 스레드 전용 작업 큐잉(임의 스레드에서 호출 가능). 메인 루프가 소진.
    void RunOnMainThread(std::function<void()> fn);
    // 메인 스레드가 프레임마다 호출 — 큐잉된 메인 스레드 작업을 모두 실행.
    // (01 메인 루프가 PreUpdate 또는 04 AssetManager::Update 경로에서 호출.)
    void PumpMainThreadTasks();

    uint32_t WorkerCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye
