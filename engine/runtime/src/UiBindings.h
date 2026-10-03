#pragma once

#include "mye/script/IBindingModule.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/Texture.h"
#include "mye/ui/Widget.h"
#include "mye/ui/UiSystem.h"
#include "mye/script/LuaApi.h"

namespace mye::asset { class AssetDatabase; class VirtualFileSystem; class AssetManager; }
namespace mye::ecs { class World; }

namespace mye::runtime {
// Owned by ObjectSystem, alive through Lua on_destroy and destroyed before AssetManager.
class UiBindingModule final : public script::IBindingModule {
public:
    Expected<void, Error> Load(ecs::World& world, asset::AssetDatabase* database,
        asset::VirtualFileSystem* files, asset::AssetManager* assets);
    std::string_view Name() const override { return "ui"; }
    void Register(lua_State* state) override;
    ui::Widget* Root() const { return m_canvas ? m_canvas->root() : nullptr; }
    Expected<bool, Error> FilterInput(InputState& input, Vec2 pointer, bool enabled);
    Expected<void, Error> ProcessClicks();
    bool BlocksGameplay() const { return m_ui.HasModal(); }
    void Reset();
private:
    std::vector<asset::AssetHandle<asset::Texture>> m_textures;
    ui::UiSystem m_ui;
    ui::UiCanvas* m_canvas = nullptr; // owned by m_ui
    struct Click { std::string name; script::LuaReference callback; };
    std::vector<Click> m_clicks;
    bool m_clickOverflow = false;
};
} // namespace mye::runtime
