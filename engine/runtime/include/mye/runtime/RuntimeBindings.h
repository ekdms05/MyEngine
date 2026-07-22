// mye/runtime/RuntimeBindings.h — 런타임 Lua 바인딩 모듈 (docs/05 바인딩 + docs/06 §7, M6-A)
//
// docs/06 §5 경계: "Ui/Audio/Save/Loc 의 Lua API 표면 정의는 06, 바인딩 구현 방식은 05."
//   이 모듈은 IBindingModule 로 ScriptRuntime 에 등록되어 `mye` 전역 하위에 런타임 표면을 심는다:
//     mye.dialogue.say(line) / mye.dialogue.choose(options)   (컷신 코루틴 — yield 대기)
//     mye.cutscene.move_to(entity, x, y[, speed]) / mye.cutscene.wait(sec)
//     mye.camera.focus(x, y[, speed]) / mye.camera.follow(entity[, speed])
//     mye.save.write(slot) / mye.save.read(slot) / mye.save.exists(slot)
//     mye.loc.text(key[, args]) / mye.loc.set_locale(tag)
//     mye.scene.change(vpath[, desc])
//
// 계약(docs/05 IBindingModule): Register 는 VM (재)생성 시마다 재호출 — sol 객체를 멤버로 저장하지
//   않는다. 함수는 snake_case, 타입은 PascalCase. 대기형(say/choose/move_to)은 05 코루틴
//   프리미티브(coroutine.yield + CoroutineScheduler 재개 조건)와 결합한다(구현 에이전트가 배선).
//
// 이 헤더는 sol.hpp 를 끌어오지 않는다(IBindingModule 은 sol/forward.hpp). 구현 .cpp 가 sol.hpp 포함.
#pragma once

#include "mye/script/IBindingModule.h"

#include <string_view>

namespace mye::runtime {

class DialogueSystem;
class CutsceneRuntime;
class SaveSystem;
class SceneTransitionManager;
class LocalizationSystem;

// 런타임 서브시스템을 Lua 로 노출하는 바인딩 모듈. 서브시스템은 전부 비소유(수명 > VM).
//   RuntimeModule 이 OnPostInitialize 에서 생성·등록한다(ScriptRuntime::AddBindingModule).
class RuntimeBindings final : public script::IBindingModule {
public:
    RuntimeBindings(DialogueSystem* dialogue, CutsceneRuntime* cutscene,
                    SaveSystem* save, SceneTransitionManager* sceneTransition,
                    LocalizationSystem* loc);

    std::string_view Name() const override { return "runtime"; }
    void Register(sol::state& lua) override;

private:
    DialogueSystem*         m_dialogue = nullptr;
    CutsceneRuntime*        m_cutscene = nullptr;
    SaveSystem*             m_save = nullptr;
    SceneTransitionManager* m_sceneTransition = nullptr;
    LocalizationSystem*     m_loc = nullptr;
};

} // namespace mye::runtime
