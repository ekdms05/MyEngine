#pragma once

#include "mye/runtime/ObjectComponents.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"
#include <memory>

namespace mye::runtime {
struct MapRequest { std::string scenePath, spawnName; };
struct GameInput {
    Vec2 movement{};
    bool interact = false, jump = false;
    float cameraAxis = 0, cameraMouseX = 0;
};

// One instance per Play world. Destroy before its World and EventBus.
class ObjectSystem {
public:
    explicit ObjectSystem(ecs::World& world);
    ~ObjectSystem();
    Expected<void, Error> Initialize();
    Expected<void, Error> Tick(float dt, const GameInput& input);
    Expected<void, Error> Tick(float dt, Vec2 movement, bool interact) { return Tick(dt, GameInput{movement,interact}); }
    MapRequest TakeMapRequest();
    std::string_view Message() const;
    std::string_view Prompt() const;
    void Dispatch(ecs::Entity object, ObjectEvent event);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace mye::runtime
