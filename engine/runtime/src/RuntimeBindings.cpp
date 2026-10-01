#include "mye/runtime/RuntimeBindings.h"
#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/DialogueData.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/SaveSystem.h"
#include "mye/runtime/SceneTransition.h"
#include "mye/runtime/Localization.h"
#include "mye/script/LuaApi.h"
#include "mye/core/Log.h"

#include <stdexcept>
#include <limits>
#include <string>
#include <vector>

namespace mye::runtime {
using namespace script;
namespace {
int32_t Int32Argument(lua_State* L, int index) {
    const auto value = luaL_checkinteger(L, index);
    luaL_argcheck(L, value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max(), index, "integer is outside the signed 32-bit range");
    return static_cast<int32_t>(value);
}
std::string TextField(lua_State* L, int table, const char* field) {
    lua_getfield(L, table, field);
    std::string result = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
    lua_pop(L, 1); return result;
}
std::vector<DialogueChoice> ParseChoices(lua_State* L, int table) {
    luaL_checktype(L, table, LUA_TTABLE); table = lua_absindex(L, table);
    std::vector<DialogueChoice> choices;
    const size_t count = lua_rawlen(L, table); choices.reserve(count);
    for (size_t index = 1; index <= count; ++index) {
        lua_rawgeti(L, table, static_cast<lua_Integer>(index));
        DialogueChoice choice;
        if (lua_type(L, -1) == LUA_TSTRING) choice.text = LocalizedText::Literal(lua_tostring(L, -1));
        else if (lua_istable(L, -1)) {
            const int option = lua_gettop(L);
            const auto key = TextField(L, option, "key");
            choice.text = key.empty() ? LocalizedText::Literal(TextField(L, option, "text")) : LocalizedText::Key(key);
            choice.gotoId = TextField(L, option, "goto");
        }
        choices.push_back(std::move(choice)); lua_pop(L, 1);
    }
    return choices;
}
} // namespace
RuntimeBindings::RuntimeBindings(DialogueSystem* dialogue, CutsceneRuntime* cutscene,
                                 SaveSystem* save, SceneTransitionManager* sceneTransition,
                                 LocalizationSystem* loc)
    : m_dialogue(dialogue), m_cutscene(cutscene), m_save(save), m_sceneTransition(sceneTransition), m_loc(loc) {}
void RuntimeBindings::Register(lua_State* L) {
    LuaStackGuard stack(L);
    lua_getglobal(L, "mye"); const int mye = lua_gettop(L);
    EnsureTable(L, mye, "__rt"); const int rt = lua_gettop(L);
    auto say = [](lua_State* L) -> int {
        auto* dialogue = Context<RuntimeBindings>(L)->m_dialogue;
        const char* speaker = luaL_checkstring(L, 1); const char* body = luaL_checkstring(L, 2);
        if (!dialogue) { lua_pushboolean(L, false); return 1; }
        DialogueLine line; line.speaker = LocalizedText::Literal(speaker); line.body = LocalizedText::Literal(body);
        auto result = dialogue->Say(line);
        if (!result) MYE_LOG_WARN("RuntimeBindings", "dialogue.say: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    };
    PushFunction(L, say, this); lua_setfield(L, rt, "say_begin");
    PushFunction(L, [](lua_State* L) -> int {
        auto* dialogue = Context<RuntimeBindings>(L)->m_dialogue;
        const char* speaker = luaL_checkstring(L, 1); const char* body = luaL_checkstring(L, 2);
        if (!dialogue) { lua_pushboolean(L, false); return 1; }
        DialogueLine line; line.speaker = LocalizedText::Key(speaker); line.body = LocalizedText::Key(body);
        auto result = dialogue->Say(line);
        if (!result) MYE_LOG_WARN("RuntimeBindings", "dialogue.say_key: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    }, this); lua_setfield(L, rt, "say_key");
    PushFunction(L, [](lua_State* L) -> int {
        auto* dialogue = Context<RuntimeBindings>(L)->m_dialogue; auto choices = ParseChoices(L, 1);
        if (!dialogue) { lua_pushboolean(L, false); return 1; }
        auto result = dialogue->Choose(choices);
        if (!result) MYE_LOG_WARN("RuntimeBindings", "dialogue.choose: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    }, this); lua_setfield(L, rt, "choose_begin");
    PushFunction(L, [](lua_State* L) -> int { const auto* d = Context<RuntimeBindings>(L)->m_dialogue; lua_pushboolean(L, d && d->IsActive()); return 1; }, this); lua_setfield(L, rt, "dlg_is_active");
    PushFunction(L, [](lua_State* L) -> int { const auto* d = Context<RuntimeBindings>(L)->m_dialogue; lua_pushboolean(L, d && d->IsWaitingAdvance()); return 1; }, this); lua_setfield(L, rt, "dlg_waiting_advance");
    PushFunction(L, [](lua_State* L) -> int { const auto* d = Context<RuntimeBindings>(L)->m_dialogue; lua_pushboolean(L, d && d->IsWaitingChoice()); return 1; }, this); lua_setfield(L, rt, "dlg_waiting_choice");
    PushFunction(L, [](lua_State* L) -> int { const auto* d = Context<RuntimeBindings>(L)->m_dialogue; lua_pushinteger(L, d ? d->PickedChoice() : -1); return 1; }, this); lua_setfield(L, rt, "dlg_picked");
    EnsureTable(L, mye, "dialogue");
    PushFunction(L, [](lua_State* L) -> int { auto* d = Context<RuntimeBindings>(L)->m_dialogue; if (d) d->Advance(); return 0; }, this); lua_setfield(L, -2, "advance");
    PushFunction(L, [](lua_State* L) -> int { auto* d = Context<RuntimeBindings>(L)->m_dialogue; const auto index = Int32Argument(L, 1); if (d) d->Pick(index); return 0; }, this); lua_setfield(L, -2, "pick");
    lua_getfield(L, rt, "dlg_is_active"); lua_setfield(L, -2, "is_active");
    lua_getfield(L, rt, "dlg_picked"); lua_setfield(L, -2, "picked"); lua_pop(L, 1);
    PushFunction(L, [](lua_State* L) -> int {
        auto* c = Context<RuntimeBindings>(L)->m_cutscene; const auto entity = CheckEntity(L, 1);
        const Vec2 p{static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))};
        const float speed = static_cast<float>(luaL_optnumber(L, 4, 3));
        if (c && c->Move()) c->Move()->MoveTo(entity, p, speed); return 0;
    }, this); lua_setfield(L, rt, "move_begin");
    PushFunction(L, [](lua_State* L) -> int {
        const auto* c = Context<RuntimeBindings>(L)->m_cutscene; const auto entity = CheckEntity(L, 1);
        lua_pushboolean(L, !c || !c->Move() || c->Move()->IsMoveDone(entity)); return 1;
    }, this); lua_setfield(L, rt, "move_done");
    EnsureTable(L, mye, "cutscene"); lua_getfield(L, rt, "move_done"); lua_setfield(L, -2, "is_move_done"); lua_pop(L, 1);
    PushFunction(L, [](lua_State* L) -> int {
        auto* c = Context<RuntimeBindings>(L)->m_cutscene;
        const Vec2 p{static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2))};
        const float speed = static_cast<float>(luaL_optnumber(L, 3, 0));
        if (c && c->Camera()) c->Camera()->FocusWorld(p, speed); return 0;
    }, this); lua_setfield(L, rt, "focus_begin");
    PushFunction(L, [](lua_State* L) -> int { const auto* c = Context<RuntimeBindings>(L)->m_cutscene; lua_pushboolean(L, !c || !c->Camera() || c->Camera()->IsFocusDone()); return 1; }, this); lua_setfield(L, rt, "focus_done");
    EnsureTable(L, mye, "camera");
    lua_getfield(L, rt, "focus_begin"); lua_setfield(L, -2, "focus");
    lua_getfield(L, rt, "focus_done"); lua_setfield(L, -2, "is_focus_done");
    PushFunction(L, [](lua_State* L) -> int {
        auto* c = Context<RuntimeBindings>(L)->m_cutscene; const auto entity = CheckEntity(L, 1); const float speed = static_cast<float>(luaL_optnumber(L, 2, 5));
        if (c && c->Camera()) c->Camera()->FocusEntity(entity, speed); return 0;
    }, this); lua_setfield(L, -2, "follow");
    PushFunction(L, [](lua_State* L) -> int { auto* c = Context<RuntimeBindings>(L)->m_cutscene; if (c && c->Camera()) c->Camera()->ClearFollow(); return 0; }, this); lua_setfield(L, -2, "clear_follow"); lua_pop(L, 1);
    EnsureTable(L, mye, "save");
    PushFunction(L, [](lua_State* L) -> int { const auto* s = Context<RuntimeBindings>(L)->m_save; const auto slot = Int32Argument(L, 1); lua_pushboolean(L, s && s->Exists(SlotId{slot})); return 1; }, this); lua_setfield(L, -2, "exists");
    PushFunction(L, [](lua_State* L) -> int {
        auto* s = Context<RuntimeBindings>(L)->m_save; const auto slot = Int32Argument(L, 1);
        const char* title = luaL_optstring(L, 2, ""); const double playTime = luaL_optnumber(L, 3, 0);
        if (!s) { MYE_LOG_WARN("RuntimeBindings", "mye.save.write: no SaveSystem"); lua_pushboolean(L, false); return 1; }
        SaveHeader header; header.title = title; header.playTimeSec = playTime;
        auto result = s->WriteSlot(SlotId{slot}, header);
        if (!result) MYE_LOG_WARN("RuntimeBindings", "mye.save.write: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    }, this); lua_setfield(L, -2, "write");
    PushFunction(L, [](lua_State* L) -> int {
        auto* s = Context<RuntimeBindings>(L)->m_save; const auto slot = Int32Argument(L, 1);
        if (!s) { MYE_LOG_WARN("RuntimeBindings", "mye.save.read: no SaveSystem"); lua_pushboolean(L, false); return 1; }
        auto result = s->ReadSlot(SlotId{slot});
        if (!result) MYE_LOG_WARN("RuntimeBindings", "mye.save.read: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    }, this); lua_setfield(L, -2, "read"); lua_pop(L, 1);
    EnsureTable(L, mye, "loc");
    PushFunction(L, [](lua_State* L) -> int {
        auto* loc = Context<RuntimeBindings>(L)->m_loc; const char* key = luaL_checkstring(L, 1);
        if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
        std::vector<FormatArg> args;
        if (lua_istable(L, 2)) {
            lua_pushnil(L);
            while (lua_next(L, 2)) {
                if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) args.push_back({lua_tostring(L, -2), lua_tostring(L, -1)});
                lua_pop(L, 1);
            }
        }
        const std::string text = !loc ? key : args.empty() ? loc->Get(key) : loc->Format(key, args);
        lua_pushlstring(L, text.data(), text.size()); return 1;
    }, this); lua_setfield(L, -2, "text");
    PushFunction(L, [](lua_State* L) -> int { auto* loc = Context<RuntimeBindings>(L)->m_loc; const char* tag = luaL_checkstring(L, 1); if (loc) loc->SetLocale(LocaleFromTag(tag)); return 0; }, this); lua_setfield(L, -2, "set_locale");
    PushFunction(L, [](lua_State* L) -> int { const auto* loc = Context<RuntimeBindings>(L)->m_loc; const std::string tag = loc ? LocaleTag(loc->CurrentLocale()) : "ko"; lua_pushlstring(L, tag.data(), tag.size()); return 1; }, this); lua_setfield(L, -2, "locale"); lua_pop(L, 1);
    PushFunction(L, [](lua_State* L) -> int {
        auto* scene = Context<RuntimeBindings>(L)->m_sceneTransition; const char* path = luaL_checkstring(L, 1);
        if (!scene) { MYE_LOG_WARN("RuntimeBindings", "mye.scene.change: no SceneTransitionManager"); lua_pushboolean(L, false); return 1; }
        auto result = scene->ChangeScene(SceneRef{path}, TransitionDesc{});
        if (!result) MYE_LOG_WARN("RuntimeBindings", "mye.scene.change: {}", result.GetError().message);
        lua_pushboolean(L, static_cast<bool>(result)); return 1;
    }, this); lua_setfield(L, rt, "scene_begin");
    PushFunction(L, [](lua_State* L) -> int { const auto* scene = Context<RuntimeBindings>(L)->m_sceneTransition; lua_pushboolean(L, scene && scene->IsTransitioning()); return 1; }, this); lua_setfield(L, rt, "scene_transitioning");
    EnsureTable(L, mye, "scene");
    lua_getfield(L, rt, "scene_begin"); lua_setfield(L, -2, "change");
    lua_getfield(L, rt, "scene_transitioning"); lua_setfield(L, -2, "is_transitioning"); lua_pop(L, 1);
    // Yield remains in Lua; native callbacks only start work or poll its state.
    constexpr const char* wrappers = R"LUA(
        mye.co = mye.co or {}
        if not mye.co.yield then
            function mye.co.yield() return coroutine.yield() end
        end
        local rt = mye.__rt
        local co = mye.co

        -- say(speaker, body): 대화창 표시 후 진행 입력까지 yield.
        function mye.dialogue.say(speaker, body)
            if not rt.say_begin(speaker or "", body or "") then return false end
            while rt.dlg_waiting_advance() do co.yield() end
        end
        -- say_key(speakerKey, bodyKey): 로컬라이즈 키 버전.
        function mye.dialogue.say_key(speakerKey, bodyKey)
            if not rt.say_key(speakerKey or "", bodyKey or "") then return false end
            while rt.dlg_waiting_advance() do co.yield() end
        end
        -- choose(options): 선택지 표시 후 선택까지 yield, 선택 인덱스(0-base) 반환.
        function mye.dialogue.choose(options)
            if not rt.choose_begin(options) then return -1 end
            while rt.dlg_waiting_choice() do co.yield() end
            return rt.dlg_picked()
        end

        -- move_to(entity, x, y[, speed]): 도착까지 yield.
        function mye.cutscene.move_to(entity, x, y, speed)
            rt.move_begin(entity, x, y, speed)
            while not rt.move_done(entity) do co.yield() end
        end
        -- wait(sec): 05 wait_seconds 위임(있으면). 없으면 즉시.
        function mye.cutscene.wait(sec)
            if co.wait_seconds then return co.wait_seconds(sec) end
        end

        -- camera.focus_wait(x, y[, speed]): 포커스 완료까지 yield(비대기 focus 와 별도).
        function mye.camera.focus_wait(x, y, speed)
            rt.focus_begin(x, y, speed)
            while not rt.focus_done() do co.yield() end
        end

        -- scene.change_wait(vpath): 전환 완료까지 yield.
        function mye.scene.change_wait(vpath)
            if not rt.scene_begin(vpath) then return false end
            while rt.scene_transitioning() do co.yield() end
            return true
        end
    )LUA";
    if (luaL_loadstring(L, wrappers) != LUA_OK || ProtectedCall(L, 0, 0) != LUA_OK) throw std::runtime_error(lua_tostring(L, -1));
}
} // namespace mye::runtime
