// mye/scene/SystemScheduler.h — 5페이즈 시스템 스케줄러 (docs/03 §2)
//
// 페이즈: Input → FixedUpdate → Update → PostUpdate → RenderExtract.
// 01 UpdatePhase 틱 내부에서 도는 하위 스케줄러다(독립 루프 아님). 매핑(docs/03 §2):
//   Input        → 01 PreUpdate (전역 버스 Flush·입력 스냅샷 확정 이후)
//   FixedUpdate  → 01 FixedUpdate (고정 dt, N회)
//   Update       → 01 Update
//   PostUpdate   → 01 PostUpdate (Transform 전파·애니 샘플링·카메라)
//   RenderExtract→ 01 PreRender 초입 (02 보간 렌더보다 먼저 완료)
//
// 순서: 시스템은 페이즈 소속 + 페이즈 내 runBefore/runAfter 제약으로 순서를 선언한다
//   (토폴로지 정렬). 명시적 정수 우선순위는 쓰지 않는다(플러그인 정수 조정 지옥 회피).
// 구조 변경: 시스템은 CommandBuffer에 기록하고, 스케줄러가 각 페이즈 실행 후 경계에서
//   Flush한다. 그 직후 월드-로컬 EventBus도 Flush한다(게임플레이 이벤트, docs/03 §2).
#pragma once

#include "mye/core/Time.h"
#include "mye/ecs/ComponentType.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mye { class EventBus; }
namespace mye::ecs { class World; class CommandBuffer; }

namespace mye::scene {

enum class Phase : uint8_t { Input, FixedUpdate, Update, PostUpdate, RenderExtract, Count };

// 시스템 함수 시그니처 — World·CommandBuffer·프레임 타이밍을 받는다.
using SystemFn = std::function<void(ecs::World&, ecs::CommandBuffer&, const TimeStep&)>;

struct SystemDesc {
    std::string name;
    Phase       phase = Phase::Update;
    SystemFn    fn;

    // 병렬화 대비 선언(M2-A는 단일 스레드 — 선언만 보관, 디스패치는 후속).
    std::vector<ecs::ComponentTypeId> reads;
    std::vector<ecs::ComponentTypeId> writes;

    // 페이즈 내 토폴로지 제약(시스템 이름 기준). 순환 시 등록 거부 + 에러 로그.
    std::vector<std::string> runBefore;
    std::vector<std::string> runAfter;
};

struct SystemId { uint32_t value = 0; bool IsValid() const { return value != 0; } };

class SystemScheduler {
public:
    SystemScheduler(ecs::World& world, EventBus* worldBus);
    ~SystemScheduler();
    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    // 등록 — 반환 SystemId로 이후 해제 가능(플러그인·Lua 언로드). 토폴로지 재정렬 트리거.
    SystemId RegisterSystem(SystemDesc desc);
    void     UnregisterSystem(SystemId id);

    // 한 페이즈 실행: 토폴로지 순으로 시스템 실행 → CommandBuffer Flush → 월드 버스 Flush.
    // 01 UpdatePhase 틱 콜백(SceneModule::AddTick)이 각 페이즈에 대해 호출한다.
    void RunPhase(Phase phase, const TimeStep& time);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::scene
