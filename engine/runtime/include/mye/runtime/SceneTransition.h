// mye/runtime/SceneTransition.h — 씬 전환(페이드·로딩·비동기 로드) (docs/06 §7, M6-A)
//
// 소유: 06 (M6-A). changeScene(sceneRef, TransitionDesc): 페이드아웃 → 로딩 화면(UiDocument)
//   표시 → 04 비동기 로드(진행률 콜백을 로딩 UI 에 바인딩) → 액티베이트 → 페이드인.
//   전환 중 입력 컨텍스트는 Loading(전부 소비). 소규모 이동은 로딩 화면 없는 fast-path.
//
// 런타임 씬 로드: SceneSerializer 재사용(editor 소유) 또는 런타임 로더. M6-A 는 로더 배선점을
//   콜백(SceneLoaderFn)으로 추상화 — 실제 로드 구현은 구현 에이전트가 SceneSerializer/asset 로.
//
// 시뮬/표현 분리: 전환 상태기계(Phase)는 시뮬, 페이드 알파·로딩 UI 는 표현. 페이드 렌더는
//   구현 에이전트가 UiRenderer/SpriteBatch 로 배선(전체화면 쿼드). 여기선 상태·진행률 관리.
#pragma once

#include "mye/runtime/RuntimeTypes.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace mye { class EventBus; }
namespace mye::ui { class UiSystem; class UiCanvas; }

namespace mye::runtime {

// 전환 단계(상태기계).
enum class TransitionPhase : uint8_t {
    Idle,        // 전환 없음
    FadeOut,     // 화면 어두워지는 중
    Loading,     // 로딩 화면 표시 + 비동기 로드 진행
    Activating,  // 로드 완료 → 새 씬 활성화(1~수 프레임)
    FadeIn,      // 화면 밝아지는 중
};

// 씬 로드 요청 핸들(비동기 로더가 진행률·완료를 갱신). 구현 에이전트가 실제 로드에 매핑.
struct SceneLoadTicket {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
};

// 씬 로더 배선(구현 에이전트가 SceneSerializer/asset 로 채운다).
//   Begin: 로드 시작(티켓 반환). Poll: [0,1] 진행률, done=true 시 완료. Activate: 로드된 씬 활성화.
struct SceneLoaderFn {
    std::function<SceneLoadTicket(const SceneRef&)>            begin;
    std::function<float(SceneLoadTicket, bool& outDone)>      poll;    // 진행률, done 갱신
    std::function<Expected<void, Error>(SceneLoadTicket)>     activate;
    bool IsBound() const { return begin && poll && activate; }
};

// 전환 완료 통지(게임 로직이 구독 — 새 씬 진입 후 초기화).
struct SceneChangedEvent {
    MYE_EVENT(SceneChangedEvent);
    bool succeeded;
};

// ---------------------------------------------------------------------------
// SceneTransitionManager — 전환 상태기계 + 페이드/로딩/진행률.
//   EngineContext 서비스. Lua Scene.change(vpath, desc) 바인딩(05)의 백엔드.
// ---------------------------------------------------------------------------
class SceneTransitionManager {
public:
    MYE_SERVICE(mye::runtime::SceneTransitionManager);

    SceneTransitionManager();
    ~SceneTransitionManager();
    SceneTransitionManager(const SceneTransitionManager&) = delete;
    SceneTransitionManager& operator=(const SceneTransitionManager&) = delete;

    // ui/canvas 는 로딩 화면·페이드 오버레이 표시(비소유, nullptr 허용=페이드만 상태로 관리).
    //   loader 는 실제 씬 로드 배선. events 로 SceneChangedEvent 발행.
    void Initialize(ui::UiSystem* ui, ui::UiCanvas* overlayCanvas,
                    SceneLoaderFn loader, EventBus* events);
    void Shutdown();

    // 전환 시작. 이미 진행 중이면 Busy. fast-path(showLoadingScreen=false)는 로딩 UI 생략.
    Expected<void, Error> ChangeScene(const SceneRef& scene, const TransitionDesc& desc);

    // ---- 상태·진행 ----
    TransitionPhase Phase() const;
    bool  IsTransitioning() const;      // Idle 아님
    float FadeAlpha() const;            // 현재 페이드 알파[0,1] (표현 계층이 오버레이 렌더에 사용)
    float LoadProgress() const;         // [0,1] 로드 진행률(로딩 UI 바인딩)
    bool  ConsumesInput() const;        // 전환 중 입력 독점(Loading 컨텍스트 대체 판단)

    // 매 프레임 갱신 — 페이드 타이머·로드 폴링·단계 전이. dt=가변 프레임.
    void Update(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::runtime
