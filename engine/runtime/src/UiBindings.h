#pragma once

#include "mye/script/IBindingModule.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/Texture.h"
#include "mye/ui/Widget.h"

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
    ui::Widget* Root() const { return m_root.get(); }
private:
    std::vector<asset::AssetHandle<asset::Texture>> m_textures;
    ui::WidgetPtr m_root;
};
} // namespace mye::runtime
