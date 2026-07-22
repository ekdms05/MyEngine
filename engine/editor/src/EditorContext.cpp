// EditorContext.cpp — activeWorld 위임 (M4-A 골격)
#include "mye/editor/EditorContext.h"
#include "mye/editor/PlayMode.h"

namespace mye::editor {

ecs::World* EditorContext::activeWorld() const {
    // 07 §3: PlayModeController가 표시 대상 World의 정본. 플레이모드가 없으면 null.
    return playMode ? playMode->ActiveWorld() : nullptr;
}

} // namespace mye::editor
