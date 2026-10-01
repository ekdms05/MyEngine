// Lua VM ownership and protected game scripting.
#pragma once

#include "mye/core/Base.h"
#include "mye/script/ScriptTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace mye {
class EventBus;
namespace asset { class AssetManager; }
}

namespace mye::script {

class IBindingModule;
class CoroutineScheduler;

// Standard libraries exposed to game scripts (docs/19-lua-api.md).
// This policy does not impose execution-time or allocation limits.
struct StdLibPolicy {
    bool base = true;        // print/pairs/ipairs/type/tostring/... (print 는 로그로 리다이렉트)
    bool package = false;    // require/package are unavailable by default.
    bool coroutine = true;   // 코루틴 프리미티브(연출 스케줄러 기반)
    bool table = true;
    bool string = true;
    bool math = true;
    bool utf8 = true;
    bool io = false;         // Raw filesystem access is unavailable by default.
    bool os = false;         // os.execute/exit 등 — 은닉(안전상)
    bool debug = false;      // traceback 만 내부적으로 사용(전체 노출 금지)
};

// 청크/콜백 실행 결과 — 실패 시 파싱된 파일·라인·메시지(traceback)를 담는다.
//   ScriptSystem 이 이걸로 ScriptErrorEvent 를 채우고 hasError 를 세운다.
struct ScriptError {
    std::string file;        // 에러 소스 vpath(알 수 없으면 비움)
    int32_t     line = 0;
    std::string message;     // traceback 포함(UTF-8)
};

class ScriptRuntime {
public:
    ScriptRuntime();
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // ---- 초기화 ----
    // 표준 lib 선별 오픈 + `mye` 전역 테이블 생성 + 코루틴 스케줄러 배선 + print 로그 리다이렉트.
    //   events publishes coroutine errors; assets is available to app binding modules.
    //   Both services are borrowed and may be null.
    void Initialize(const StdLibPolicy& policy, EventBus* events, asset::AssetManager* assets);
    void Shutdown();                      // Cancel tasks, close VM, then release binding contexts.

    // ---- 바인딩 모듈 ----
    // 등록만 하고 Initialize 이후(또는 이 호출 시점)에 즉시 적용한다. 모듈은 비소유(호출자 수명 보장)
    //   또는 소유(unique_ptr) 두 경로 제공. VM 재생성(ReapplyBindings) 시 전부 다시 Register 된다.
    void AddBindingModule(IBindingModule* module);                    // 비소유
    void AddBindingModule(std::unique_ptr<IBindingModule> module);    // 소유(런타임이 보관)
    void ReapplyBindings();               // Register modules not yet applied to the current VM.
    // 매 프레임 — 등록 모듈의 OnUpdate(dt) 호출(예: ddc Lua 시스템 실행). 앱이 프레임마다 호출.
    void UpdateBindings(float dt);

    // ---- 안전한 실행 ----
    // Execute text source and discard its return values. For a class table use
    // LoadClass in ScriptClass.h. chunkName identifies the source in diagnostics.
    Expected<void, ScriptError> DoString(std::string_view source, std::string_view chunkName);

    // ---- 코루틴 ----
    CoroutineScheduler& Coroutines();
    // 매 프레임 호출 — 대기 조건(wait_seconds/wait_event) 검사 후 코루틴 재개.
    void UpdateCoroutines(float dt);
    // Advance incremental collection by the configured allocation step.
    void CollectGarbageStep();

    // ---- 저수준 접근(바인딩 계층·ScriptSystem 전용) ----
    // 비소유 C API 포인터. VM 종료·재생성 뒤에는 재조회해야 한다.
    lua_State* State();
    bool        IsInitialized() const;

    EventBus* Events() const;                 // 비소유
    asset::AssetManager* Assets() const;      // 비소유

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::script
