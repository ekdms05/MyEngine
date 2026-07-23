// mye/runtime/NpcBindings.cpp — NPC 시스템 Lua 바인딩 (docs/05·06, M6-B)
//
// `mye.npc` 하위에 register/set_player/interact/is_interacting 등을 등록한다. 상호작용 개시는
//   NpcSystem(C++) 이 InteractFn 콜백으로 Lua 디스패처를 호출하는 구조:
//     - register 는 on_interact 함수를 lua 측 테이블(mye.npc.__handlers[id]) 에 보관한다.
//     - NpcSystem 이 상호작용을 수락하면 InteractFn → Lua __begin(id) 호출 → on_interact 를
//       mye.co.start 로 코루틴 실행(내부 pcall 로 감싸 busy 플래그를 반드시 정리).
//     - InteractBusyFn → Lua __busy(id) 로 busy 플래그를 폴링. 코루틴이 끝나면 false → NpcSystem
//       이 배회를 재개한다.
//   on_interact 안에서는 mye.dialogue.say/choose(대기형) 를 자유롭게 쓸 수 있다(코루틴 컨텍스트).
#include "mye/runtime/NpcBindings.h"

#include "mye/runtime/NpcSystem.h"

#include "mye/script/ScriptRuntime.h"

#include "mye/ecs/Entity.h"

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace mye::runtime {

NpcBindings::NpcBindings(NpcSystem* npc, script::ScriptRuntime* runtime)
    : m_npc(npc), m_runtime(runtime) {}

namespace {

// Lua 테이블에서 옵션 float 읽기(없으면 def).
float GetF(const sol::table& t, const char* key, float def) {
    sol::optional<float> v = t[key];
    return v ? *v : def;
}

// waypoints 테이블({{x,y},{x,y},..} 또는 {x=,y=} 배열) → Vec2 벡터.
std::vector<Vec2> ParseWaypoints(const sol::object& obj) {
    std::vector<Vec2> out;
    if (!obj.is<sol::table>()) return out;
    sol::table arr = obj.as<sol::table>();
    for (auto& kv : arr) {
        if (!kv.second.is<sol::table>()) continue;
        sol::table p = kv.second.as<sol::table>();
        // {x=,y=} 우선, 없으면 [1],[2].
        sol::optional<float> x = p["x"];
        sol::optional<float> y = p["y"];
        if (!x) x = p[1];
        if (!y) y = p[2];
        out.push_back(Vec2{ x.value_or(0.0f), y.value_or(0.0f) });
    }
    return out;
}

WanderMode ParseMode(const sol::table& t) {
    sol::optional<std::string> m = t["mode"];
    if (!m) return WanderMode::Patrol;
    if (*m == "random") return WanderMode::Random;
    if (*m == "none")   return WanderMode::None;
    return WanderMode::Patrol;
}

} // namespace

void NpcBindings::Register(sol::state& lua) {
    sol::table mye = lua["mye"].get_or_create<sol::table>();
    sol::table npc = mye["npc"].get_or_create<sol::table>();

    NpcSystem* sys = m_npc;

    // on_interact 핸들러 보관 테이블 + busy 플래그(id → bool). VM 재생성마다 새로 만든다.
    sol::table handlers = lua.create_table();
    sol::table busy     = lua.create_table();
    npc["__handlers"] = handlers;
    npc["__busy"]     = busy;

    // ---- register{ ... } : NPC 등록 ----
    npc.set_function("register", [sys, handlers](sol::table t) mutable -> bool {
        if (!sys) return false;
        sol::optional<double> ent = t["entity"];
        if (!ent) return false;

        NpcDesc d;
        d.entity = ecs::Entity::FromPacked(static_cast<uint64_t>(*ent));
        sol::optional<std::string> id = t["id"];
        d.id = id.value_or(std::string{});

        // register 테이블에 on_interact 가 실렸으면 핸들러 테이블에 보관(id 키). 없으면 별도
        //   mye.npc.on_interact(id, fn) 로 등록 가능.
        sol::optional<sol::function> onInteract = t["on_interact"];
        if (onInteract) handlers[d.id] = *onInteract;
        d.mode = ParseMode(t);
        d.waypoints = ParseWaypoints(t["waypoints"]);
        sol::optional<bool> loop = t["loop"];
        d.loop = loop.value_or(true);
        d.moveSpeed      = GetF(t, "speed", d.moveSpeed);
        d.waitMin        = GetF(t, "wait_min", d.waitMin);
        d.waitMax        = GetF(t, "wait_max", d.waitMax);
        d.wanderRadius   = GetF(t, "wander_radius", d.wanderRadius);
        d.alertRadius    = GetF(t, "alert_radius", d.alertRadius);
        d.interactRadius = GetF(t, "interact_radius", d.interactRadius);
        sol::optional<bool> face = t["face_player"];
        d.facePlayerOnAlert = face.value_or(true);

        return sys->Register(d);
    });

    // on_interact 를 id 로 등록(register 와 분리 호출 가능). id 는 register 의 id 와 일치.
    npc.set_function("on_interact", [handlers](const std::string& id, sol::function fn) mutable {
        handlers[id] = fn;
    });

    npc.set_function("set_player", [sys](double packed) {
        if (sys) sys->SetPlayer(ecs::Entity::FromPacked(static_cast<uint64_t>(packed)));
    });
    npc.set_function("unregister", [sys](double packed) {
        if (sys) sys->Unregister(ecs::Entity::FromPacked(static_cast<uint64_t>(packed)));
    });
    npc.set_function("clear", [sys]() { if (sys) sys->Clear(); });
    npc.set_function("count", [sys]() -> int { return sys ? static_cast<int>(sys->Count()) : 0; });

    // 플레이어 입력·트리거가 호출: 가장 가까운/특정 NPC 상호작용 시도.
    npc.set_function("interact", [sys]() -> double {
        if (!sys) return 0.0;
        ecs::Entity e = sys->TryInteractNearest();
        return static_cast<double>(e.Packed());
    });
    npc.set_function("interact_with", [sys](double packed) -> bool {
        if (!sys) return false;
        return sys->TryInteract(ecs::Entity::FromPacked(static_cast<uint64_t>(packed)));
    });
    npc.set_function("is_interacting", [sys]() -> bool {
        return sys && sys->IsAnyInteracting();
    });
    npc.set_function("interacting_npc", [sys]() -> double {
        if (!sys) return 0.0;
        return static_cast<double>(sys->InteractingNpc().Packed());
    });

    // ---- C++ ↔ Lua 상호작용 디스패처(내부) ----
    //   NpcSystem.InteractFn → __begin(id): on_interact 를 코루틴으로 실행(pcall 로 busy 정리 보장).
    //   NpcSystem.InteractBusyFn → __busy(id): busy 플래그 폴링.
    lua.safe_script(R"LUA(
        local npc = mye.npc
        local handlers = npc.__handlers
        local busy = npc.__busy

        -- C++ 가 상호작용 개시 시 호출. on_interact 가 없으면 false(거부) → NpcSystem 배회 유지.
        --   있으면 코루틴 실행하고 true(수락) 반환.
        function npc.__begin(id)
            local fn = handlers[id]
            if not fn then return false end
            busy[id] = true
            mye.co.start(function()
                -- pcall 로 감싸 on_interact 에러가 나도 busy 를 반드시 내린다(에러 격리).
                local ok, err = pcall(fn)
                busy[id] = false
                if not ok then
                    print("[npc] on_interact 오류(" .. tostring(id) .. "): " .. tostring(err))
                end
            end)
            return true
        end

        -- C++ busy 폴링. 명시 플래그가 없으면 false(안전).
        function npc.__busy(id)
            return busy[id] == true
        end
    )LUA", "mye.npc.dispatch");

    // C++ NpcSystem 에 Lua 디스패처를 InteractFn/InteractBusyFn 으로 배선.
    //   sol 함수는 VM 수명 동안 유효. VM 재생성 시 이 Register 가 다시 호출돼 재배선된다.
    if (m_npc) {
        sol::protected_function begin = npc["__begin"];
        sol::protected_function isBusy = npc["__busy"];
        m_npc->SetInteractHandlers(
            [begin](const std::string& id, ecs::Entity /*e*/) -> bool {
                // __begin 이 핸들러 유무로 수락/거부(bool) 판정. 거부면 NpcSystem 이 배회 유지.
                sol::protected_function_result r = begin(id);
                if (!r.valid()) return false;
                return r.get<bool>();
            },
            [isBusy](const std::string& id, ecs::Entity /*e*/) -> bool {
                sol::protected_function_result r = isBusy(id);
                if (!r.valid()) return false;
                return r.get<bool>();
            });
    }
}

} // namespace mye::runtime
