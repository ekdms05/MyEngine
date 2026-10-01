// Optional dialogue, cutscene, camera, save, localization, and scene Lua APIs.
// Register attaches native service callbacks and yielding Lua wrappers to a VM.
// The service pointers are borrowed; the caller must keep them alive while the
// VM can invoke these callbacks. See docs/19-lua-api.md for the exact API.
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
    void Register(lua_State* lua) override;

private:
    DialogueSystem*         m_dialogue = nullptr;
    CutsceneRuntime*        m_cutscene = nullptr;
    SaveSystem*             m_save = nullptr;
    SceneTransitionManager* m_sceneTransition = nullptr;
    LocalizationSystem*     m_loc = nullptr;
};

} // namespace mye::runtime
