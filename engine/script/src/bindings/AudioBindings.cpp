#include "mye/script/bindings/EngineBindings.h"
#include "mye/script/LuaApi.h"
#include "mye/asset/AudioClip.h"
#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioTypes.h"

namespace mye::script {
void AudioBindingModule::Register(lua_State* L) {
    LuaStackGuard stack(L);
    lua_getglobal(L, "mye"); EnsureTable(L, -1, "audio");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<AudioBindingModule>(L); const char* name = luaL_checkstring(L, 1);
        const bool spatial = !lua_isnoneornil(L, 2) && !lua_isnoneornil(L, 3);
        const Vec2 position{static_cast<float>(luaL_optnumber(L, 2, 0)), static_cast<float>(luaL_optnumber(L, 3, 0))};
        if (self->m_engine && self->m_cueResolver) {
            if (const auto* cue = self->m_cueResolver(name)) self->m_engine->PostCue(*cue, spatial ? std::optional<Vec2>(position) : std::nullopt);
        }
        return 0;
    }, this); lua_setfield(L, -2, "play_cue");
    PushFunction(L, [](lua_State* L) -> int {
        auto* self = Context<AudioBindingModule>(L); const char* name = luaL_checkstring(L, 1);
        const float fade = static_cast<float>(luaL_optnumber(L, 2, 0.5));
        if (self->m_engine && self->m_clipResolver) {
            if (const auto* clip = self->m_clipResolver(name)) self->m_engine->Music().Play(*clip, fade);
        }
        return 0;
    }, this); lua_setfield(L, -2, "play_music");
    PushFunction(L, [](lua_State* L) -> int {
        auto* engine = Context<AudioBindingModule>(L)->m_engine;
        const float fade = static_cast<float>(luaL_optnumber(L, 1, 0));
        if (engine) engine->Music().Stop(fade); return 0;
    }, this); lua_setfield(L, -2, "stop_music");
    PushFunction(L, [](lua_State* L) -> int {
        auto* engine = Context<AudioBindingModule>(L)->m_engine;
        const auto bus = luaL_checkinteger(L, 1); const float value = static_cast<float>(luaL_checknumber(L, 2));
        if (engine && bus >= 0 && bus < static_cast<int>(audio::BusId::Count)) engine->SetBusVolume(static_cast<audio::BusId>(bus), value);
        return 0;
    }, this); lua_setfield(L, -2, "set_bus_volume");
    PushFunction(L, [](lua_State* L) -> int {
        auto* engine = Context<AudioBindingModule>(L)->m_engine; const auto bus = luaL_checkinteger(L, 1);
        lua_pushnumber(L, engine && bus >= 0 && bus < static_cast<int>(audio::BusId::Count) ? engine->GetBusVolume(static_cast<audio::BusId>(bus)) : 0); return 1;
    }, this); lua_setfield(L, -2, "get_bus_volume");
    PushFunction(L, [](lua_State* L) -> int {
        auto* engine = Context<AudioBindingModule>(L)->m_engine;
        const Vec2 p{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
        if (engine) engine->SetListener(p); return 0;
    }, this); lua_setfield(L, -2, "set_listener");
    lua_pop(L, 1); EnsureTable(L, -1, "Bus");
    lua_pushinteger(L, static_cast<int>(audio::BusId::Master)); lua_setfield(L, -2, "MASTER");
    lua_pushinteger(L, static_cast<int>(audio::BusId::BGM)); lua_setfield(L, -2, "BGM");
    lua_pushinteger(L, static_cast<int>(audio::BusId::SFX)); lua_setfield(L, -2, "SFX");
    lua_pushinteger(L, static_cast<int>(audio::BusId::UI)); lua_setfield(L, -2, "UI");
    lua_pushinteger(L, static_cast<int>(audio::BusId::Voice)); lua_setfield(L, -2, "VOICE");
}
} // namespace mye::script
