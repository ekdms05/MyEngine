// Lua VM ownership and protected game scripting.
#pragma once

#include "mye/ecs/Entity.h"

#include "mye/script/LuaApi.h"

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace mye::script {

// 코루틴 재개 결과 — 실패 시 ScriptSystem 이 에러 격리 처리에 사용.
struct CoResumeError {
    ecs::Entity entity;
    std::string message;   // traceback 포함(UTF-8)
    int32_t     line = 0;
};

// 스케줄러가 관리하는 대기 종류.
enum class WaitKind : uint8_t {
    None,        // 대기 없음(다음 tick 즉시 재개 — yield())
    Seconds,     // wait_seconds(t)
    Event,       // wait_event(name)
    Finished,    // 코루틴 정상 종료(제거 대상)
};

// ---------------------------------------------------------------------------
// CoroutineScheduler — 활성 코루틴 목록·대기 상태 관리.
// ---------------------------------------------------------------------------
// ScriptRuntime 이 소유하며, `mye.co.*` 바인딩(start/wait_seconds/wait_event)이 이 스케줄러에
//   태스크를 등록한다. ScriptSystem::Update 가 매 프레임 Tick(dt) 을 호출한다.
class CoroutineScheduler {
public:
    CoroutineScheduler();
    ~CoroutineScheduler();
    CoroutineScheduler(const CoroutineScheduler&) = delete;
    CoroutineScheduler& operator=(const CoroutineScheduler&) = delete;

    //   owner 는 소유 엔티티(파괴 시 자동 취소용, 전역 코루틴이면 Null).
    //   반환: 등록된 코루틴의 핸들(취소·조회용). fn 은 인자 없는 Lua 함수.
    uint64_t Start(ecs::Entity owner, LuaReference fn);

    // 매 프레임 호출 — 경과 dt 로 wait_seconds 만료를 검사하고, 만료된/무대기 코루틴을
    //   protected 재개한다. 재개 중 에러가 나면 outErrors 에 누적(호출측이 격리 처리).
    //   정상 종료한 코루틴은 목록에서 제거한다.
    void Tick(float dt, std::vector<CoResumeError>& outErrors);

    // 이벤트 도착 통지 — wait_event(name) 로 대기 중인 코루틴을 재개 대상으로 표시한다.
    //   실제 재개는 다음 Tick 에서 수행(재귀·재진입 회피). payload 는 코루틴 재개 인자로 전달.
    void NotifyEvent(std::string_view name, const LuaReference& payload);

    // 소유 엔티티 파괴 시 그 엔티티의 모든 코루틴을 취소·제거.
    void CancelForEntity(ecs::Entity owner);

    // 전체 취소(VM 파기·씬 종료 시).
    void CancelAll();

    size_t ActiveCount() const;

    // `mye.co` 바인딩 등록 — start/wait_seconds/wait_event 를 lua["mye"]["co"] 에 심는다.
    //   ScriptRuntime 이 Initialize 시 호출한다(대기 프리미티브는 yield 를 발생시키는 Lua 측
    //   래퍼 + 이 스케줄러의 상태 기록으로 구성).
    void RegisterBindings(lua_State* lua);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::script
