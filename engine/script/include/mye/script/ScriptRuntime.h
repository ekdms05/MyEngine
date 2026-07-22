// mye/script/ScriptRuntime.h — 게임 Lua 스테이트 소유·안전 실행·바인딩 등록 (docs/05, M3-C)
//
// docs/05 §Lua 스테이트 소유 모델: 게임 로직은 ScriptSystem 이 소유한 단일 sol::state 에서
//   실행된다(에디터 스테이트는 07 소유, 여기선 게임 스테이트 1개만). ScriptRuntime 은:
//   - 단일 sol::state 소유·초기화(표준 lib 선별 노출).
//   - IBindingModule 등록·적용(register: 매 VM 생성 시 전부 재적용).
//   - 안전한 실행: 청크 로드·protected_function 호출을 Expected 로 감싸 에러를 격리.
//   - `mye` 전역 테이블 생성(바인딩 모듈의 루트).
//   - 코루틴 스케줄러 소유(wait_seconds/wait_event 프리미티브 — 매 프레임 재개).
//
// 이 헤더는 sol::state 를 값 멤버로 갖지 않고 pImpl 로 감춰 sol.hpp 전파를 막는다.
//   다만 protected_function 실행 결과·클래스 테이블 접근이 필요한 표면(ScriptComponent·
//   ScriptSystem)은 sol.hpp 를 직접 인클루드한다.
#pragma once

#include "mye/core/Base.h"
#include "mye/script/ScriptTypes.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// sol::state 전방 선언(무거운 sol.hpp 회피). 구현 .cpp 에서 <sol/sol.hpp> 포함.
namespace sol { class state; }

namespace mye {
class EventBus;
namespace asset { class AssetManager; }
}

namespace mye::script {

class IBindingModule;
class CoroutineScheduler;

// 표준 라이브러리 선별 노출 정책(docs/05 §은닉: 파일시스템 원시 접근·os 위험 함수 차단).
//   기본값은 게임 로직에 안전한 집합(base/table/string/math/coroutine/utf8). io/os/package 는
//   기본 비노출(require 는 04 VFS 커스텀 로더로 대체 — 후속). 필요한 팀이 명시적으로 켠다.
struct StdLibPolicy {
    bool base = true;        // print/pairs/ipairs/type/tostring/... (print 는 로그로 리다이렉트)
    bool package = false;    // require/package — VFS 커스텀 로더로 대체 예정
    bool coroutine = true;   // 코루틴 프리미티브(연출 스케줄러 기반)
    bool table = true;
    bool string = true;
    bool math = true;
    bool utf8 = true;
    bool io = false;         // 파일 I/O — 은닉(docs/05)
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
    //   events 는 ScriptErrorEvent 발행·wait_event 배선에 쓰인다(비소유, nullptr 허용=테스트).
    //   assets 는 require/스크립트 소스 로드에 쓰인다(비소유, nullptr 허용).
    void Initialize(const StdLibPolicy& policy, EventBus* events, asset::AssetManager* assets);
    void Shutdown();                      // 코루틴·바인딩 정리 후 sol::state 파기

    // ---- 바인딩 모듈 ----
    // 등록만 하고 Initialize 이후(또는 이 호출 시점)에 즉시 적용한다. 모듈은 비소유(호출자 수명 보장)
    //   또는 소유(unique_ptr) 두 경로 제공. VM 재생성(ReapplyBindings) 시 전부 다시 Register 된다.
    void AddBindingModule(IBindingModule* module);                    // 비소유
    void AddBindingModule(std::unique_ptr<IBindingModule> module);    // 소유(런타임이 보관)
    void ReapplyBindings();               // 모든 등록 모듈의 Register(state) 재호출

    // ---- 안전한 실행 ----
    // 소스 문자열을 청크로 로드·실행하고 반환값을 클래스 테이블로 취급(mye.script 규약).
    //   성공: 로드된 클래스 테이블(sol::table) 을 out 계약으로 다루기 위해 LoadClass 사용.
    //   chunkName 은 에러 메시지의 파일명으로 쓰인다("@" 접두사 규약은 내부에서 처리).
    Expected<void, ScriptError> DoString(std::string_view source, std::string_view chunkName);

    // .lua 스크립트 소스를 로드해 "클래스 테이블"을 등록/반환한다. 스크립트는 클래스 테이블을
    //   return 하는 규약(docs/05). 반환 핸들은 ScriptComponent 가 인스턴스화에 사용.
    //   실제 sol::table 반환 시그니처는 ScriptClass.h(ScriptComponent 계약) 에서 정의한다.

    // ---- 코루틴 ----
    CoroutineScheduler& Coroutines();
    // 매 프레임 호출 — 대기 조건(wait_seconds/wait_event/wait_frames) 검사 후 코루틴 재개.
    void UpdateCoroutines(float dt);
    // 인크리멘털 GC step(스파이크 방지, docs/05 성능 가이드). 프레임당 예산 소진.
    void CollectGarbageStep();

    // ---- 저수준 접근(바인딩 계층·ScriptSystem 전용) ----
    // sol::state 참조. 헤더에 sol.hpp 를 끌어오지 않으려 반환은 참조로 두되, 호출부는 sol.hpp
    //   를 포함한 TU 에서만 사용한다(ScriptSystem.cpp·바인딩 모듈).
    sol::state& State();
    bool        IsInitialized() const;

    EventBus* Events() const;                 // 비소유
    asset::AssetManager* Assets() const;      // 비소유

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::script
