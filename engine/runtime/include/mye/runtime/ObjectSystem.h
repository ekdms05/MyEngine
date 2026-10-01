#pragma once

#include "mye/runtime/ObjectComponents.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"
#include <memory>

namespace mye::runtime {
struct MapRequest { std::string scenePath, spawnName; };

// One instance per Play world. Destroy before its World and EventBus.
class ObjectSystem {
public:
    explicit ObjectSystem(ecs::World& world);
    ~ObjectSystem();
    Expected<void, Error> Initialize();
    void Tick(float dt, Vec2 movement, bool interact);
    MapRequest TakeMapRequest();
    std::string_view Message() const;
    std::string_view Prompt() const;
    void Dispatch(ecs::Entity object, ObjectEvent event);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace mye::runtime
