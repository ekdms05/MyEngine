// mye/editor/PlayMode.h — 에디트↔플레이 모드 컨트롤러 (docs/07 §3)
//
// 07 §3: Play 진입 시 편집 World를 인메모리 바이너리 스냅샷으로 직렬화(04 직렬화)하고,
//   역직렬화해 별도 Play World를 만든다. 편집 World는 보존. Stop 시 Play World 파기 + 편집
//   World 재표시(스냅샷 복원 아님). Play World에서만 스크립트·물리·AI가 tick. Pause/Step 지원.
//   플레이 중 Undo 스택은 별도로 쌓이고 Stop 시 통째로 버려진다.
//
// 상태 머신: Edit → (play) → Playing ⇄ Paused → (stop) → Edit.
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace mye::ecs { class World; }
namespace mye::runtime { struct GameInput; }
namespace mye::render { class Camera2D; }

namespace mye::editor {

enum class PlayState : std::uint8_t { Edit, Playing, Paused };

// 플레이 상태 변경 브로드캐스트(크롬 틴트·툴바·패널 갱신).
struct PlayStateChangedEvent {
    MYE_EVENT(PlayStateChangedEvent);
    PlayState previous = PlayState::Edit;
    PlayState current = PlayState::Edit;
};

class PlayModeController {
public:
    PlayModeController();
    ~PlayModeController();
    PlayModeController(const PlayModeController&) = delete;
    PlayModeController& operator=(const PlayModeController&) = delete;

    // 활성 문서의 편집 World를 배선. Play/Stop 스냅샷 원본.
    void SetEditWorld(ecs::World* world) { m_editWorld = world; }
    void SetEventBus(EventBus* events) { m_events = events; }
    // Resolve render/animation resources before Lua on_init, including candidate map worlds.
    void SetWorldPreparation(std::function<Expected<void, Error>(ecs::World&)> prepare) { m_prepareWorld = std::move(prepare); }

    // 편집 World → 메모리 스냅샷 → Play World 생성 후 Playing. 이미 Playing이면 no-op.
    Expected<void, Error> Play();
    void Pause();      // Playing → Paused
    void Resume();     // Paused → Playing
    void StepFrame();  // Paused에서 1프레임 진행(F10)
    bool ConsumeStepRequest(); // Fixed-update owner consumes one paused tick.
    void Stop();       // Play World 파기 + Play Undo 스택 파기 → Edit 복귀
    Expected<void, Error> Tick(float dt, Vec2 movement, bool interact, std::string_view projectRoot);
    Expected<void, Error> Tick(float dt, const runtime::GameInput& input, std::string_view projectRoot);
    std::string_view Message() const;
    std::string_view Prompt() const;
    float FadeAlpha() const;
    void SetInputEnabled(bool enabled) { m_inputEnabled = enabled; }
    bool InputEnabled() const { return m_inputEnabled; }

    PlayState State() const { return m_state; }
    bool IsPlaying() const { return m_state != PlayState::Edit; }

    // 패널들이 표시·편집 대상 World를 얻는 유일한 경로(07 §3).
    //   Edit=편집 World, Playing/Paused=Play World.
    ecs::World* ActiveWorld() const;
    const render::Camera2D& DefaultCamera() const;

    // 플레이 중 편집용 별도 Undo 스택(Stop 시 파기). Edit 모드면 nullptr.
    CommandStack* PlayCommandStack() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ecs::World* m_editWorld = nullptr;   // 비소유(Document 소유)
    EventBus*   m_events = nullptr;
    PlayState   m_state = PlayState::Edit;
    bool m_inputEnabled = false;
    std::function<Expected<void, Error>(ecs::World&)> m_prepareWorld;
};

} // namespace mye::editor
