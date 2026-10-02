#pragma once
#include "mye/core/InputActions.h"

namespace mye::runtime {
struct GameInput {
    Vec2 movement{};
    bool interact = false, jump = false;
    float cameraAxis = 0, cameraMouseX = 0, cameraZoomSteps = 0;
    bool exitGame = false;
    // Non-owning consumed snapshot. Use before its GameInputBuffer changes; runtime
    // exposes it to Lua only for this tick, never to online authority or teardown.
    const InputActions* actions = nullptr;
};
InputMap DefaultGameInputMap();
Expected<InputMap, Error> LoadGameInputMap(const json::Value* value);
std::string_view GameActionDescription(std::string_view name);

class GameInputBuffer {
public:
    Expected<void, Error> Configure(InputMap map);
    void Capture(const InputState& input, bool enabled);
    GameInput ConsumeTick();
    void Clear();
    const InputActions& Actions() const { return m_actions; }
private:
    InputActions m_actions;
    float m_mouseX = 0;
};
} // namespace mye::runtime
