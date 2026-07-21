// Module.cpp — 모듈 라이프사이클 구현 (docs/01 "모듈 라이프사이클")
//
// 구현 노트:
//  - InitializeAll: 이름 문자열 의존성 기반 DFS 위상 정렬(중복 이름·미지 의존성·순환 감지 시
//    모듈 이름을 포함한 명확한 Error 반환) → 정렬 순서로 modules 재배열 →
//    OnLoad 전체 → OnInitialize 전체 → OnPostInitialize 전체.
//  - ShutdownAll: 초기화의 정확한 역순으로 OnShutdown 전체 → OnUnload 전체.
//  - RegisterBatch: 플러그인 로드 단계(코어 초기화 후) 전용 late-register.
//    기존 등록 모듈은 "충족된 외부 의존성"으로 취급하고 배치만 따로 위상 정렬 →
//    OnLoad → OnInitialize → OnPostInitialize. 검증 실패 시 아무것도 등록하지 않는다.
//    (헤더 시그니처에 EngineContext 인자가 없으므로 InitializeAll에서 받은 ctx를 보관·재사용)
//  - Tick: 페이즈별 orderKey(작을수록 먼저) 정렬 순 실행. 실행 중인 페이즈 목록에 대한
//    AddTick은 순회 안전을 위해 디버그 어서트로 금지 (M0 — 지연 등록은 필요 시 확장).
#include "mye/core/Module.h"

#include "mye/core/Assert.h"
#include "mye/core/Log.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace mye {

namespace {

// DFS 위상 정렬 — batch 인덱스를 "의존성 먼저" 순서로 산출.
// external이 주어지면 batch에 없는 의존성 이름을 external에서 추가로 찾는다(RegisterBatch용).
struct TopoSorter {
    const std::vector<std::unique_ptr<IModule>>& batch;
    const std::vector<std::unique_ptr<IModule>>* external = nullptr;   // 이미 초기화된 모듈(nullable)

    std::unordered_map<std::string_view, size_t> nameToIndex;
    std::vector<uint8_t>          marks;   // 0=미방문, 1=방문 중(DFS 경로), 2=완료
    std::vector<size_t>           order;   // 결과: 의존성이 앞에 오는 순서
    std::vector<std::string_view> path;    // 순환 경로 보고용 현재 DFS 스택

    Expected<void, Error> Run() {
        nameToIndex.reserve(batch.size());
        for (size_t i = 0; i < batch.size(); ++i) {
            const std::string_view name = batch[i]->GetName();
            if (!nameToIndex.emplace(name, i).second)
                return Error{std::format("ModuleRegistry: 중복 모듈 이름 '{}'", name), 1};
        }
        marks.assign(batch.size(), 0);
        order.reserve(batch.size());
        for (size_t i = 0; i < batch.size(); ++i)
            if (auto r = Visit(i); !r)
                return r;
        return {};
    }

    bool IsExternallySatisfied(std::string_view dep) const {
        if (external == nullptr)
            return false;
        for (const auto& m : *external)
            if (dep == m->GetName())
                return true;
        return false;
    }

    Expected<void, Error> Visit(size_t index) {
        if (marks[index] == 2)
            return {};   // 완료
        if (marks[index] == 1) {
            // 순환 감지 — DFS 경로에서 index 이름이 처음 등장한 지점부터 경로를 조립
            const std::string_view name = batch[index]->GetName();
            std::string cycle;
            bool inCycle = false;
            for (const std::string_view n : path) {
                if (n == name)
                    inCycle = true;
                if (inCycle) {
                    cycle += n;
                    cycle += " -> ";
                }
            }
            cycle += name;
            return Error{std::format("ModuleRegistry: 모듈 의존성 순환 감지: {}", cycle), 2};
        }

        marks[index] = 1;
        path.push_back(batch[index]->GetName());
        for (const char* dep : batch[index]->GetDependencies()) {
            const auto it = nameToIndex.find(std::string_view{dep});
            if (it == nameToIndex.end()) {
                if (IsExternallySatisfied(dep))
                    continue;   // 이미 초기화된 모듈이 충족
                return Error{std::format("ModuleRegistry: 모듈 '{}'의 의존성 '{}'을(를) 찾을 수 없음",
                                         batch[index]->GetName(), dep), 3};
            }
            if (auto r = Visit(it->second); !r)
                return r;
        }
        path.pop_back();
        marks[index] = 2;
        order.push_back(index);
        return {};
    }
};

} // namespace

struct ModuleRegistry::Impl {
    struct TickEntry {
        IModule*    module = nullptr;
        int         orderKey = 0;
        std::function<void(const TimeStep&)> tick;
    };

    std::vector<std::unique_ptr<IModule>> modules;   // 초기화 순서(위상 정렬 결과)로 유지
    std::vector<TickEntry> ticks[static_cast<size_t>(UpdatePhase::Count)];
    bool           initialized = false;
    EngineContext* context = nullptr;                // InitializeAll에서 보관 — RegisterBatch가 사용
    UpdatePhase    activeTickPhase = UpdatePhase::Count;   // Tick 재진입/순회 중 변형 감지용
};

ModuleRegistry::ModuleRegistry() : m_impl(std::make_unique<Impl>()) {}
ModuleRegistry::~ModuleRegistry() = default;

void ModuleRegistry::Register(std::unique_ptr<IModule> module) {
    auto& s = *m_impl;
    if (!MYE_VERIFY(module != nullptr, "ModuleRegistry::Register: null 모듈"))
        return;
    if (!MYE_VERIFY(!s.initialized,
                    "ModuleRegistry::Register: 초기화 이후 등록 금지 — RegisterBatch를 사용하라")) {
        MYE_LOG_ERROR("Module", "Register('{}') 무시됨 — 초기화 이후에는 RegisterBatch를 사용",
                      module->GetName());
        return;
    }
    s.modules.push_back(std::move(module));
}

Expected<void, Error> ModuleRegistry::RegisterBatch(std::vector<std::unique_ptr<IModule>> modules) {
    auto& s = *m_impl;
    if (!s.initialized || s.context == nullptr)
        return Error{"ModuleRegistry::RegisterBatch: InitializeAll 이전 — 부팅 등록은 Register를 사용하라", 4};
    if (modules.empty())
        return {};

    for (const auto& m : modules) {
        if (!MYE_VERIFY(m != nullptr, "ModuleRegistry::RegisterBatch: null 모듈"))
            return Error{"ModuleRegistry::RegisterBatch: null 모듈 포함", 1};
        if (Find(m->GetName()) != nullptr)
            return Error{std::format("ModuleRegistry: 모듈 이름 '{}'이(가) 이미 등록됨", m->GetName()), 1};
    }

    // 배치만 따로 위상 정렬 — 기존 등록·초기화된 모듈은 충족된 의존성으로 취급.
    // 검증 실패 시 아무것도 등록하지 않고 Error 반환(강한 보장).
    TopoSorter sorter{modules, &s.modules};
    if (auto r = sorter.Run(); !r)
        return r;

    std::vector<IModule*> batchOrder;   // 배치 내 초기화 순서
    batchOrder.reserve(modules.size());
    for (const size_t idx : sorter.order) {
        batchOrder.push_back(modules[idx].get());
        s.modules.push_back(std::move(modules[idx]));   // 전체 초기화 순서 뒤에 이어붙임 → 역순 종료 보장
    }

    EngineContext& ctx = *s.context;
    for (IModule* m : batchOrder) m->OnLoad(ctx);
    for (IModule* m : batchOrder) m->OnInitialize(ctx);
    for (IModule* m : batchOrder) m->OnPostInitialize(ctx);

    MYE_LOG_INFO("Module", "RegisterBatch: 모듈 {}개 late-register 완료", batchOrder.size());
    return {};
}

void ModuleRegistry::AddTick(IModule* module, UpdatePhase phase,
                             std::function<void(const TimeStep&)> tick, int orderKey) {
    auto& s = *m_impl;
    if (!MYE_VERIFY(static_cast<size_t>(phase) < static_cast<size_t>(UpdatePhase::Count),
                    "ModuleRegistry::AddTick: 잘못된 페이즈"))
        return;
    if (!MYE_VERIFY(static_cast<bool>(tick), "ModuleRegistry::AddTick: 빈 틱 콜백"))
        return;
    // 실행 중인 페이즈 목록에 대한 삽입은 순회를 깨뜨린다 (M0: 금지 — 필요 시 지연 등록으로 확장)
    MYE_ASSERT(s.activeTickPhase != phase,
               "ModuleRegistry::AddTick: 현재 실행 중인 페이즈에는 틱을 추가할 수 없음");

    auto& list = s.ticks[static_cast<size_t>(phase)];
    Impl::TickEntry entry{module, orderKey, std::move(tick)};
    // orderKey 오름차순(작을수록 먼저), 동일 키는 등록 순서 유지
    const auto pos = std::upper_bound(
        list.begin(), list.end(), orderKey,
        [](int key, const Impl::TickEntry& e) { return key < e.orderKey; });
    list.insert(pos, std::move(entry));
}

IModule* ModuleRegistry::Find(std::string_view name) const {
    for (const auto& m : m_impl->modules)
        if (name == m->GetName())
            return m.get();
    return nullptr;
}

Expected<void, Error> ModuleRegistry::InitializeAll(EngineContext& ctx) {
    auto& s = *m_impl;
    if (!MYE_VERIFY(!s.initialized, "ModuleRegistry::InitializeAll: 중복 호출"))
        return Error{"ModuleRegistry::InitializeAll: 이미 초기화됨", 4};

    // 의존성 위상 정렬 (중복 이름·미지 의존성·순환 시 모듈 이름 포함 Error)
    TopoSorter sorter{s.modules};
    if (auto r = sorter.Run(); !r)
        return r;

    // 정렬 결과 순서로 재배열 — 이후 ShutdownAll의 역순 순회가 "정확한 역순"이 된다
    std::vector<std::unique_ptr<IModule>> ordered;
    ordered.reserve(s.modules.size());
    for (const size_t idx : sorter.order)
        ordered.push_back(std::move(s.modules[idx]));
    s.modules = std::move(ordered);

    // 정렬 순서로 OnLoad 전체 → OnInitialize 전체 → OnPostInitialize 전체
    for (auto& m : s.modules) m->OnLoad(ctx);
    for (auto& m : s.modules) m->OnInitialize(ctx);
    for (auto& m : s.modules) m->OnPostInitialize(ctx);

    s.initialized = true;
    s.context = &ctx;   // 플러그인 로드 단계의 RegisterBatch가 사용
    return {};
}

void ModuleRegistry::ShutdownAll(EngineContext& ctx) {
    auto& s = *m_impl;
    // 더 이상 틱이 돌지 않도록 먼저 비운다
    for (auto& list : s.ticks)
        list.clear();

    // 초기화의 정확한 역순: OnShutdown 전체(역순) → OnUnload 전체(역순)
    for (auto it = s.modules.rbegin(); it != s.modules.rend(); ++it)
        (*it)->OnShutdown(ctx);
    for (auto it = s.modules.rbegin(); it != s.modules.rend(); ++it)
        (*it)->OnUnload(ctx);

    s.modules.clear();
    s.initialized = false;
    s.context = nullptr;
}

void ModuleRegistry::Tick(UpdatePhase phase, const TimeStep& step) {
    auto& s = *m_impl;
    if (!MYE_VERIFY(static_cast<size_t>(phase) < static_cast<size_t>(UpdatePhase::Count),
                    "ModuleRegistry::Tick: 잘못된 페이즈"))
        return;
    MYE_ASSERT(s.activeTickPhase == UpdatePhase::Count, "ModuleRegistry::Tick: 재진입 금지");

    s.activeTickPhase = phase;
    for (auto& entry : s.ticks[static_cast<size_t>(phase)])
        entry.tick(step);
    s.activeTickPhase = UpdatePhase::Count;
}

} // namespace mye
