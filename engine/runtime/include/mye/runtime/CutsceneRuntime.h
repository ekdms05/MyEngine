// mye/runtime/CutsceneRuntime.h — 컷신 코루틴 프리미티브 백엔드 (docs/05 코루틴 + docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). docs/05 코루틴 프리미티브(wait_seconds/wait_event) 위에 컷신 연출 프리미티브를
//   쌓는다. Lua 스크립트가 컷신을 코루틴으로 절차적으로 기술한다:
//     say(text)          → 대화창 표시하고 진행 입력까지 대기
//     choose(options)    → 선택지 표시하고 선택까지 대기, 선택 인덱스 반환
//     move_to(entity,pos)→ nav/이동을 시작하고 도착까지 대기
//     camera.focus(target)→ 카메라 포커스 이동(완료까지 대기 옵션)
//     wait(sec)          → 05 wait_seconds 위임
//
// 배선 모델(sol 경계 격리): 이 헤더는 sol.hpp 를 끌어오지 않는다. Lua 바인딩(05 에이전트)이
//   `mye.cutscene.*` 를 등록할 때, 각 프리미티브를 "명령 시작 + 완료 조건 폴링(yield 루프)"으로
//   구현한다. CutsceneRuntime 은 그 완료 조건 판정과 명령 시작을 담당하는 C++ 백엔드다.
//   완료 폴링은 CoroutineScheduler 의 wait_event/조건 재개와 결합한다(구현 에이전트 배선).
//
// 이동 완료 판정: MoveController(아래) 가 대상 엔티티를 목표로 이동시키며(nav 경로 추종),
//   IsMoveDone(entity) 로 코루틴 재개 조건을 제공한다. 카메라 포커스는 HybridRenderer/Camera2D
//   소비(비소유 참조) — 구현 에이전트가 실제 카메라 API 로 배선한다.
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/core/Base.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::nav   { class NavSystem; }
namespace mye::ecs   { class World; }

namespace mye::runtime {

class DialogueSystem;

// 컷신 이동 명령 상태(코루틴 대기 조건).
enum class MoveStatus : uint8_t {
    Idle,       // 명령 없음
    Moving,     // 경로 추종 중
    Arrived,    // 목표 도착(완료)
    Failed,     // 경로 없음·취소
};

// ---------------------------------------------------------------------------
// MoveController — move_to(entity, pos) 백엔드. 대상 엔티티를 목표 월드좌표로 이동.
//   nav 로 경로를 요청하고(있으면) 프레임마다 Transform 을 목표로 전진시킨다. nav==nullptr 이면
//   직선 이동(간이). 시뮬레이션 계층(고정틱에서 상태 변경) 규약 준수 — Update 는 시뮬 틱에서.
// ---------------------------------------------------------------------------
class MoveController {
public:
    MoveController();
    ~MoveController();
    MoveController(const MoveController&) = delete;
    MoveController& operator=(const MoveController&) = delete;

    // world 는 Transform 접근(비소유). nav 는 경로 계산(nullptr=직선 이동). 수명 > MoveController.
    void Initialize(ecs::World* world, nav::NavSystem* nav);
    void Shutdown();

    // 이동 시작. speed(월드유닛/초). 기존 명령이 있으면 대체.
    void MoveTo(ecs::Entity entity, Vec2 targetWorld, float speed);
    void Cancel(ecs::Entity entity);

    MoveStatus Status(ecs::Entity entity) const;
    bool       IsMoveDone(ecs::Entity entity) const;   // Arrived || Failed || Idle

    // 시뮬레이션 틱 — 활성 이동을 전진시킨다(고정틱 권장, docs/06 §8).
    void Update(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---------------------------------------------------------------------------
// CameraFocusController — camera.focus(target) 백엔드. 카메라 관심점을 목표로 부드럽게 이동.
//   실제 카메라(render::Camera2D/HybridRenderer)는 구현 에이전트가 콜백으로 주입(sol/render
//   헤더 전파 회피). SetTarget 후 Update 로 현재 포커스를 lerp, IsFocusDone 로 완료 판정.
// ---------------------------------------------------------------------------
class CameraFocusController {
public:
    // 카메라에 관심점 위치를 적용하는 싱크(구현 에이전트가 Camera2D 로 배선). 비면 무동작.
    using ApplyFocusFn = std::function<void(Vec2 worldPos)>;

    CameraFocusController();
    ~CameraFocusController();

    void Initialize(ApplyFocusFn apply, Vec2 initialFocus);

    // 목표 포커스 설정. lerpSpeed(단위/초, 0=즉시). 엔티티 추종은 targetEntity 로(옵션).
    void FocusWorld(Vec2 worldPos, float lerpSpeed);
    void FocusEntity(ecs::Entity target, float lerpSpeed);
    void ClearFollow();

    Vec2 CurrentFocus() const;
    bool IsFocusDone() const;   // 목표에 도달(임계 이내)

    // world 로 엔티티 추종 위치 조회(FocusEntity 시). 표현 계층(가변 프레임)에서 호출.
    void Update(float dt, const ecs::World* world);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---------------------------------------------------------------------------
// CutsceneRuntime — 컷신 프리미티브의 C++ 백엔드 허브.
//   Lua 바인딩(05)이 `mye.cutscene` 을 등록할 때 이 허브를 통해 명령을 시작하고 완료를 폴링한다.
//   say/choose 는 DialogueSystem 위임, move_to 는 MoveController, camera.focus 는
//   CameraFocusController 위임. wait 은 05 CoroutineScheduler wait_seconds 그대로 쓴다.
// ---------------------------------------------------------------------------
class CutsceneRuntime {
public:
    MYE_SERVICE(mye::runtime::CutsceneRuntime);

    CutsceneRuntime();
    ~CutsceneRuntime();
    CutsceneRuntime(const CutsceneRuntime&) = delete;
    CutsceneRuntime& operator=(const CutsceneRuntime&) = delete;

    // 서브시스템 배선(전부 비소유, 수명 > CutsceneRuntime). 부분 nullptr 허용(해당 프리미티브 비활성).
    void Initialize(DialogueSystem* dialogue, MoveController* move,
                    CameraFocusController* camera);
    void Shutdown();

    DialogueSystem*        Dialogue() const;
    MoveController*        Move() const;
    CameraFocusController* Camera() const;

    // 표현/시뮬 갱신 위임(모듈 틱에서 호출). dtSim: 이동(고정틱), dtVar: 카메라(가변).
    void UpdateSim(float dtSim);
    void UpdateView(float dtVar, const ecs::World* world);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
