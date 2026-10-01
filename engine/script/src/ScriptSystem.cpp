#include "mye/script/ScriptSystem.h"

#include "mye/anim/AnimationTypes.h"
#include "mye/asset/AssetDatabase.h"      // AssetReloadedEvent
#include "mye/asset/AssetManager.h"
#include "mye/core/Events.h"
#include "mye/core/Log.h"
#include "mye/core/Time.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"           // TriggerEnter/ExitEvent
#include "mye/scene/SystemScheduler.h"
#include "mye/script/CoroutineScheduler.h"
#include "mye/script/ScriptClass.h"
#include "mye/script/ScriptRuntime.h"

#include "ScriptErrorParse.h"

#include "mye/script/LuaApi.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mye::script {

struct ScriptSystem::Impl {
    ScriptRuntime&       runtime;
    ecs::World&          world;
    EventBus*            worldBus = nullptr;
    asset::AssetManager* assets = nullptr;

    // 월드 버스 구독 보관(RAII 해제).
    std::vector<ScopedSubscription> subs;

    // 살아있는(인스턴스화된) 스크립트 엔티티 추적 — 파괴 감지용. World 에 파괴 훅이 없으므로
    //   매 프레임 현재 live 집합과 대조해 사라진 엔티티에 on_destroy + 코루틴 취소를 수행한다.
    //   instance/on_destroy 비트를 보관해 컴포넌트가 이미 제거된 뒤에도 on_destroy 를 부를 수 있다.
    struct TrackedInstance {
        LuaReference instance;
        bool       hasOnDestroy = false;
    };
    std::unordered_map<ecs::Entity, TrackedInstance> tracked;

    Impl(ScriptRuntime& rt, ecs::World& w, EventBus* bus, asset::AssetManager* a)
        : runtime(rt), world(w), worldBus(bus), assets(a) {}
};

namespace {

// Push function and self in call order. The caller's guard owns stack cleanup.
bool PushCallback(lua_State* lua, const LuaReference& instance, std::string_view name) {
    if (!instance.Valid()) return false;
    instance.Push(lua);
    lua_pushlstring(lua, name.data(), name.size());
    lua_gettable(lua, -2);
    if (!lua_isfunction(lua, -1)) { lua_pop(lua, 2); return false; }
    lua_insert(lua, -2);
    return true;
}

// 콜백 실패 처리: hasError=true + ScriptErrorEvent(즉시) + 로그. 엔티티만 정지한다.
void ReportError(EventBus* bus, ecs::Entity e, ScriptComponent& sc,
                 std::string_view callbackName, const char* message) {
    sc.hasError = true;
    if (bus) {
        detail::ParsedError pe = detail::ParseLuaError(message, callbackName);
        ScriptErrorEvent ev;
        ev.entity = e;
        ev.file = pe.file;
        ev.line = pe.line;
        ev.message = pe.message;
        ev.callback = std::string(callbackName);
        bus->Publish(ev);   // 즉시 디스패치(std::string 포함 → Enqueue 불가).
    }
    MYE_LOG_ERROR("Script", "callback {} failed: {}", callbackName, message);
}

} // namespace

ScriptSystem::ScriptSystem(ScriptRuntime& runtime, ecs::World& world, EventBus* worldBus,
                           asset::AssetManager* assets)
    : m_impl(std::make_unique<Impl>(runtime, world, worldBus, assets)) {}

ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::RegisterComponent() {
    m_impl->world.RegisterComponent<ScriptComponent>("ScriptComponent");

    EventBus* bus = m_impl->worldBus;
    if (!bus) return;

    // 트리거 Enter/Exit → on_trigger_enter/exit 라우팅. trigger(콜라이더)·other(진입) 양측에
    //   스크립트가 있으면 각각 상대를 인자로 콜백을 받는다(대칭 라우팅).
    m_impl->subs.emplace_back(*bus, bus->Subscribe<phys::TriggerEnterEvent>(
        [this](const phys::TriggerEnterEvent& e) {
            OnTriggerEnter(e.trigger, e.other);
            OnTriggerEnter(e.other, e.trigger);
            return false;
        }));
    m_impl->subs.emplace_back(*bus, bus->Subscribe<phys::TriggerExitEvent>(
        [this](const phys::TriggerExitEvent& e) {
            OnTriggerExit(e.trigger, e.other);
            OnTriggerExit(e.other, e.trigger);
            return false;
        }));

    // AnimationEvent(footstep 등) → on_event("animation", { name, arg, frame }) 로 라우팅.
    //   name/stringArg 는 즉시 디스패치 중에만 유효 → 여기서 std::string 으로 복사한다.
    m_impl->subs.emplace_back(*bus, bus->Subscribe<anim::AnimationEvent>(
        [this](const anim::AnimationEvent& e) {
            ecs::Entity target = e.entity;
            std::string name = e.name ? e.name : "";
            std::string strArg = e.stringArg ? e.stringArg : "";
            float floatArg = e.floatArg;
            uint32_t frame = e.frameIndex;

            ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(target);
            if (!sc || !sc->IsLive() || !sc->HasCallback(CallbackBit::OnEvent)) return false;

            lua_State* lua = m_impl->runtime.State();
            LuaStackGuard stack(lua);
            if (PushCallback(lua, sc->instance, callbacks::kOnEvent)) {
                lua_pushliteral(lua, "animation");
                lua_newtable(lua);
                lua_pushlstring(lua, name.data(), name.size()); lua_setfield(lua, -2, "name");
                lua_pushlstring(lua, strArg.data(), strArg.size()); lua_setfield(lua, -2, "arg");
                lua_pushnumber(lua, floatArg); lua_setfield(lua, -2, "value");
                lua_pushinteger(lua, frame); lua_setfield(lua, -2, "frame");
                InvokeGuarded(target, *sc, callbacks::kOnEvent, 2);
            }
            return false;
        }));

    // .lua 핫 리로드(AssetReloadedEvent) → 해당 GUID 를 쓰는 컴포넌트 클래스 스왑.
    m_impl->subs.emplace_back(*bus, bus->Subscribe<asset::AssetReloadedEvent>(
        [this](const asset::AssetReloadedEvent& e) {
            if (e.type == ScriptAsset::kAssetTypeId) HotReload(e.guid);
            return false;
        }));
}

void ScriptSystem::RegisterInto(scene::SystemScheduler& scheduler) {
    scene::SystemDesc desc;
    desc.name = "mye.ScriptSystem";
    desc.phase = scene::Phase::Update;
    desc.fn = [this](ecs::World&, ecs::CommandBuffer&, const TimeStep& t) {
        EnsureInstances();
        Update(static_cast<float>(t.deltaSeconds));
    };
    scheduler.RegisterSystem(std::move(desc));
}

void ScriptSystem::EnsureInstances() {
    std::vector<ecs::Entity> pending;
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent& sc) {
        if (!sc.enabled || sc.hasError) return;
        if (sc.instance.Valid()) return;   // 이미 인스턴스화됨
        pending.push_back(e);
    });

    for (ecs::Entity e : pending) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc) continue;

        ScriptAsset* asset = sc->script.IsValid() ? sc->script.Get() : nullptr;
        if (!asset && sc->inlineSource.empty()) {
            // 소스가 아직 로드되지 않음 — 다음 프레임 재시도(에러 아님).
            continue;
        }

        std::string chunk = asset && !asset->sourcePath.empty() ? asset->sourcePath : "object-script";
        Expected<LuaReference, ScriptError> cls =
            LoadClass(m_impl->runtime, sc->inlineSource.empty() ? asset->source : sc->inlineSource, chunk);
        if (!cls) {
            const ScriptError& err = cls.GetError();
            sc->hasError = true;
            if (m_impl->worldBus) {
                ScriptErrorEvent ev;
                ev.entity = e; ev.file = err.file; ev.line = err.line;
                ev.message = err.message; ev.callback = "load";
                m_impl->worldBus->Publish(ev);
            }
            MYE_LOG_ERROR("Script", "load failed [{}]: {}", chunk, err.message);
            continue;
        }

        sc->classTable = cls.Value();
        sc->instance = MakeInstance(m_impl->runtime, sc->classTable, e,
                                    sc->properties.values);
        sc->callbacks = ScanCallbacks(sc->classTable);
        sc->started = false;
        sc->hasError = false;

        // 파괴 감지용 추적 등록(instance/on_destroy 보관 — 컴포넌트 제거 후에도 on_destroy 가능).
        m_impl->tracked[e] = { sc->instance, sc->HasCallback(CallbackBit::OnDestroy) };

        if (sc->HasCallback(CallbackBit::OnInit) && sc->IsLive()) {
            CallOnEntity(e, callbacks::kOnInit, {});
        }
    }
}

void ScriptSystem::Update(float dt) {
    // 파괴된 스크립트 엔티티 정리(on_destroy + 코루틴 취소) — 프레임 경계에서 먼저.
    ReconcileDestroyed();

    // on_start(1회) + on_update(dt).
    std::vector<ecs::Entity> live;
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent& sc) {
        if (sc.IsLive()) live.push_back(e);
    });

    for (ecs::Entity e : live) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc || !sc->IsLive()) continue;

        // 파괴 감지용 추적 등록/갱신(EnsureInstances 를 안 거친 직접 설치 경로도 포함).
        //   instance/on_destroy 비트를 최신으로 유지.
        m_impl->tracked[e] = { sc->instance, sc->HasCallback(CallbackBit::OnDestroy) };

        if (!sc->started) {
            sc->started = true;
            if (sc->HasCallback(CallbackBit::OnStart)) {
                CallOnEntity(e, callbacks::kOnStart, {});
                if (!sc->IsLive()) continue;
            }
        }

        if (sc->HasCallback(CallbackBit::OnUpdate) && sc->IsLive()) {
            lua_State* lua = m_impl->runtime.State();
            LuaStackGuard stack(lua);
            if (PushCallback(lua, sc->instance, callbacks::kOnUpdate)) {
                lua_pushnumber(lua, dt);
                InvokeGuarded(e, *sc, callbacks::kOnUpdate, 1);
            }
        }
    }

    // on_late_update(dt).
    for (ecs::Entity e : live) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc || !sc->IsLive()) continue;
        if (!sc->HasCallback(CallbackBit::OnLateUpdate)) continue;
        lua_State* lua = m_impl->runtime.State();
        LuaStackGuard stack(lua);
        if (PushCallback(lua, sc->instance, callbacks::kOnLateUpdate)) {
            lua_pushnumber(lua, dt);
            InvokeGuarded(e, *sc, callbacks::kOnLateUpdate, 1);
        }
    }

    // 코루틴 tick(재개 에러 → ScriptErrorEvent) + 인크리멘털 GC.
    m_impl->runtime.UpdateCoroutines(dt);
    m_impl->runtime.CollectGarbageStep();
}

void ScriptSystem::DispatchEvent(std::string_view name, const LuaReference& payload) {
    m_impl->runtime.Coroutines().NotifyEvent(name, payload);
    std::vector<ecs::Entity> targets;
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent& sc) {
        if (sc.IsLive() && sc.HasCallback(CallbackBit::OnEvent)) targets.push_back(e);
    });
    lua_State* lua = m_impl->runtime.State();
    for (ecs::Entity e : targets) {
        auto* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc || !sc->IsLive()) continue;
        LuaStackGuard stack(lua);
        if (PushCallback(lua, sc->instance, callbacks::kOnEvent)) {
            lua_pushlstring(lua, name.data(), name.size());
            payload.Push(lua);
            InvokeGuarded(e, *sc, callbacks::kOnEvent, 2);
        }
    }
}

void ScriptSystem::OnTriggerEnter(ecs::Entity self, ecs::Entity other) {
    auto* sc = m_impl->world.TryGet<ScriptComponent>(self);
    if (!sc || !sc->IsLive() || !sc->HasCallback(CallbackBit::OnTriggerEnter)) return;
    lua_State* lua = m_impl->runtime.State();
    LuaStackGuard stack(lua);
    if (PushCallback(lua, sc->instance, callbacks::kOnTriggerEnter)) {
        lua_pushinteger(lua, static_cast<lua_Integer>(other.Packed()));
        InvokeGuarded(self, *sc, callbacks::kOnTriggerEnter, 1);
    }
}
void ScriptSystem::OnTriggerExit(ecs::Entity self, ecs::Entity other) {
    auto* sc = m_impl->world.TryGet<ScriptComponent>(self);
    if (!sc || !sc->IsLive() || !sc->HasCallback(CallbackBit::OnTriggerExit)) return;
    lua_State* lua = m_impl->runtime.State();
    LuaStackGuard stack(lua);
    if (PushCallback(lua, sc->instance, callbacks::kOnTriggerExit)) {
        lua_pushinteger(lua, static_cast<lua_Integer>(other.Packed()));
        InvokeGuarded(self, *sc, callbacks::kOnTriggerExit, 1);
    }
}

void ScriptSystem::HotReload(asset::AssetGuid scriptGuid) {
    // 해당 GUID 를 쓰는 모든 ScriptComponent 순회 — 새 소스로 클래스 재로드.
    std::vector<ecs::Entity> targets;
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent& sc) {
        if (sc.script.IsValid() && sc.script.Guid() == scriptGuid) targets.push_back(e);
    });
    if (targets.empty()) return;

    // 소스는 슬롯 스왑으로 이미 갱신됨 — 첫 컴포넌트에서 소스를 얻어 클래스 1회 재컴파일.
    for (ecs::Entity e : targets) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc) continue;
        ScriptAsset* asset = sc->script.Get();
        if (!asset) continue;

        std::string chunk = asset->sourcePath.empty() ? "script" : asset->sourcePath;
        Expected<LuaReference, ScriptError> cls =
            LoadClass(m_impl->runtime, asset->source, chunk);
        if (!cls) {
            // 문법 에러 — 기존 클래스 유지 + 에러 발행(게임 지속).
            const ScriptError& err = cls.GetError();
            if (m_impl->worldBus) {
                ScriptErrorEvent ev;
                ev.entity = e; ev.file = err.file; ev.line = err.line;
                ev.message = err.message; ev.callback = "hot_reload";
                m_impl->worldBus->Publish(ev);
            }
            MYE_LOG_ERROR("Script", "hot reload failed [{}]: {}", chunk, err.message);
            continue;
        }

        // 클래스 스왑 — self.state·self.entity 는 기존 인스턴스에서 보존.
        sc->classTable = cls.Value();
        if (!sc->instance.Valid()) {
            // 아직 인스턴스가 없던 경우(예: 에러 상태) 새로 만든다.
            sc->instance = MakeInstance(m_impl->runtime, sc->classTable, e,
                                        sc->properties.values);
        } else {
            // 인스턴스의 메타테이블 __index 를 새 클래스로 교체(self.state 유지).
            lua_State* lua = m_impl->runtime.State();
            LuaStackGuard stack(lua);
            sc->instance.Push(lua);
            lua_newtable(lua);
            sc->classTable.Push(lua); lua_setfield(lua, -2, "__index");
            lua_setmetatable(lua, -2);
        }
        sc->callbacks = ScanCallbacks(sc->classTable);
        sc->hasError = false;   // 리로드 성공 시 정지 해제.

        // 추적 갱신(instance 재생성·on_destroy 유무 변경 반영).
        m_impl->tracked[e] = { sc->instance, sc->HasCallback(CallbackBit::OnDestroy) };

        // on_hot_reload 호출(on_init 재호출 안 함).
        if (sc->HasCallback(CallbackBit::OnHotReload) && sc->IsLive()) {
            CallOnEntity(e, callbacks::kOnHotReload, {});
        }
    }
}

void ScriptSystem::CallOnEntity(ecs::Entity e, std::string_view callback, const LuaReference& arg) {
    auto* sc = m_impl->world.TryGet<ScriptComponent>(e);
    if (!sc || !sc->IsLive()) return;
    lua_State* lua = m_impl->runtime.State();
    LuaStackGuard stack(lua);
    if (!PushCallback(lua, sc->instance, callback)) return;
    if (arg.Valid()) arg.Push(lua);
    InvokeGuarded(e, *sc, callback, arg.Valid() ? 1 : 0);
}

void ScriptSystem::ReconcileDestroyed() {
    if (m_impl->tracked.empty()) return;

    // 현재 존재하는(유효 + ScriptComponent 보유) 스크립트 엔티티 집합.
    std::unordered_map<ecs::Entity, bool> present;
    present.reserve(m_impl->tracked.size());
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent&) {
        present[e] = true;
    });

    std::vector<ecs::Entity> destroyed;
    for (auto& [e, ti] : m_impl->tracked) {
        // Query 로 잡힌 엔티티는 유효(Valid)하다. present 에 없으면 파괴된 것.
        if (present.find(e) == present.end()) destroyed.push_back(e);
    }

    for (ecs::Entity e : destroyed) {
        auto it = m_impl->tracked.find(e);
        if (it == m_impl->tracked.end()) continue;
        Impl::TrackedInstance ti = it->second;
        m_impl->tracked.erase(it);

        // on_destroy 호출(정의돼 있고 인스턴스가 유효하면). 컴포넌트는 이미 없을 수 있으므로
        //   보관해 둔 instance 로 protected 호출(에러는 로그만 — 컴포넌트 격리 대상이 없음).
        if (ti.hasOnDestroy && ti.instance.Valid()) {
            lua_State* lua = m_impl->runtime.State();
            LuaStackGuard stack(lua);
            if (PushCallback(lua, ti.instance, callbacks::kOnDestroy) && ProtectedCall(lua, 1, 0) != LUA_OK)
                MYE_LOG_ERROR("Script", "on_destroy failed: {}", lua_tostring(lua, -1));
        }

        // 소유 코루틴 취소(파괴된 엔티티의 코루틴이 살아남지 않게).
        m_impl->runtime.Coroutines().CancelForEntity(e);
    }
}

bool ScriptSystem::InvokeGuarded(ecs::Entity e, ScriptComponent& sc,
                                 std::string_view callbackName, int arguments) {
    lua_State* lua = m_impl->runtime.State();
    if (ProtectedCall(lua, arguments + 1, 0) == LUA_OK) return true;
    const char* message = lua_tostring(lua, -1);
    ReportError(m_impl->worldBus, e, sc, callbackName, message ? message : "Lua callback failed");
    return false;
}
} // namespace mye::script
