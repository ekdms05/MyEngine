// mye/runtime/NpcSystem.h — NPC 배회·상호작용 시스템 (docs/06 §7, M6-B)
//
// 소유: 06 (M6-B). 테일즈위버풍 데모의 NPC 행동을 담당한다:
//   - 배회(wander): 웨이포인트 순회(patrol) 또는 랜덤 순회(random). 대기(Idle)↔이동(Moving)
//     상태를 오가며, MoveController 로 목표까지 전진하고 SpriteAnimator 로 8방향 애니를 구동한다.
//   - 플레이어 접근 감지: 플레이어가 alertRadius 안에 들어오면 배회를 멈추고 플레이어를 바라본다.
//   - 상호작용: 플레이어가 interactRadius 안에서 상호작용을 요청(E 키/트리거)하면 해당 NPC 의
//     on_interact 를 개시한다(대화/컷신). 상호작용 중에는 NPC·플레이어 이동을 정지시킨다.
//
// 시뮬/표현 분리(docs/06 §8): 상태 결정(웨이포인트 선택·타이머·상태전이)과 이동(MoveController)은
//   시뮬(고정틱)에서, 애니 파라미터 갱신은 표현에서 한다. 본 시스템은 Tick(고정틱) 만 노출하고
//   애니 배선은 콜백/컴포넌트 직접 갱신으로 처리한다(비소유 World).
//
// 배선 모델(sol/스크립트 경계 격리): 이 헤더는 sol.hpp 를 끌어오지 않는다. Lua 는 NpcBindings 로
//   NPC 를 등록하고, on_interact 를 콜백(InteractFn)으로 심는다. 상호작용 개시는 콜백 호출뿐이며,
//   실제 대화 코루틴은 Lua 가 mye.dialogue/cutscene 로 기술한다(에러 격리는 코루틴 스케줄러가).
//
// 다중 NPC 독립: 각 NPC 는 독립 레코드다. 한 NPC 의 배회/상호작용이 실패하거나 콜백이 던져도(Lua
//   에러는 코루틴 스케줄러가 격리) 나머지 NPC 는 계속 동작한다.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mye::ecs { class World; }

namespace mye::runtime {

class MoveController;

// NPC 배회 모드.
enum class WanderMode : uint8_t {
    None    = 0,   // 배회 안 함(제자리) — 상호작용만
    Patrol  = 1,   // 웨이포인트를 순서대로 순회(끝에서 되돌기/루프)
    Random  = 2,   // 웨이포인트 중 무작위 선택(또는 원점 주변 무작위)
};

// NPC 배회 상태(내부 상태기계). 테스트·데모가 질의.
enum class NpcState : uint8_t {
    Idle        = 0,   // 대기(다음 이동까지 타이머)
    Moving      = 1,   // 웨이포인트로 이동 중
    AlertPaused = 2,   // 플레이어 접근으로 정지·주시
    Interacting = 3,   // 상호작용(대화) 진행 중 — 이동 완전 정지
};

// NPC 등록 서술(데이터). Lua/데모가 채워 Register 로 등록한다.
struct NpcDesc {
    ecs::Entity entity = ecs::Entity::Null();   // NPC 엔티티(LocalTransform 필요)
    std::string id;                             // 논리 id(로그·on_interact 라우팅 키, 옵션)

    WanderMode  mode = WanderMode::Patrol;
    std::vector<Vec2> waypoints;                // 순회 지점(월드 XY). 비면 원점 주변 배회.
    bool        loop = true;                    // Patrol: 끝에서 처음으로 루프(false=핑퐁)

    float moveSpeed  = 2.0f;                     // 배회 이동 속도(월드/초)
    float waitMin    = 0.8f;                     // 웨이포인트 도착 후 최소 대기(초)
    float waitMax    = 2.0f;                     // 최대 대기(초)
    float wanderRadius = 3.0f;                   // Random 모드에서 원점 주변 반경(웨이포인트 없을 때)

    float alertRadius    = 2.5f;                 // 플레이어가 이 안에 들면 정지·주시
    float interactRadius = 1.5f;                 // 상호작용 가능 거리(E 키/트리거)
    bool  facePlayerOnAlert = true;              // 접근 시 플레이어를 바라볼지
};

// 상호작용 개시 콜백. NPC id·엔티티를 받아 대화/컷신을 시작한다(Lua on_interact 로 배선).
//   반환 true = 상호작용을 수락(NpcSystem 이 Interacting 으로 잠금). false = 무시(계속 배회).
using InteractFn = std::function<bool(const std::string& npcId, ecs::Entity npc)>;

// 상호작용 진행 여부 질의 콜백. 대화/컷신이 아직 진행 중이면 true.
//   NpcSystem 은 이게 false 가 되면 상호작용을 종료하고 배회를 재개한다(Lua 대화 상태 폴링).
using InteractBusyFn = std::function<bool(const std::string& npcId, ecs::Entity npc)>;

// ---------------------------------------------------------------------------
// NpcSystem — NPC 배회·접근·상호작용의 C++ 백엔드.
//   MoveController(비소유) 로 이동을 위임하고, World(비소유) 로 Transform·SpriteAnimator 를 읽고
//   쓴다. 플레이어 엔티티(SetPlayer) 기준으로 접근·상호작용 거리를 판정한다.
// ---------------------------------------------------------------------------
class NpcSystem {
public:
    MYE_SERVICE(mye::runtime::NpcSystem);

    NpcSystem();
    ~NpcSystem();
    NpcSystem(const NpcSystem&) = delete;
    NpcSystem& operator=(const NpcSystem&) = delete;

    // world/move 는 비소유(수명 > NpcSystem). move==nullptr 이면 등록만 가능(이동 no-op).
    void Initialize(ecs::World* world, MoveController* move);
    void Shutdown();

    // 상호작용 콜백 배선(Lua on_interact). begin==null 이면 상호작용이 항상 거부된다.
    //   busy==null 이면 상호작용은 즉시 종료된 것으로 간주(1틱 잠금).
    void SetInteractHandlers(InteractFn begin, InteractBusyFn busy);

    // 플레이어 엔티티 지정(접근/상호작용 거리 기준). Null 이면 접근 감지 비활성(항상 배회).
    void SetPlayer(ecs::Entity player);
    ecs::Entity Player() const;

    // ---- NPC 등록·해제 ----
    // NPC 를 등록(중복 id/entity 는 대체). 반환 false = 유효하지 않은 엔티티.
    bool Register(const NpcDesc& desc);
    void Unregister(ecs::Entity entity);
    void Clear();
    size_t Count() const;

    // ---- 상호작용 요청(플레이어 입력·트리거) ----
    // 플레이어와 가장 가까운(interactRadius 이내) NPC 의 상호작용을 개시한다. 개시 성공 시 그 NPC
    //   엔티티 반환, 없으면 Null. 이미 다른 NPC 와 상호작용 중이면 Null(중복 개시 거부).
    ecs::Entity TryInteractNearest();
    // 특정 NPC 상호작용 개시(트리거 등 명시 대상). 성공 true.
    bool        TryInteract(ecs::Entity npc);

    // 현재 상호작용 중인 NPC(없으면 Null). 상호작용 중이면 플레이어 이동을 막아야 한다(데모 참고).
    ecs::Entity InteractingNpc() const;
    bool        IsAnyInteracting() const { return !InteractingNpc().IsNull(); }

    // ---- 상태 질의(테스트·데모) ----
    NpcState State(ecs::Entity npc) const;
    Vec2     CurrentWaypoint(ecs::Entity npc) const;   // Moving 시 목표(없으면 {0,0})

    // ---- 시뮬 틱(고정틱 권장) ----
    // 각 NPC 의 상태기계를 전진시킨다: 대기 타이머·웨이포인트 선택·이동 명령·접근 감지·상호작용
    //   종료 폴링·8방향 애니 파라미터 갱신. move->Update 는 호출자(모듈)가 별도로 돌린다.
    void Tick(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
