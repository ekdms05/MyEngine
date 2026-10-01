// Cutscene backends exposed by RuntimeBindings (docs/19-lua-api.md).
// Lua wrappers start a command and yield until its completion condition.
// MoveController updates LocalTransform XY along a straight line; navigation
// and collision avoidance are not connected. Camera presentation is supplied
// by an app callback. Service pointers are borrowed and must outlive bindings.
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
//   실제 카메라는 앱이 콜백으로 주입(Lua/render
//   헤더 전파 회피). SetTarget 후 Update 로 현재 포커스를 lerp, IsFocusDone 로 완료 판정.
// ---------------------------------------------------------------------------
class CameraFocusController {
public:
    // 카메라에 관심점 위치를 적용하는 싱크(앱이 Camera2D로 연결). 비면 무동작.
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
