#include "UiBindings.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/FileSystem.h"
#include "mye/ecs/World.h"
#include "mye/script/LuaApi.h"
#include "mye/ui/UiDocument.h"
#include "mye/ui/Widgets.h"
#include "mye/text/RichText.h"
#include <cmath>

namespace mye::runtime {
namespace {
int Failure(lua_State* state, std::string_view message) {
    lua_pushnil(state);
    lua_pushlstring(state, message.data(), message.size());
    return 2;
}
ui::Widget* Find(lua_State* state, int arguments) {
    if (lua_gettop(state) != arguments || lua_type(state, 1) != LUA_TSTRING) return nullptr;
    size_t length = 0;
    const char* name = lua_tolstring(state, 1, &length);
    if (length == 0 || length > 64 || std::string_view(name, length).find('\0') != std::string_view::npos) return nullptr;
    auto* root = script::Context<UiBindingModule>(state)->Root();
    return root ? root->findByName({name, length}) : nullptr;
}
int Success(lua_State* state) { lua_pushboolean(state, true); return 1; }
} // namespace

Expected<void, Error> UiBindingModule::Load(ecs::World& world, asset::AssetDatabase* database,
    asset::VirtualFileSystem* files, asset::AssetManager* assets) {
    asset::AssetGuid guid;
    world.Query<GameUi>().Each([&](ecs::Entity, const GameUi& component) {
        if (component.enabled) guid = component.document.guid;
    });
    if (!guid.IsValid()) return {};
    if (!database || !files || !assets) return Error{"GameUi requires the project AssetDatabase, VFS and AssetManager", 1};
    const auto path = database->PathFromGuid(guid);
    if (!path.ends_with(".ui")) return Error{"GameUi " + guid.ToString() + ": document GUID does not resolve to a project .ui file", 1};
    auto bytes = files->ReadAll(path);
    if (!bytes) return Error{path + ": " + bytes.GetError().message, 1};
    auto document = ui::LoadDocumentJson({reinterpret_cast<const char*>(bytes.Value().data()), bytes.Value().size()});
    if (!document) return Error{path + ": " + document.GetError().message, 1};
    auto instance = ui::InstantiateGameUi(document.Value(), *database, *assets);
    if (!instance) return Error{path + ": " + instance.GetError().message, 1};
    m_root = std::move(instance.Value().root);
    m_textures = std::move(instance.Value().textures);
    return {};
}

void UiBindingModule::Register(lua_State* state) {
    script::LuaStackGuard stack(state);
    lua_getglobal(state, "mye");
    script::EnsureTable(state, -1, "ui");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 2);
        auto* label = node ? node->As<ui::Label>() : nullptr;
        if (!label || lua_type(state, 2) != LUA_TSTRING) return Failure(state, "set_text requires an existing Label name and string");
        size_t length = 0;
        const auto* text = lua_tolstring(state, 2, &length);
        if (length > 4096 || std::string_view(text, length).find('\0') != std::string_view::npos)
            return Failure(state, "UI text must be at most 4096 UTF-8 bytes without NUL");
        label->setText(text::EscapeRichText({text, length}));
        return Success(state);
    }, this); lua_setfield(state, -2, "set_text");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 3);
        auto* progress = node ? node->As<ui::ProgressBar>() : nullptr;
        if (!progress || lua_type(state, 2) != LUA_TNUMBER || lua_type(state, 3) != LUA_TNUMBER)
            return Failure(state, "set_progress requires a ProgressBar name, value and maximum");
        const auto value = lua_tonumber(state, 2), maximum = lua_tonumber(state, 3);
        if (!std::isfinite(value) || !std::isfinite(maximum) || value < 0 || maximum <= 0 || value > maximum || maximum > 32768)
            return Failure(state, "UI progress requires 0 <= value <= maximum <= 32768 and maximum > 0");
        auto updated = progress->SetValue(static_cast<float>(value), static_cast<float>(maximum));
        if (!updated) return Failure(state, updated.GetError().message);
        return Success(state);
    }, this); lua_setfield(state, -2, "set_progress");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 2);
        auto* button = node ? node->As<ui::Button>() : nullptr;
        if (!button || lua_type(state, 2) != LUA_TBOOLEAN) return Failure(state, "set_enabled requires a Button name and boolean");
        const bool enabled = lua_toboolean(state, 2) != 0;
        button->state = enabled ? ui::Button::State::Normal : ui::Button::State::Disabled;
        button->interactive = enabled;
        return Success(state);
    }, this); lua_setfield(state, -2, "set_enabled");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 2);
        if (!node || lua_type(state, 2) != LUA_TBOOLEAN) return Failure(state, "set_visible requires an existing widget name and boolean");
        node->visibility = lua_toboolean(state, 2) ? ui::Visibility::Visible : ui::Visibility::Hidden;
        return Success(state);
    }, this); lua_setfield(state, -2, "set_visible");
}
} // namespace mye::runtime
