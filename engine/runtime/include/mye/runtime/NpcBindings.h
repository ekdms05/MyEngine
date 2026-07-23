// mye/runtime/NpcBindings.h — NPC 시스템 Lua 바인딩 모듈 (docs/05 바인딩 + docs/06 §7, M6-B)
//
// `mye.npc` 표면을 ScriptRuntime 에 심어 NPC 배회·상호작용을 Lua 로 기술한다:
//     mye.npc.register{ entity=, id=, waypoints={{x,y},..}, mode="patrol"|"random"|"none",
//                       speed=, wait_min=, wait_max=, alert_radius=, interact_radius=,
//                       on_interact=function(npc) ... end }
//     mye.npc.set_player(entity)
//     mye.npc.interact()            -- 가장 가까운 NPC 상호작용 시도(플레이어 입력 배선)
//     mye.npc.interact_with(entity) -- 특정 NPC 상호작용
//     mye.npc.is_interacting()      -- 대화 중 여부(플레이어 이동 잠금 판단)
//     mye.npc.unregister(entity) / mye.npc.clear() / mye.npc.count()
//
// 상호작용 개시 모델(05 코루틴 결합): register 의 on_interact 는 Lua 함수다. C++ NpcSystem 이
//   상호작용을 개시하면, 이 바인딩이 on_interact 를 mye.co.start 로 코루틴 실행한다 — on_interact
//   안에서 mye.dialogue.say/choose(대기형)를 자유롭게 쓸 수 있다. 상호작용 종료(busy=false) 판정은
//   그 코루틴이 아직 살아있는지로 한다(NpcSystem 이 InteractBusyFn 으로 폴링).
//
// 에러 격리(05): on_interact 코루틴이 던져도 CoroutineScheduler 가 격리한다(다른 NPC 무영향).
//   VM 재생성 시 Register 가 재호출되므로 Lua 측 등록 테이블도 재구축한다(IBindingModule 계약).
//
// 이 헤더는 sol.hpp 를 끌어오지 않는다(IBindingModule 은 sol/forward.hpp). 구현 .cpp 가 sol.hpp 포함.
#pragma once

#include "mye/script/IBindingModule.h"

#include <string_view>

namespace mye::script { class ScriptRuntime; }

namespace mye::runtime {

class NpcSystem;

// NpcSystem 을 Lua 로 노출하는 바인딩 모듈. NpcSystem·ScriptRuntime 은 비소유(수명 > VM).
//   ScriptRuntime 은 on_interact 코루틴 시작(mye.co.start)에 쓴다.
class NpcBindings final : public script::IBindingModule {
public:
    NpcBindings(NpcSystem* npc, script::ScriptRuntime* runtime);

    std::string_view Name() const override { return "npc"; }
    void Register(sol::state& lua) override;

private:
    NpcSystem*             m_npc = nullptr;
    script::ScriptRuntime* m_runtime = nullptr;
};

} // namespace mye::runtime
