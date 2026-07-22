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

#include <cstdint>
#include <memory>

namespace mye::ecs { class World; }

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

    // 편집 World를 배선(에디터 부팅 시 SceneModule::World()). Play/Stop 스냅샷 원본.
    void SetEditWorld(ecs::World* world) { m_editWorld = world; }
    void SetEventBus(EventBus* events) { m_events = events; }

    // 편집 World → 메모리 스냅샷 → Play World 생성 후 Playing. 이미 Playing이면 no-op.
    void Play();
    void Pause();      // Playing → Paused
    void Resume();     // Paused → Playing
    void StepFrame();  // Paused에서 1프레임 진행(F10)
    void Stop();       // Play World 파기 + Play Undo 스택 파기 → Edit 복귀

    PlayState State() const { return m_state; }
    bool IsPlaying() const { return m_state != PlayState::Edit; }

    // 패널들이 표시·편집 대상 World를 얻는 유일한 경로(07 §3).
    //   Edit=편집 World, Playing/Paused=Play World.
    ecs::World* ActiveWorld() const;

    // 플레이 중 편집용 별도 Undo 스택(Stop 시 파기). Edit 모드면 nullptr.
    CommandStack* PlayCommandStack() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ecs::World* m_editWorld = nullptr;   // 비소유(SceneModule 소유)
    EventBus*   m_events = nullptr;
    PlayState   m_state = PlayState::Edit;
};

} // namespace mye::editor
