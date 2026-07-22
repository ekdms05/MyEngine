// mye/script/ScriptSystem.cpp — ScriptComponent 실행·에러 격리·핫 리로드 (docs/05, M3-C 구현)
//
// 책임:
//   - EnsureInstances: 미인스턴스화 live 컴포넌트를 LoadClass/MakeInstance/ScanCallbacks →
//     on_init 1회 호출.
//   - Update: on_start(1회)·on_update(dt)·on_late_update(dt) + 코루틴 tick + GC step.
//   - 에러 격리: 콜백/코루틴 실패 시 해당 컴포넌트만 hasError=true 정지 + ScriptErrorEvent
//     발행(즉시). 엔진·다른 엔티티는 계속(M3 완료 기준).
//   - 이벤트 라우팅: on_event(name,payload), on_trigger_enter/exit(other), AnimationEvent.
//   - 핫 리로드: 클래스 테이블 스왑 + self.state 보존 + on_hot_reload(on_init 재호출 안 함).
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

#include <sol/sol.hpp>

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
        sol::table instance;
        bool       hasOnDestroy = false;
    };
    std::unordered_map<ecs::Entity, TrackedInstance> tracked;

    Impl(ScriptRuntime& rt, ecs::World& w, EventBus* bus, asset::AssetManager* a)
        : runtime(rt), world(w), worldBus(bus), assets(a) {}
};

namespace {

bool IsFn(const sol::object& o) {
    return o.valid() && o.get_type() == sol::type::function;
}

// 콜백 실패 처리: hasError=true + ScriptErrorEvent(즉시) + 로그. M3 완료 기준(엔티티만 정지).
void ReportError(EventBus* bus, ecs::Entity e, ScriptComponent& sc,
                 std::string_view callbackName, const sol::error& err) {
    sc.hasError = true;
    if (bus) {
        detail::ParsedError pe = detail::ParseLuaError(err.what(), callbackName);
        ScriptErrorEvent ev;
        ev.entity = e;
        ev.file = pe.file;
        ev.line = pe.line;
        ev.message = pe.message;
        ev.callback = std::string(callbackName);
        bus->Publish(ev);   // 즉시 디스패치(std::string 포함 → Enqueue 불가).
    }
    MYE_LOG_ERROR("Script", "callback {} failed: {}", callbackName, err.what());
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

            sol::state& lua = m_impl->runtime.State();
            sol::table payload = lua.create_table();
            payload["name"] = name;
            payload["arg"] = strArg;
            payload["value"] = floatArg;
            payload["frame"] = frame;

            sol::object fnObj = sc->instance[callbacks::kOnEvent];
            if (IsFn(fnObj)) {
                sol::protected_function fn = fnObj.as<sol::protected_function>();
                sol::protected_function_result r =
                    fn(sc->instance, std::string("animation"), payload);
                if (!r.valid()) {
                    sol::error err = r;
                    ReportError(m_impl->worldBus, target, *sc, callbacks::kOnEvent, err);
                }
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
        if (sc.instance.valid()) return;   // 이미 인스턴스화됨
        pending.push_back(e);
    });

    for (ecs::Entity e : pending) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc) continue;

        ScriptAsset* asset = sc->script.IsValid() ? sc->script.Get() : nullptr;
        if (!asset) {
            // 소스가 아직 로드되지 않음 — 다음 프레임 재시도(에러 아님).
            continue;
        }

        std::string chunk = asset->sourcePath.empty() ? "script" : asset->sourcePath;
        Expected<sol::table, ScriptError> cls =
            LoadClass(m_impl->runtime, asset->source, chunk);
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
            sol::object fnObj = sc->instance[callbacks::kOnInit];
            if (IsFn(fnObj)) {
                InvokeGuarded(e, *sc, callbacks::kOnInit,
                              fnObj.as<sol::protected_function>());
            }
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
                sol::object fnObj = sc->instance[callbacks::kOnStart];
                if (IsFn(fnObj)) {
                    if (!InvokeGuarded(e, *sc, callbacks::kOnStart,
                                       fnObj.as<sol::protected_function>())) continue;
                }
            }
        }

        if (sc->HasCallback(CallbackBit::OnUpdate) && sc->IsLive()) {
            sol::object fnObj = sc->instance[callbacks::kOnUpdate];
            if (IsFn(fnObj)) {
                sol::protected_function fn = fnObj.as<sol::protected_function>();
                sol::protected_function_result r = fn(sc->instance, dt);
                if (!r.valid()) {
                    sol::error err = r;
                    ReportError(m_impl->worldBus, e, *sc, callbacks::kOnUpdate, err);
                }
            }
        }
    }

    // on_late_update(dt).
    for (ecs::Entity e : live) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc || !sc->IsLive()) continue;
        if (!sc->HasCallback(CallbackBit::OnLateUpdate)) continue;
        sol::object fnObj = sc->instance[callbacks::kOnLateUpdate];
        if (IsFn(fnObj)) {
            sol::protected_function fn = fnObj.as<sol::protected_function>();
            sol::protected_function_result r = fn(sc->instance, dt);
            if (!r.valid()) {
                sol::error err = r;
                ReportError(m_impl->worldBus, e, *sc, callbacks::kOnLateUpdate, err);
            }
        }
    }

    // 코루틴 tick(재개 에러 → ScriptErrorEvent) + 인크리멘털 GC.
    m_impl->runtime.UpdateCoroutines(dt);
    m_impl->runtime.CollectGarbageStep();
}

void ScriptSystem::DispatchEvent(std::string_view name, const sol::object& payload) {
    // 코루틴 wait_event 통지.
    m_impl->runtime.Coroutines().NotifyEvent(name, payload);

    // on_event(name, payload) 라우팅(정의된 컴포넌트 전부).
    std::string nameStr(name);
    std::vector<ecs::Entity> targets;
    m_impl->world.Query<ScriptComponent>().Each([&](ecs::Entity e, ScriptComponent& sc) {
        if (sc.IsLive() && sc.HasCallback(CallbackBit::OnEvent)) targets.push_back(e);
    });
    for (ecs::Entity e : targets) {
        ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
        if (!sc || !sc->IsLive()) continue;
        sol::object fnObj = sc->instance[callbacks::kOnEvent];
        if (!IsFn(fnObj)) continue;
        sol::protected_function fn = fnObj.as<sol::protected_function>();
        sol::protected_function_result r = fn(sc->instance, nameStr, payload);
        if (!r.valid()) {
            sol::error err = r;
            ReportError(m_impl->worldBus, e, *sc, callbacks::kOnEvent, err);
        }
    }
}

void ScriptSystem::OnTriggerEnter(ecs::Entity self, ecs::Entity other) {
    ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(self);
    if (!sc || !sc->IsLive() || !sc->HasCallback(CallbackBit::OnTriggerEnter)) return;
    sol::object fnObj = sc->instance[callbacks::kOnTriggerEnter];
    if (!IsFn(fnObj)) return;
    sol::protected_function fn = fnObj.as<sol::protected_function>();
    sol::protected_function_result r = fn(sc->instance, other.Packed());
    if (!r.valid()) {
        sol::error err = r;
        ReportError(m_impl->worldBus, self, *sc, callbacks::kOnTriggerEnter, err);
    }
}

void ScriptSystem::OnTriggerExit(ecs::Entity self, ecs::Entity other) {
    ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(self);
    if (!sc || !sc->IsLive() || !sc->HasCallback(CallbackBit::OnTriggerExit)) return;
    sol::object fnObj = sc->instance[callbacks::kOnTriggerExit];
    if (!IsFn(fnObj)) return;
    sol::protected_function fn = fnObj.as<sol::protected_function>();
    sol::protected_function_result r = fn(sc->instance, other.Packed());
    if (!r.valid()) {
        sol::error err = r;
        ReportError(m_impl->worldBus, self, *sc, callbacks::kOnTriggerExit, err);
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
        Expected<sol::table, ScriptError> cls =
            LoadClass(m_impl->runtime, asset->source, chunk);
        if (!cls) {
            // 문법 에러 — 기존 클래스 유지 + 에러 발행(게임 지속, docs/05).
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
        if (!sc->instance.valid()) {
            // 아직 인스턴스가 없던 경우(예: 에러 상태) 새로 만든다.
            sc->instance = MakeInstance(m_impl->runtime, sc->classTable, e,
                                        sc->properties.values);
        } else {
            // 인스턴스의 메타테이블 __index 를 새 클래스로 교체(self.state 유지).
            sol::state& lua = m_impl->runtime.State();
            sol::table meta = lua.create_table();
            meta["__index"] = sc->classTable;
            sc->instance[sol::metatable_key] = meta;
        }
        sc->callbacks = ScanCallbacks(sc->classTable);
        sc->hasError = false;   // 리로드 성공 시 정지 해제(docs/05).

        // 추적 갱신(instance 재생성·on_destroy 유무 변경 반영).
        m_impl->tracked[e] = { sc->instance, sc->HasCallback(CallbackBit::OnDestroy) };

        // on_hot_reload 호출(on_init 재호출 안 함, docs/05).
        if (sc->HasCallback(CallbackBit::OnHotReload) && sc->IsLive()) {
            sol::object fnObj = sc->instance[callbacks::kOnHotReload];
            if (IsFn(fnObj)) {
                InvokeGuarded(e, *sc, callbacks::kOnHotReload,
                              fnObj.as<sol::protected_function>());
            }
        }
    }
}

void ScriptSystem::CallOnEntity(ecs::Entity e, std::string_view callback,
                                const sol::object& arg) {
    ScriptComponent* sc = m_impl->world.TryGet<ScriptComponent>(e);
    if (!sc || !sc->IsLive()) return;
    sol::object fnObj = sc->instance[callback];
    if (!IsFn(fnObj)) return;
    sol::protected_function fn = fnObj.as<sol::protected_function>();

    sol::protected_function_result r = arg.valid() ? fn(sc->instance, arg) : fn(sc->instance);
    if (!r.valid()) {
        sol::error err = r;
        ReportError(m_impl->worldBus, e, *sc, callback, err);
    }
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
        if (ti.hasOnDestroy && ti.instance.valid()) {
            sol::object fnObj = ti.instance[callbacks::kOnDestroy];
            if (IsFn(fnObj)) {
                sol::protected_function fn = fnObj.as<sol::protected_function>();
                sol::protected_function_result r = fn(ti.instance);
                if (!r.valid()) {
                    sol::error err = r;
                    MYE_LOG_ERROR("Script", "on_destroy failed: {}", err.what());
                }
            }
        }

        // 소유 코루틴 취소(파괴된 엔티티의 코루틴이 살아남지 않게).
        m_impl->runtime.Coroutines().CancelForEntity(e);
    }
}

bool ScriptSystem::InvokeGuarded(ecs::Entity e, ScriptComponent& sc,
                                 std::string_view callbackName,
                                 const sol::protected_function& fn) {
    sol::protected_function_result r = fn(sc.instance);
    if (r.valid()) return true;

    sol::error err = r;
    ReportError(m_impl->worldBus, e, sc, callbackName, err);
    return false;
}

} // namespace mye::script
