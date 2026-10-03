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
    Reset();
    m_canvas = m_ui.CreateCanvas("game");
    m_canvas->setRoot(std::move(instance.Value().root));
    m_textures = std::move(instance.Value().textures);
    m_ui.Update(0);
    return {};
}

void UiBindingModule::Reset() {
    m_callbacks.clear(); m_callbackOverflow = false;
    m_ui.RemoveCanvas("game"); m_canvas = nullptr; m_textures.clear();
}
Expected<bool, Error> UiBindingModule::FilterInput(InputState& input, Vec2 pointer, bool enabled) {
    if (!enabled) { m_ui.ResetInput(); m_callbacks.clear(); m_callbackOverflow = false; return false; }
    if (!Root()) return true;
    m_ui.Update(0);
    auto text=m_ui.HandleTextInput(input); if (!text) return text.GetError();
    const bool keyboardCaptured = m_ui.CapturesKeyboard();
    const bool pointerCaptured = m_ui.HandleInput(input, pointer);
    if (m_callbackOverflow) return Error{"Game UI exceeded 64 pending UI actions before a fixed tick", 1};
    input.SetMouseSuppressed(input.IsMouseSuppressed() || pointerCaptured);
    input.SetKeyboardSuppressed(input.IsKeyboardSuppressed() || keyboardCaptured || m_ui.CapturesKeyboard());
    return !m_ui.HasModal();
}
void UiBindingModule::Queue(std::string name, script::LuaReference callback, std::optional<std::string> submittedText) {
    if (m_callbacks.size() >= 64) { m_callbackOverflow=true; return; }
    m_callbacks.push_back({std::move(name),std::move(callback),std::move(submittedText)});
}
Expected<void, Error> UiBindingModule::ProcessCallbacks() {
    auto clicks = std::move(m_callbacks); m_callbacks.clear();
    for (auto& click : clicks) {
        auto* node = Root() ? Root()->findByName(click.name) : nullptr;
        auto* button = node ? node->As<ui::Button>() : nullptr;
        if (!node || !node->interactive || !click.callback.Valid()) continue;
        if (click.submittedText ? !node->As<ui::TextInput>() : (!button || button->state == ui::Button::State::Disabled)) continue;
        bool visible = true;
        for (auto* parent = node; parent; parent = parent->parent()) visible &= parent->visibility == ui::Visibility::Visible;
        if (!visible) continue;
        auto* state = click.callback.State();
        script::LuaStackGuard stack(state);
        click.callback.Push();
        if (click.submittedText) lua_pushlstring(state,click.submittedText->data(),click.submittedText->size());
        if (script::ProtectedCall(state, click.submittedText ? 1 : 0, 0) != LUA_OK) {
            const char* message = lua_tostring(state, -1);
            return Error{"UI " + click.name + ": " + (message ? message : "callback failed"), 1};
        }
    }
    return {};
}

void UiBindingModule::Register(lua_State* state) {
    script::LuaStackGuard stack(state);
    lua_getglobal(state, "mye");
    script::EnsureTable(state, -1, "ui");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 2);
        auto* button = node ? node->As<ui::Button>() : nullptr;
        if (!button || (lua_type(state,2) != LUA_TFUNCTION && lua_type(state,2) != LUA_TNIL))
            return Failure(state, "on_click requires a Button name and function or nil");
        auto* module = script::Context<UiBindingModule>(state);
        if (lua_type(state,2) == LUA_TNIL) button->onClick = {};
        else button->onClick = [module, name=button->name, callback=script::LuaReference(state,2)] {
            module->Queue(name,callback,{});
        };
        return Success(state);
    }, this); lua_setfield(state, -2, "on_click");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* module = script::Context<UiBindingModule>(state);
        if (lua_gettop(state) != 1) return Failure(state, "focus requires a Button name or nil");
        auto* node = lua_type(state,1) == LUA_TNIL ? nullptr : Find(state,1);
        if (lua_type(state,1) != LUA_TNIL && !node) return Failure(state, "focus requires an existing Button or TextInput name");
        if (!module->m_ui.Focus(node)) return Failure(state, "Widget cannot receive focus");
        if (node) node->keyboardFocused = true;
        return Success(state);
    }, this); lua_setfield(state, -2, "focus");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node=Find(state,2); auto* input=node ? node->As<ui::TextInput>() : nullptr;
        if (!input || (lua_type(state,2)!=LUA_TFUNCTION && lua_type(state,2)!=LUA_TNIL))
            return Failure(state,"on_submit requires a TextInput name and function or nil");
        auto* module=script::Context<UiBindingModule>(state);
        if (lua_type(state,2)==LUA_TNIL) input->onSubmit={};
        else input->onSubmit=[module,name=input->name,callback=script::LuaReference(state,2)](const std::string& value) {
            module->Queue(name,callback,value);
        };
        return Success(state);
    },this); lua_setfield(state,-2,"on_submit");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node=Find(state,1); auto* input=node ? node->As<ui::TextInput>() : nullptr;
        if (!input) return Failure(state,"get_text requires a TextInput name");
        lua_pushlstring(state,input->text.data(),input->text.size()); return 1;
    },this); lua_setfield(state,-2,"get_text");
    script::PushFunction(state, [](lua_State* state) -> int {
        auto* node = Find(state, 2);
        auto* label = node ? node->As<ui::Label>() : nullptr;
        auto* input = node ? node->As<ui::TextInput>() : nullptr;
        if ((!label && !input) || lua_type(state, 2) != LUA_TSTRING) return Failure(state, "set_text requires a Label or TextInput name and string");
        size_t length = 0;
        const auto* text = lua_tolstring(state, 2, &length);
        if (length > 4096 || std::string_view(text, length).find('\0') != std::string_view::npos)
            return Failure(state, "UI text must be at most 4096 UTF-8 bytes without NUL");
        if (input) {
            auto updated=input->SetText({text,length}); if (!updated) return Failure(state,updated.GetError().message);
            script::Context<UiBindingModule>(state)->m_ui.RefreshTextFocus(input);
        }
        else label->setText(text::EscapeRichText({text, length}));
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
