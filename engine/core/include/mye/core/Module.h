// mye/core/Module.h — 모듈 라이프사이클 + EngineContext 서비스 게이트웨이 (docs/01 소유 주제)
//
// 수명: OnLoad(등록만) → OnInitialize(의존성 위상 정렬 순) → OnPostInitialize(교차 배선)
//       → 페이즈 틱 → OnShutdown/OnUnload(정확한 역순).
// EngineContext는 가상 서비스 게이트웨이(유일한 전역 접근점) — 핵심은 GetServiceRaw(ServiceId).
// 편의 래퍼는 전부 GetServiceRaw 위임(가상 경계만 넘으므로 DLL 안전).
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Time.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye {

class EventBus;
class ConfigSystem;
class TimeSystem;
class IWindow;
class ModuleRegistry;
class JobSystem;

enum class UpdatePhase : uint8_t {
    PreUpdate, FixedUpdate, Update, PostUpdate, PreRender, PostRender, Count
};

struct EnginePaths {
    std::string engineDir;    // 엔진 배포물 루트 (engine.json 위치)
    std::string projectDir;   // --project 인자 (config/project.json)
    std::string userDir;      // %LOCALAPPDATA%/MyEngine/<project>
};

// 주의: 편의 래퍼의 ServiceId는 MYE_SERVICE(TypeName)의 #TypeName 해시와 반드시 일치해야 한다.
// (전방 선언 타입의 kServiceId를 참조할 수 없어 이름 리터럴 해시를 직접 사용)
inline constexpr ServiceId kMainWindowServiceId = HashFnv1a64("MainWindow");

class EngineContext {
public:
    virtual ~EngineContext() = default;

    virtual void*    GetServiceRaw(ServiceId id) = 0;                     // 미등록 시 nullptr
    virtual void     RegisterServiceRaw(ServiceId id, void* service) = 0; // OnLoad/OnInitialize에서
    virtual void     UnregisterServiceRaw(ServiceId id) = 0;              // OnShutdown 역순 해제
    virtual uint32_t GetEngineVersion() const = 0;                        // 05 버전 협상용
    virtual const EnginePaths& GetPaths() const = 0;

    // ---- 인라인 편의 래퍼 (Jobs()/FileWatcher()는 M1 Phase 1에서 추가) ----
    EventBus&       Events()  { return *static_cast<EventBus*>(GetServiceRaw(HashFnv1a64("EventBus"))); }
    ConfigSystem&   Config()  { return *static_cast<ConfigSystem*>(GetServiceRaw(HashFnv1a64("ConfigSystem"))); }
    TimeSystem&     Time()    { return *static_cast<TimeSystem*>(GetServiceRaw(HashFnv1a64("TimeSystem"))); }
    IWindow&        MainWindow() { return *static_cast<IWindow*>(GetServiceRaw(kMainWindowServiceId)); }
    ModuleRegistry& Modules() { return *static_cast<ModuleRegistry*>(GetServiceRaw(HashFnv1a64("ModuleRegistry"))); }
    // M2-A: 잡 시스템 접근. 04 비동기 로딩·03 병렬 시스템 디스패치가 소비.
    JobSystem&      Jobs()    { return *static_cast<JobSystem*>(GetServiceRaw(HashFnv1a64("JobSystem"))); }

    template <typename T>
    T* GetService() { return static_cast<T*>(GetServiceRaw(T::kServiceId)); }
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual const char* GetName() const = 0;
    // 이름 문자열 목록으로 의존성 선언 → ModuleRegistry가 위상 정렬 (순환 시 초기화 거부)
    virtual std::span<const char* const> GetDependencies() const { return {}; }

    virtual void OnLoad(EngineContext& /*ctx*/) {}           // 등록만. 타 모듈 접근 금지
    virtual void OnInitialize(EngineContext& /*ctx*/) {}     // 의존성 순서 보장
    virtual void OnPostInitialize(EngineContext& /*ctx*/) {} // 전 모듈 초기화 후 교차 배선
    virtual void OnShutdown(EngineContext& /*ctx*/) {}       // 초기화 역순
    virtual void OnUnload(EngineContext& /*ctx*/) {}
};

class ModuleRegistry {
public:
    MYE_SERVICE(ModuleRegistry);

    ModuleRegistry();
    ~ModuleRegistry();
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    // 부팅 시 정적 모듈 등록 경로 (Application::OnRegisterModules에서 호출)
    void Register(std::unique_ptr<IModule> module);

    // 플러그인 로드 단계(코어 초기화 후·프로젝트 로드 전)용 late-register:
    // 배치 단위 위상 정렬 → OnLoad → OnInitialize → OnPostInitialize 수행. 실패 시 Error.
    Expected<void, Error> RegisterBatch(std::vector<std::unique_ptr<IModule>> modules);

    // 페이즈 틱 등록. 같은 페이즈 내 순서는 orderKey(작을수록 먼저) 결정적 정렬.
    void AddTick(IModule* module, UpdatePhase phase,
                 std::function<void(const TimeStep&)> tick, int orderKey = 0);

    IModule* Find(std::string_view name) const;   // 없으면 nullptr

    // ---- 엔진(GuardedMain) 전용 ----
    Expected<void, Error> InitializeAll(EngineContext& ctx);  // 위상 정렬 → OnLoad → OnInitialize → OnPostInitialize
    void ShutdownAll(EngineContext& ctx);                     // 정확한 역순 OnShutdown → OnUnload
    void Tick(UpdatePhase phase, const TimeStep& step);       // orderKey 정렬 순 틱 실행

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye
