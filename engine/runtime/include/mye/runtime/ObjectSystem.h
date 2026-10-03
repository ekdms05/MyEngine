#pragma once

#include "mye/runtime/ObjectComponents.h"
#include "mye/runtime/GameInput.h"
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"
#include "mye/render/Camera2D.h"
#include <memory>

namespace mye::asset { class AssetDatabase; class VirtualFileSystem; class AssetManager; }
namespace mye::ui { class Widget; }

namespace mye::runtime {
struct MapRequest { std::string scenePath, spawnName; };

// Compatibility for scenes without a saved camera: initialize at the controller,
// then follow the same fixed-tick deadzone in Play and MyGame.
void UpdateDefaultCamera2D(ecs::World& world, render::Camera2D& camera, bool reset = false);

// One instance per Play world. Destroy before its World and EventBus.
class ObjectSystem {
public:
    explicit ObjectSystem(ecs::World& world);
    ~ObjectSystem();
    Expected<void, Error> Initialize(asset::AssetDatabase* database = nullptr,
        asset::VirtualFileSystem* files = nullptr, asset::AssetManager* assets = nullptr);
    Expected<void, Error> Tick(float dt, const GameInput& input);
    Expected<void, Error> Tick(float dt, Vec2 movement, bool interact) { return Tick(dt, GameInput{movement,interact}); }
    MapRequest TakeMapRequest();
    std::string_view Message() const;
    std::string_view Prompt() const;
    ui::Widget* UiRoot() const; // Borrow only until this ObjectSystem/Play world is destroyed.
    std::optional<TextInputFocus> TextFocus() const;
    Expected<bool, Error> FilterUiInput(InputState& input, Vec2 pointer, bool enabled);
    void Dispatch(ecs::Entity object, ObjectEvent event);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace mye::runtime
