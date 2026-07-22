// mye/runtime/RuntimeBindings.cpp — 런타임 Lua 바인딩 (docs/05·06, M6-A)
//
// `mye` 하위에 dialogue/cutscene/camera/save/loc/scene 표면을 등록한다. 대기형 프리미티브
//   (say/choose/move_to/camera focus/wait)는 05 코루틴과 결합해 coroutine.yield 루프로 감싼다:
//   컷신을 Lua 코루틴으로 절차적으로 기술할 수 있다.
//
// 코루틴 결합 방식(05 계약): C-call 경계를 넘어 yield 할 수 없으므로, C++ 는 "명령 시작 함수"와
//   "완료 폴링 함수"만 노출하고, 실제 yield 루프는 순수 Lua 래퍼(mye.co.yield 사용)로 정의한다.
//   래퍼: 명령 시작 → while not done() do co.yield() end → 결과 반환. 스케줄러가 매 tick 재개.
#include "mye/runtime/RuntimeBindings.h"

#include "mye/runtime/DialogueSystem.h"
#include "mye/runtime/DialogueData.h"
#include "mye/runtime/CutsceneRuntime.h"
#include "mye/runtime/SaveSystem.h"
#include "mye/runtime/SceneTransition.h"
#include "mye/runtime/Localization.h"

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace mye::runtime {

RuntimeBindings::RuntimeBindings(DialogueSystem* dialogue, CutsceneRuntime* cutscene,
                                 SaveSystem* save, SceneTransitionManager* sceneTransition,
                                 LocalizationSystem* loc)
    : m_dialogue(dialogue), m_cutscene(cutscene), m_save(save),
      m_sceneTransition(sceneTransition), m_loc(loc) {}

namespace {

// Lua table(문자열 배열 또는 {text=..,goto=..} 테이블 배열)을 DialogueChoice 벡터로.
std::vector<DialogueChoice> ParseChoices(const sol::table& options) {
    std::vector<DialogueChoice> choices;
    for (auto& kv : options) {
        DialogueChoice c;
        if (kv.second.is<std::string>()) {
            c.text = LocalizedText::Literal(kv.second.as<std::string>());
        } else if (kv.second.is<sol::table>()) {
            sol::table t = kv.second.as<sol::table>();
            sol::optional<std::string> txt = t["text"];
            sol::optional<std::string> key = t["key"];
            sol::optional<std::string> g   = t["goto"];
            if (key) c.text = LocalizedText::Key(*key);
            else     c.text = LocalizedText::Literal(txt.value_or(std::string{}));
            if (g) c.gotoId = *g;
        }
        choices.push_back(std::move(c));
    }
    return choices;
}

} // namespace

void RuntimeBindings::Register(sol::state& lua) {
    sol::table mye = lua["mye"].get_or_create<sol::table>();

    // ---- 저수준 C++ 명령/폴링 표면(내부용, __rt) — Lua 코루틴 래퍼가 소비 ----
    sol::table rt = mye["__rt"].get_or_create<sol::table>();

    // ---- mye.dialogue : 상태 질의 + 저수준 명령 ----
    {
        sol::table dlg = mye["dialogue"].get_or_create<sol::table>();
        DialogueSystem* d = m_dialogue;

        // 저수준(즉시반환) — 래퍼가 감싼다.
        rt.set_function("say_begin", [d](const std::string& speaker, const std::string& body) {
            if (!d) return;
            DialogueLine line;
            line.speaker = LocalizedText::Literal(speaker);
            line.body = LocalizedText::Literal(body);
            (void)d->Say(line);
        });
        rt.set_function("say_key", [d](const std::string& speakerKey, const std::string& bodyKey) {
            if (!d) return;
            DialogueLine line;
            line.speaker = LocalizedText::Key(speakerKey);
            line.body = LocalizedText::Key(bodyKey);
            (void)d->Say(line);
        });
        rt.set_function("choose_begin", [d](sol::table options) {
            if (!d) return;
            (void)d->Choose(ParseChoices(options));
        });
        rt.set_function("dlg_is_active", [d]() { return d && d->IsActive(); });
        rt.set_function("dlg_waiting_advance", [d]() { return d && d->IsWaitingAdvance(); });
        rt.set_function("dlg_waiting_choice", [d]() { return d && d->IsWaitingChoice(); });
        rt.set_function("dlg_picked", [d]() { return d ? d->PickedChoice() : -1; });

        // 공개 즉시 API(비코루틴 경로 — 게임 로직/UI 콜백용).
        dlg.set_function("advance", [d]() { if (d) d->Advance(); });
        dlg.set_function("pick", [d](int32_t index) { if (d) d->Pick(index); });
        dlg.set_function("is_active", [d]() { return d && d->IsActive(); });
        dlg.set_function("picked", [d]() { return d ? d->PickedChoice() : -1; });
    }

    // ---- mye.cutscene : move_to / is_move_done (저수준 명령) ----
    {
        sol::table cs = mye["cutscene"].get_or_create<sol::table>();
        CutsceneRuntime* c = m_cutscene;
        rt.set_function("move_begin",
            [c](double entityPacked, float x, float y, sol::optional<float> speed) {
                if (!c || !c->Move()) return;
                ecs::Entity e = ecs::Entity::FromPacked(static_cast<uint64_t>(entityPacked));
                c->Move()->MoveTo(e, Vec2{x, y}, speed.value_or(3.0f));
            });
        rt.set_function("move_done", [c](double entityPacked) {
            if (!c || !c->Move()) return true;
            ecs::Entity e = ecs::Entity::FromPacked(static_cast<uint64_t>(entityPacked));
            return c->Move()->IsMoveDone(e);
        });
        cs.set_function("is_move_done", [c](double entityPacked) {
            if (!c || !c->Move()) return true;
            ecs::Entity e = ecs::Entity::FromPacked(static_cast<uint64_t>(entityPacked));
            return c->Move()->IsMoveDone(e);
        });
    }

    // ---- mye.camera : focus / follow / is_focus_done ----
    {
        sol::table cam = mye["camera"].get_or_create<sol::table>();
        CutsceneRuntime* c = m_cutscene;
        rt.set_function("focus_begin", [c](float x, float y, sol::optional<float> speed) {
            if (c && c->Camera()) c->Camera()->FocusWorld(Vec2{x, y}, speed.value_or(0.0f));
        });
        rt.set_function("focus_done", [c]() {
            return !c || !c->Camera() || c->Camera()->IsFocusDone();
        });
        cam.set_function("focus", [c](float x, float y, sol::optional<float> speed) {
            if (c && c->Camera()) c->Camera()->FocusWorld(Vec2{x, y}, speed.value_or(0.0f));
        });
        cam.set_function("follow", [c](double entityPacked, sol::optional<float> speed) {
            if (!c || !c->Camera()) return;
            ecs::Entity e = ecs::Entity::FromPacked(static_cast<uint64_t>(entityPacked));
            c->Camera()->FocusEntity(e, speed.value_or(5.0f));
        });
        cam.set_function("clear_follow", [c]() { if (c && c->Camera()) c->Camera()->ClearFollow(); });
        cam.set_function("is_focus_done", [c]() {
            return !c || !c->Camera() || c->Camera()->IsFocusDone();
        });
    }

    // ---- mye.save : write / read / exists ----
    {
        sol::table sv = mye["save"].get_or_create<sol::table>();
        SaveSystem* s = m_save;
        sv.set_function("exists", [s](int32_t slot) {
            return s && s->Exists(SlotId{slot});
        });
        sv.set_function("write", [s](int32_t slot) {
            if (!s) return false;
            SaveHeader h;
            return static_cast<bool>(s->WriteSlot(SlotId{slot}, h));
        });
        sv.set_function("read", [s](int32_t slot) {
            return s && static_cast<bool>(s->ReadSlot(SlotId{slot}));
        });
    }

    // ---- mye.loc : text / set_locale / locale ----
    {
        sol::table lc = mye["loc"].get_or_create<sol::table>();
        LocalizationSystem* l = m_loc;
        lc.set_function("text", [l](const std::string& key, sol::optional<sol::table> args) {
            if (!l) return key;
            if (!args) return l->Get(key);
            std::vector<FormatArg> fa;
            for (auto& kv : *args) {
                FormatArg a;
                a.key = kv.first.as<std::string>();
                a.value = kv.second.as<std::string>();
                fa.push_back(std::move(a));
            }
            return l->Format(key, fa);
        });
        lc.set_function("set_locale", [l](const std::string& tag) {
            if (l) l->SetLocale(LocaleFromTag(tag));
        });
        lc.set_function("locale", [l]() -> std::string {
            return l ? LocaleTag(l->CurrentLocale()) : "ko";
        });
    }

    // ---- mye.scene : change / is_transitioning ----
    {
        sol::table sc = mye["scene"].get_or_create<sol::table>();
        SceneTransitionManager* st = m_sceneTransition;
        rt.set_function("scene_begin", [st](const std::string& vpath) {
            if (!st) return false;
            TransitionDesc desc;
            return static_cast<bool>(st->ChangeScene(SceneRef{vpath}, desc));
        });
        rt.set_function("scene_transitioning", [st]() {
            return st && st->IsTransitioning();
        });
        sc.set_function("change", [st](const std::string& vpath) {
            if (!st) return false;
            TransitionDesc desc;
            return static_cast<bool>(st->ChangeScene(SceneRef{vpath}, desc));
        });
        sc.set_function("is_transitioning", [st]() {
            return st && st->IsTransitioning();
        });
    }

    // ---- 코루틴 래퍼(순수 Lua) — C-call 경계 넘는 yield 회피(05 계약) ----
    //   mye.co.yield() 로 무대기 양보하고 매 tick C++ 폴링을 검사한다. mye.co 는 CoroutineScheduler
    //   가 이미 등록(ScriptRuntime). 없으면(테스트 등) 폴백 co 를 만들어 최소 동작 보장.
    lua.safe_script(R"LUA(
        mye.co = mye.co or {}
        if not mye.co.yield then
            function mye.co.yield() return coroutine.yield() end
        end
        local rt = mye.__rt
        local co = mye.co

        -- say(speaker, body): 대화창 표시 후 진행 입력까지 yield.
        function mye.dialogue.say(speaker, body)
            rt.say_begin(speaker or "", body or "")
            while rt.dlg_waiting_advance() do co.yield() end
        end
        -- say_key(speakerKey, bodyKey): 로컬라이즈 키 버전.
        function mye.dialogue.say_key(speakerKey, bodyKey)
            rt.say_key(speakerKey or "", bodyKey or "")
            while rt.dlg_waiting_advance() do co.yield() end
        end
        -- choose(options): 선택지 표시 후 선택까지 yield, 선택 인덱스(0-base) 반환.
        function mye.dialogue.choose(options)
            rt.choose_begin(options)
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
    )LUA", "mye.runtime.coroutine_wrappers");
}

} // namespace mye::runtime
