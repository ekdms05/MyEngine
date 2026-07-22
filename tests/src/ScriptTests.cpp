// ScriptTests.cpp — mye_script 검증 (05, M3-C): 콜백 순서·에러 격리·핫리로드·코루틴
//
// GPU·디바이스 불필요(순수 CPU). 각 테스트는 자체 ScriptRuntime(단일 sol::state) +
//   World + ScriptSystem 을 세우고, 스크립트 소스는 파일(핫리로드 케이스) 또는 인메모리
//   LoadClass/MakeInstance(콜백·코루틴 케이스)로 주입한다.
#include "TestFramework.h"

#include "mye/anim/AnimationTypes.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/FileSystem.h"
#include "mye/core/Events.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/script/CoroutineScheduler.h"
#include "mye/script/ScriptClass.h"
#include "mye/script/ScriptComponent.h"
#include "mye/script/ScriptImporter.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/ScriptSystem.h"

#include <sol/sol.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::script;

namespace {

std::atomic<int> g_scriptTempCounter{0};

std::filesystem::path MakeTempDir() {
    auto base = std::filesystem::temp_directory_path() /
                ("mye_script_test_" + std::to_string(g_scriptTempCounter++));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

void WriteText(const std::filesystem::path& p, std::string_view text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

StdLibPolicy DefaultPolicy() {
    StdLibPolicy p;   // base/table/string/math/coroutine/utf8 (io/os/package/debug off)
    return p;
}

// 인메모리로 ScriptComponent 를 인스턴스화(에셋 파이프라인 우회 — 콜백/코루틴 테스트용).
//   ScriptSystem::EnsureInstances 는 에셋 핸들을 요구하므로, 직접 classTable/instance 를 채운다.
bool InstallScript(ScriptRuntime& rt, ecs::World& world, ecs::Entity e,
                   std::string_view source, std::string_view chunk) {
    ScriptComponent* sc = world.TryGet<ScriptComponent>(e);
    if (!sc) return false;
    auto cls = LoadClass(rt, source, chunk);
    if (!cls) return false;
    sc->classTable = cls.Value();
    sc->instance = MakeInstance(rt, sc->classTable, e, sc->properties.values);
    sc->callbacks = ScanCallbacks(sc->classTable);
    sc->started = false;
    sc->hasError = false;
    return true;
}

} // namespace

// ===========================================================================
// 임포터 — .lua 원문·해시
// ===========================================================================
MYE_TEST(ScriptImporterReadsSourceAndHash) {
    auto dir = MakeTempDir();
    const char* src = "return { on_update = function(self, dt) end }\n";
    WriteText(dir / "npc.lua", src);

    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(dir.string()), 0);
    asset::AssetManager mgr(vfs, /*device=*/nullptr);
    mgr.RegisterImporter(std::make_unique<ScriptImporter>());

    auto h = mgr.LoadSync<ScriptAsset>("assets://npc.lua");
    MYE_EXPECT(h.State() == asset::AssetState::Loaded);
    ScriptAsset* a = h.Get();
    MYE_EXPECT(a != nullptr);
    if (a) {
        MYE_EXPECT(a->source == std::string(src));
        MYE_EXPECT(a->sourceHash != 0);
    }

    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

// ===========================================================================
// 콜백 호출 순서 — on_init → on_start → on_update → on_late_update
// ===========================================================================
MYE_TEST(ScriptCallbackOrder) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, nullptr);
    ss.RegisterComponent();

    // 전역 로그 테이블에 콜백 이름을 append.
    (void)rt.DoString("_order = {}", "test_setup");

    ecs::Entity e = world.Create();
    world.Add<ScriptComponent>(e);
    const char* src = R"LUA(
        local C = {}
        function C:on_init()        table.insert(_order, "init") end
        function C:on_start()       table.insert(_order, "start") end
        function C:on_update(dt)    table.insert(_order, "update") end
        function C:on_late_update(dt) table.insert(_order, "late") end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, e, src, "order.lua"));

    // EnsureInstances 는 에셋 요구 → 여기선 on_init 을 직접 호출(인메모리 경로).
    ss.CallOnEntity(e, callbacks::kOnInit, sol::object());

    ss.Update(0.016f);
    ss.Update(0.016f);

    sol::state& lua = rt.State();
    sol::table order = lua["_order"];
    std::vector<std::string> got;
    for (std::size_t i = 1; i <= order.size(); ++i) got.push_back(order[i].get<std::string>());

    // 기대: init, start, update, late, update, late
    MYE_EXPECT(got.size() == 6);
    if (got.size() == 6) {
        MYE_EXPECT(got[0] == "init");
        MYE_EXPECT(got[1] == "start");
        MYE_EXPECT(got[2] == "update");
        MYE_EXPECT(got[3] == "late");
        MYE_EXPECT(got[4] == "update");
        MYE_EXPECT(got[5] == "late");
    }
}

// ===========================================================================
// 에러 격리 — 에러 엔티티만 정지, 나머지 진행, ScriptErrorEvent 발행
// ===========================================================================
MYE_TEST(ScriptErrorIsolation) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, nullptr);
    ss.RegisterComponent();

    int errorEvents = 0;
    ecs::Entity erroredEntity = ecs::Entity::Null();
    bus.Subscribe<ScriptErrorEvent>([&](const ScriptErrorEvent& ev) {
        ++errorEvents;
        erroredEntity = ev.entity;
        return false;
    });

    (void)rt.DoString("_good_ticks = 0", "test_setup");

    // 에러 엔티티: on_update 에서 nil 인덱싱.
    ecs::Entity bad = world.Create();
    world.Add<ScriptComponent>(bad);
    const char* badSrc = R"LUA(
        local C = {}
        function C:on_update(dt) local x = nil; return x.field end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, bad, badSrc, "bad.lua"));

    // 정상 엔티티: on_update 마다 카운트 증가.
    ecs::Entity good = world.Create();
    world.Add<ScriptComponent>(good);
    const char* goodSrc = R"LUA(
        local C = {}
        function C:on_update(dt) _good_ticks = _good_ticks + 1 end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, good, goodSrc, "good.lua"));

    ss.Update(0.016f);   // bad 에러 발생·정지, good 진행
    ss.Update(0.016f);   // bad 는 not-live → 스킵, good 계속

    MYE_EXPECT(errorEvents == 1);           // 정지된 뒤 재발행 없음
    MYE_EXPECT(erroredEntity == bad);

    ScriptComponent* badSc = world.TryGet<ScriptComponent>(bad);
    ScriptComponent* goodSc = world.TryGet<ScriptComponent>(good);
    MYE_EXPECT(badSc && badSc->hasError);
    MYE_EXPECT(goodSc && !goodSc->hasError);

    sol::state& lua = rt.State();
    int ticks = lua["_good_ticks"];
    MYE_EXPECT(ticks == 2);                  // good 는 두 프레임 모두 실행
}

// ===========================================================================
// on_event / on_trigger — 이벤트 라우팅
// ===========================================================================
MYE_TEST(ScriptEventAndTriggerRouting) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, nullptr);
    ss.RegisterComponent();

    (void)rt.DoString("_ev = {}", "test_setup");

    ecs::Entity e = world.Create();
    world.Add<ScriptComponent>(e);
    const char* src = R"LUA(
        local C = {}
        function C:on_event(name, payload) _ev[#_ev+1] = name end
        function C:on_trigger_enter(other) _ev[#_ev+1] = "enter" end
        function C:on_trigger_exit(other)  _ev[#_ev+1] = "exit" end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, e, src, "ev.lua"));

    ss.DispatchEvent("hello", sol::make_object(rt.State(), 42));
    ss.OnTriggerEnter(e, ecs::Entity{99, 1});
    ss.OnTriggerExit(e, ecs::Entity{99, 1});

    sol::state& lua = rt.State();
    sol::table ev = lua["_ev"];
    MYE_EXPECT(ev.size() == 3);
    if (ev.size() == 3) {
        MYE_EXPECT(ev[1].get<std::string>() == "hello");
        MYE_EXPECT(ev[2].get<std::string>() == "enter");
        MYE_EXPECT(ev[3].get<std::string>() == "exit");
    }
}

// ===========================================================================
// 월드 버스 구독 경로 — TriggerEnterEvent·AnimationEvent 발행이 콜백으로 라우팅
//   (M3 완료 기준: 발소리 애니메이션 이벤트 → on_event 라우팅)
// ===========================================================================
MYE_TEST(ScriptWorldBusRouting) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, nullptr);
    ss.RegisterComponent();   // 여기서 트리거·애니 이벤트 구독

    (void)rt.DoString("_wb = { steps = 0, enters = 0, lastFoot = '' }", "test_setup");

    ecs::Entity e = world.Create();
    world.Add<ScriptComponent>(e);
    const char* src = R"LUA(
        local C = {}
        function C:on_event(name, payload)
            if name == "animation" and payload.name == "footstep" then
                _wb.steps = _wb.steps + 1
                _wb.lastFoot = payload.arg
            end
        end
        function C:on_trigger_enter(other) _wb.enters = _wb.enters + 1 end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, e, src, "wb.lua"));

    // 발소리 애니메이션 이벤트 발행(name/stringArg 는 즉시 디스패치 중만 유효 → 시스템이 복사).
    anim::AnimationEvent ae;
    ae.entity = e;
    ae.name = "footstep";
    ae.stringArg = "sfx_step_grass";
    ae.frameIndex = 3;
    bus.Publish(ae);

    // 트리거 진입 이벤트 발행(trigger 엔티티에 스크립트가 있으므로 on_trigger_enter 발화).
    phys::TriggerEnterEvent te;
    te.trigger = e;
    te.other = ecs::Entity{123, 1};
    bus.Publish(te);

    sol::state& lua = rt.State();
    MYE_EXPECT(int(lua["_wb"]["steps"]) == 1);
    MYE_EXPECT(lua["_wb"]["lastFoot"].get<std::string>() == "sfx_step_grass");
    MYE_EXPECT(int(lua["_wb"]["enters"]) == 1);
}

// ===========================================================================
// 코루틴 wait_seconds — 시간 경과 후 재개
// ===========================================================================
MYE_TEST(ScriptCoroutineWaitSeconds) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    (void)rt.DoString("_co = { phase = 0 }", "test_setup");
    // 코루틴: phase=1 → wait_seconds(1.0) → phase=2 → wait_seconds(1.0) → phase=3
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            _co.phase = 1
            mye.co.wait_seconds(1.0)
            _co.phase = 2
            mye.co.wait_seconds(1.0)
            _co.phase = 3
        end)
    )LUA", "co.lua");
    MYE_EXPECT(bool(r));

    sol::state& lua = rt.State();
    MYE_EXPECT(int(lua["_co"]["phase"]) == 1);    // 첫 실행에서 phase=1 후 첫 wait 에서 yield
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 1);

    rt.UpdateCoroutines(0.5f);
    MYE_EXPECT(int(lua["_co"]["phase"]) == 1);    // 아직 1초 안 지남

    rt.UpdateCoroutines(0.6f);                    // 누적 1.1초 → 재개
    MYE_EXPECT(int(lua["_co"]["phase"]) == 2);

    rt.UpdateCoroutines(1.0f);                    // 두 번째 wait 만료
    MYE_EXPECT(int(lua["_co"]["phase"]) == 3);

    rt.UpdateCoroutines(0.1f);                    // 종료 후 제거 확인
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);
}

// ===========================================================================
// 코루틴 wait_event — 이벤트 도착 시 재개(payload 전달)
// ===========================================================================
MYE_TEST(ScriptCoroutineWaitEvent) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    (void)rt.DoString("_ce = { got = 0, phase = 0 }", "test_setup");
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            _ce.phase = 1
            local payload = mye.co.wait_event("door_opened")
            _ce.phase = 2
            _ce.got = payload
        end)
    )LUA", "ce.lua");
    MYE_EXPECT(bool(r));

    sol::state& lua = rt.State();
    MYE_EXPECT(int(lua["_ce"]["phase"]) == 1);

    rt.UpdateCoroutines(0.016f);                  // 이벤트 없음 → 계속 대기
    MYE_EXPECT(int(lua["_ce"]["phase"]) == 1);

    rt.Coroutines().NotifyEvent("other_event", sol::make_object(lua, 5));
    rt.UpdateCoroutines(0.016f);                  // 다른 이벤트 → 재개 안 함
    MYE_EXPECT(int(lua["_ce"]["phase"]) == 1);

    rt.Coroutines().NotifyEvent("door_opened", sol::make_object(lua, 77));
    rt.UpdateCoroutines(0.016f);                  // 대기 이벤트 도착 → 재개
    MYE_EXPECT(int(lua["_ce"]["phase"]) == 2);
    MYE_EXPECT(int(lua["_ce"]["got"]) == 77);     // payload 전달 확인
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);
}

// ===========================================================================
// 코루틴 Start 첫 재개 에러 — 즉시 실패해도 CoResumeError 로 surface 되어야 함(유실 금지)
// ===========================================================================
MYE_TEST(ScriptCoroutineStartErrorSurfaced) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    int errorEvents = 0;
    bus.Subscribe<ScriptErrorEvent>([&](const ScriptErrorEvent&) { ++errorEvents; return false; });

    // 코루틴 본체가 첫 재개에서 곧바로 에러(nil 인덱싱).
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            local x = nil
            return x.field
        end)
    )LUA", "co_err.lua");
    MYE_EXPECT(bool(r));

    // 첫 Tick 에서 등록된 실패 태스크가 CoResumeError → ScriptErrorEvent 로 발행.
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(errorEvents == 1);
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);   // 종료 처리(제거)
}

// ===========================================================================
// 코루틴 NotifyEvent payload 큐 — 한 tick 사이 같은 이벤트 2회 도착 시 payload 유실 없음
// ===========================================================================
MYE_TEST(ScriptCoroutineEventPayloadQueue) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    (void)rt.DoString("_pq = { seen = {} }", "test_setup");
    // 매번 wait_event("ping") 하고 받은 payload 를 기록하는 루프 코루틴.
    auto r = rt.DoString(R"LUA(
        mye.co.start(function()
            for i = 1, 3 do
                local p = mye.co.wait_event("ping")
                _pq.seen[#_pq.seen + 1] = p
            end
        end)
    )LUA", "pq.lua");
    MYE_EXPECT(bool(r));

    sol::state& lua = rt.State();

    // 같은 tick 사이 두 번 통지 — payload 10, 20 이 큐에 쌓여야 한다(덮어쓰지 않음).
    rt.Coroutines().NotifyEvent("ping", sol::make_object(lua, 10));
    rt.Coroutines().NotifyEvent("ping", sol::make_object(lua, 20));
    rt.UpdateCoroutines(0.016f);   // 첫 재개: 10 소비 후 다시 wait_event → 남은 20 재무장
    rt.UpdateCoroutines(0.016f);   // 두 번째 재개: 20 소비

    MYE_EXPECT(int(lua["_pq"]["seen"][1]) == 10);
    MYE_EXPECT(int(lua["_pq"]["seen"][2]) == 20);   // 두 번째 payload 유실 없음

    // 세 번째는 아직 도착 안 함 → 계속 대기.
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 1);
    rt.Coroutines().NotifyEvent("ping", sol::make_object(lua, 30));
    rt.UpdateCoroutines(0.016f);
    MYE_EXPECT(int(lua["_pq"]["seen"][3]) == 30);
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);
}

// ===========================================================================
// 엔티티 파괴 — on_destroy 호출 + 소유 코루틴 취소(엔티티보다 오래 살지 않음)
// ===========================================================================
MYE_TEST(ScriptOnDestroyAndCoroutineCancel) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, nullptr);
    ss.RegisterComponent();

    (void)rt.DoString("_od = { destroyed = 0 }", "test_setup");

    ecs::Entity e = world.Create();
    world.Add<ScriptComponent>(e);
    // on_start 에서 무한 대기 코루틴 시작(엔티티 소유). on_destroy 는 카운터 증가.
    const char* src = R"LUA(
        local C = {}
        function C:on_start()
            mye.co.start_for(self.entity, function()
                while true do mye.co.wait_event("never") end
            end)
        end
        function C:on_destroy() _od.destroyed = _od.destroyed + 1 end
        return C
    )LUA";
    MYE_EXPECT(InstallScript(rt, world, e, src, "od.lua"));

    ss.Update(0.016f);   // on_start → 코루틴 시작·추적 등록
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 1);

    // 엔티티 파괴 후 다음 Update 에서 감지 → on_destroy + 코루틴 취소.
    world.Destroy(e);
    ss.Update(0.016f);

    sol::state& lua = rt.State();
    MYE_EXPECT(int(lua["_od"]["destroyed"]) == 1);      // on_destroy 1회
    MYE_EXPECT(rt.Coroutines().ActiveCount() == 0);     // 소유 코루틴 취소됨

    // 재호출 없음(추적에서 제거됨).
    ss.Update(0.016f);
    MYE_EXPECT(int(lua["_od"]["destroyed"]) == 1);
}

// ===========================================================================
// 핫 리로드 — self.state 생존 + on_hot_reload 호출 + on_init 재호출 안 함
// ===========================================================================
MYE_TEST(ScriptHotReloadPreservesState) {
    auto dir = MakeTempDir();
    const char* v1 = R"LUA(
        local C = {}
        function C:on_init() self.state.hp = 100; self.state.inits = (self.state.inits or 0) + 1 end
        function C:on_update(dt) end
        function C:version() return 1 end
        return C
    )LUA";
    WriteText(dir / "npc.lua", v1);

    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(dir.string()), 0);
    EventBus bus;
    asset::AssetManager mgr(vfs, /*device=*/nullptr);
    mgr.RegisterImporter(std::make_unique<ScriptImporter>());

    auto handle = mgr.LoadSync<ScriptAsset>("assets://npc.lua");
    MYE_EXPECT(handle.State() == asset::AssetState::Loaded);

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), &bus, &mgr);

    ecs::World world;
    world.SetEventBus(&bus);
    ScriptSystem ss(rt, world, &bus, &mgr);
    ss.RegisterComponent();

    ecs::Entity e = world.Create();
    auto& sc = world.Add<ScriptComponent>(e);
    sc.script = handle;

    ss.EnsureInstances();   // 인스턴스화 + on_init(hp=100, inits=1)

    ScriptComponent* comp = world.TryGet<ScriptComponent>(e);
    MYE_EXPECT(comp && comp->instance.valid());
    {
        sol::table inst = comp->instance;
        sol::table state = inst["state"];
        MYE_EXPECT(int(state["hp"]) == 100);
        MYE_EXPECT(int(state["inits"]) == 1);
        // 인스턴스에 런타임 변경을 남긴다(리로드 후 생존 검증용).
        state["hp"] = 42;
    }

    // v2: on_hot_reload 추가·version 변경. 파일 재기록 후 reimport(슬롯 스왑).
    const char* v2 = R"LUA(
        local C = {}
        function C:on_init() self.state.hp = 100; self.state.inits = (self.state.inits or 0) + 1 end
        function C:on_update(dt) end
        function C:on_hot_reload() self.state.reloaded = true end
        function C:version() return 2 end
        return C
    )LUA";
    WriteText(dir / "npc.lua", v2);

    asset::AssetDatabase db(mgr, &bus);
    auto rr = mgr.ReimportPath("assets://npc.lua");
    MYE_EXPECT(rr.guid.IsValid());

    // 새 소스가 슬롯에 반영됐는지 확인.
    MYE_EXPECT(handle.Get() && handle.Get()->source.find("version() return 2") != std::string::npos);

    ss.HotReload(rr.guid);

    ScriptComponent* comp2 = world.TryGet<ScriptComponent>(e);
    MYE_EXPECT(comp2 && !comp2->hasError);
    if (comp2) {
        sol::table inst = comp2->instance;
        sol::table state = inst["state"];
        // self.state 생존: hp 는 리로드에서 42 유지(on_init 재호출 안 됨 → 100 으로 안 덮임).
        MYE_EXPECT(int(state["hp"]) == 42);
        MYE_EXPECT(int(state["inits"]) == 1);
        // on_hot_reload 호출 확인.
        MYE_EXPECT(bool(state["reloaded"]) == true);
        // 클래스 스왑 확인: version() == 2.
        sol::protected_function ver = inst["version"];
        sol::protected_function_result vr = ver(inst);
        MYE_EXPECT(vr.valid() && int(vr) == 2);
    }

    std::error_code ec; std::filesystem::remove_all(dir, ec);
}

// ===========================================================================
// io/os/package 은닉 — 기본 정책에서 위험 표면 접근 불가
// ===========================================================================
MYE_TEST(ScriptStdLibHardening) {
    ScriptRuntime rt;
    EventBus bus;
    rt.Initialize(DefaultPolicy(), &bus, nullptr);
    sol::state& lua = rt.State();

    MYE_EXPECT(!lua["io"].valid());
    MYE_EXPECT(!lua["os"].valid());
    MYE_EXPECT(!lua["require"].valid());
    MYE_EXPECT(!lua["package"].valid());
    MYE_EXPECT(!lua["load"].valid());
    MYE_EXPECT(!lua["loadstring"].valid());
    // 안전 표준은 살아있어야 함.
    MYE_EXPECT(lua["math"].valid());
    MYE_EXPECT(lua["string"].valid());
    MYE_EXPECT(lua["table"].valid());
}
