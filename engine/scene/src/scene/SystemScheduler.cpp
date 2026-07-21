// mye/scene/SystemScheduler.cpp — 5페이즈 스케줄러 구현 (docs/03 §2)
//
// M2-A 기준 구현: 페이즈별 시스템 목록을 runBefore/runAfter 토폴로지 정렬해 실행하고,
// 페이즈 경계에서 CommandBuffer + 월드-로컬 EventBus를 Flush한다. 병렬 디스패치(선언
// 액세스 기반)는 후속 구현 에이전트 몫 — reads/writes는 보관만 한다.
#include "mye/scene/SystemScheduler.h"

#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/ecs/CommandBuffer.h"
#include "mye/ecs/World.h"

#include <unordered_map>
#include <unordered_set>

namespace mye::scene {

struct SystemScheduler::Impl {
    ecs::World* world = nullptr;
    EventBus*   worldBus = nullptr;

    struct Entry {
        SystemId id;
        SystemDesc desc;
    };
    std::vector<Entry> systems[static_cast<size_t>(Phase::Count)];
    // 페이즈별 토폴로지 정렬 결과(systems 인덱스 순서). 등록/해제 시 재계산.
    std::vector<size_t> order[static_cast<size_t>(Phase::Count)];
    uint32_t nextId = 1;

    std::unique_ptr<ecs::CommandBuffer> cmd;

    void Resort(Phase phase) {
        const size_t pi = static_cast<size_t>(phase);
        auto& sys = systems[pi];
        auto& out = order[pi];
        out.clear();

        // 이름 → 인덱스.
        std::unordered_map<std::string, size_t> nameToIdx;
        for (size_t i = 0; i < sys.size(); ++i) nameToIdx[sys[i].desc.name] = i;

        // 인접 리스트: a → b 는 "a가 b보다 먼저".
        std::vector<std::vector<size_t>> adj(sys.size());
        std::vector<int> indeg(sys.size(), 0);
        auto addEdge = [&](size_t a, size_t b) {
            adj[a].push_back(b);
            indeg[b]++;
        };
        for (size_t i = 0; i < sys.size(); ++i) {
            for (const auto& before : sys[i].desc.runBefore)
                if (auto it = nameToIdx.find(before); it != nameToIdx.end()) addEdge(i, it->second);
            for (const auto& after : sys[i].desc.runAfter)
                if (auto it = nameToIdx.find(after); it != nameToIdx.end()) addEdge(it->second, i);
        }

        // Kahn 위상 정렬(안정성 위해 등록 순 유지).
        std::vector<size_t> queue;
        for (size_t i = 0; i < sys.size(); ++i) if (indeg[i] == 0) queue.push_back(i);
        size_t head = 0;
        while (head < queue.size()) {
            const size_t n = queue[head++];
            out.push_back(n);
            for (size_t m : adj[n]) if (--indeg[m] == 0) queue.push_back(m);
        }
        if (out.size() != sys.size()) {
            MYE_LOG_ERROR("Scene", "SystemScheduler: cyclic before/after in phase {} — falling back to registration order",
                          static_cast<int>(phase));
            out.clear();
            for (size_t i = 0; i < sys.size(); ++i) out.push_back(i);
        }
    }
};

SystemScheduler::SystemScheduler(ecs::World& world, EventBus* worldBus)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->world = &world;
    m_impl->worldBus = worldBus;
    m_impl->cmd = std::make_unique<ecs::CommandBuffer>(world);
}
SystemScheduler::~SystemScheduler() = default;

SystemId SystemScheduler::RegisterSystem(SystemDesc desc) {
    const Phase phase = desc.phase;
    const size_t pi = static_cast<size_t>(phase);
    SystemId id{m_impl->nextId++};
    m_impl->systems[pi].push_back(Impl::Entry{id, std::move(desc)});
    m_impl->Resort(phase);
    return id;
}

void SystemScheduler::UnregisterSystem(SystemId id) {
    for (size_t pi = 0; pi < static_cast<size_t>(Phase::Count); ++pi) {
        auto& sys = m_impl->systems[pi];
        for (size_t i = 0; i < sys.size(); ++i) {
            if (sys[i].id.value == id.value) {
                sys.erase(sys.begin() + i);
                m_impl->Resort(static_cast<Phase>(pi));
                return;
            }
        }
    }
}

void SystemScheduler::RunPhase(Phase phase, const TimeStep& time) {
    const size_t pi = static_cast<size_t>(phase);
    auto& sys = m_impl->systems[pi];
    for (size_t idx : m_impl->order[pi]) {
        if (idx < sys.size() && sys[idx].desc.fn)
            sys[idx].desc.fn(*m_impl->world, *m_impl->cmd, time);
    }
    // 페이즈 경계: 구조 변경 일괄 적용.
    m_impl->cmd->Flush();
    // 그 직후 월드-로컬 게임플레이 이벤트 Flush(docs/03 §2).
    if (m_impl->worldBus) {
        m_impl->worldBus->Flush(EventChannel::PreUpdate);
        m_impl->worldBus->Flush(EventChannel::PostUpdate);
    }
}

} // namespace mye::scene
