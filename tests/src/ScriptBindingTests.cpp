// ScriptBindingTests.cpp — 엔진 API Lua 바인딩 단위 테스트 (05 M3-C)
//
// 각 바인딩이 Lua 에서 호출되어 C++ 상태를 올바르게 읽고 바꾸는지 검증한다:
//   - Vec2 연산자·length·normalize·dot.
//   - ECS: 엔티티 Transform 위치·KinematicBody2D 속도·SpriteAnimator 파라미터 set/get.
//   - Input: is_down / was_pressed / move_axis 가 InputState 를 반영.
//   - Audio: set_bus_volume 이 AudioEngine 버스 볼륨을 바꾼다(헤드리스/무음 모드).
//   - Events: mye.events.on/emit 왕복.
//   - 안전: 잘못된 인자에 크래시 대신 Lua 에러(DoString 이 Expected 에러 반환).
#include "TestFramework.h"

#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioTypes.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/core/Input.h"
#include "mye/core/InputActions.h"
#include "mye/core/Math.h"
#include "mye/ecs/World.h"
#include "mye/phys/Collision.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera2D.h"
#include "mye/scene/SceneReflection.h"
#include "mye/script/ScriptRuntime.h"
#include "mye/script/bindings/EngineBindings.h"
#include "mye/runtime/RuntimeBindings.h"

#include "LuaTest.h"

using namespace mye;
using namespace mye::script;

namespace {

// 표준 정책 + 지정 모듈로 초기화된 런타임을 구성하는 헬퍼.
StdLibPolicy DefaultPolicy() {
    StdLibPolicy p;   // base/table/string/math/coroutine/utf8 기본 on.
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------
MYE_TEST(ScriptMathVec2) {
    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());

    lua_State* lua = rt.State();

    // 생성·필드.
    Vec2 a = luatest::Eval<Vec2>(lua, "return mye.Vec2(3, 4)");
    MYE_EXPECT(a.x == 3.0f && a.y == 4.0f);

    // 연산자.
    Vec2 sum = luatest::Eval<Vec2>(lua, "return mye.Vec2(1,2) + mye.Vec2(10,20)");
    MYE_EXPECT(sum == (Vec2{11.0f, 22.0f}));
    Vec2 scaled = luatest::Eval<Vec2>(lua, "return mye.Vec2(2,3) * 2.0");
    MYE_EXPECT(scaled == (Vec2{4.0f, 6.0f}));
    Vec2 rscaled = luatest::Eval<Vec2>(lua, "return 2.0 * mye.Vec2(2,3)");
    MYE_EXPECT(rscaled == (Vec2{4.0f, 6.0f}));

    // length / normalized / dot.
    float len = luatest::Eval<float>(lua, "return mye.Vec2(3,4):length()");
    MYE_EXPECT_NEAR(len, 5.0f, 1e-4f);
    Vec2 n = luatest::Eval<Vec2>(lua, "return mye.Vec2(0,8):normalized()");
    MYE_EXPECT_NEAR(n.x, 0.0f, 1e-4f);
    MYE_EXPECT_NEAR(n.y, 1.0f, 1e-4f);
    float d = luatest::Eval<float>(lua, "return mye.Vec2(1,0):dot(mye.Vec2(0,1))");
    MYE_EXPECT_NEAR(d, 0.0f, 1e-4f);

    // Color / Rect 기본.
    bool okColor = luatest::Eval<bool>(lua, "local c = mye.Color(1,0,0,1); return c.r == 1.0 and c.a == 1.0");
    MYE_EXPECT(okColor);
    bool okRect = luatest::Eval<bool>(lua,
        "local r = mye.Rect(0,0,10,10); return r:contains(mye.Vec2(5,5))");
    MYE_EXPECT(okRect);
}

// ---------------------------------------------------------------------------
// ECS — 컴포넌트 set/get
// ---------------------------------------------------------------------------
MYE_TEST(ScriptEcsComponents) {
    ecs::World world;

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());
    rt.AddBindingModule(std::make_unique<EcsBindingModule>(&world));

    // 엔티티 + 컴포넌트 준비.
    ecs::Entity e = world.Create();
    world.Add<scene::LocalTransform>(e);
    world.Add<phys::KinematicBody2D>(e);
    world.Add<anim::SpriteAnimator>(e);

    lua_State* lua = rt.State();

    // LuaEntity 를 전역으로 주입(엔진이 핸들 발급하는 경로 모사).
    // usertype 은 EcsBindingModule 이 등록했으므로 mye.world.entity_from_packed 로 구성.
    luatest::SetInteger(lua, "_entity", static_cast<lua_Integer>(e.Packed()));
    luatest::Eval(lua, "ent = mye.world.entity_from_packed(_entity)");

    // Transform 위치 set → C++ 확인.
    luatest::Eval(lua, "ent:set_position(mye.Vec2(5, 7))");
    auto* lt = world.TryGet<scene::LocalTransform>(e);
    MYE_EXPECT(lt && lt->position.x == 5.0f && lt->position.y == 7.0f);
    MYE_EXPECT(lt && lt->dirty);

    // get_position 왕복.
    Vec2 got = luatest::Eval<Vec2>(lua, "return ent:get_position()");
    MYE_EXPECT(got == (Vec2{5.0f, 7.0f}));

    // 속도 set/get.
    luatest::Eval(lua, "ent:set_velocity(mye.Vec2(-2, 3))");
    auto* body = world.TryGet<phys::KinematicBody2D>(e);
    MYE_EXPECT(body && body->velocity == (Vec2{-2.0f, 3.0f}));

    // 애니메이터 파라미터 set → C++ 확인.
    luatest::Eval(lua, "ent:set_bool('isMoving', true)");
    luatest::Eval(lua, "ent:set_float('speed', 2.5)");
    auto* anim = world.TryGet<anim::SpriteAnimator>(e);
    MYE_EXPECT(anim && anim->GetBool("isMoving"));
    MYE_EXPECT(anim && anim->GetFloat("speed") == 2.5f);

    // Lua 에서 get_* 왕복.
    bool moving = luatest::Eval<bool>(lua, "return ent:get_bool('isMoving')");
    MYE_EXPECT(moving);

    // facing: 이동 벡터로 갱신(오른쪽 → Dir8::Right=6).
    luatest::Eval(lua, "ent:face_move(mye.Vec2(1, 0))");
    MYE_EXPECT(anim && anim->facing == mye::anim::Dir8::Right);
    int idx = luatest::Eval<int>(lua, "return ent:facing_index()");
    MYE_EXPECT(idx == static_cast<int>(mye::anim::Dir8::Right));

    // 존재 질의.
    bool hasAnim = luatest::Eval<bool>(lua, "return ent:has_animator()");
    MYE_EXPECT(hasAnim);

    // is_valid.
    bool valid = luatest::Eval<bool>(lua, "return ent:is_valid()");
    MYE_EXPECT(valid);
}

// ---------------------------------------------------------------------------
// ECS — spawn/destroy(CommandBuffer 경유 + FlushDeferred)
// ---------------------------------------------------------------------------
MYE_TEST(ScriptEcsSpawnDestroy) {
    ecs::World world;

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());

    auto ecsMod = std::make_unique<EcsBindingModule>(&world);
    EcsBindingModule* ecsRaw = ecsMod.get();
    rt.AddBindingModule(std::move(ecsMod));

    lua_State* lua = rt.State();

    // spawn 은 즉시 유효 핸들 반환(예약). Flush 전이라도 Valid 여야 한다(CreateDeferred 계약).
    luatest::Eval(lua, "spawned = mye.world.spawn()");
    bool validBeforeFlush = luatest::Eval<bool>(lua, "return spawned:is_valid()");
    MYE_EXPECT(validBeforeFlush);

    ecsRaw->FlushDeferred();   // 앱/ScriptSystem 이 페이즈 경계에서 호출하는 경로.

    bool validAfterFlush = luatest::Eval<bool>(lua, "return spawned:is_valid()");
    MYE_EXPECT(validAfterFlush);

    // destroy → flush → 무효.
    luatest::Eval(lua, "mye.world.destroy(spawned)");
    ecsRaw->FlushDeferred();
    bool validAfterDestroy = luatest::Eval<bool>(lua, "return spawned:is_valid()");
    MYE_EXPECT(!validAfterDestroy);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
MYE_TEST(ScriptInput) {
    InputState input;

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());
    rt.AddBindingModule(std::make_unique<InputBindingModule>(&input));

    lua_State* lua = rt.State();

    // 초기: 아무 키도 안 눌림.
    bool downInit = luatest::Eval<bool>(lua, "return mye.input.is_down(mye.Key.W)");
    MYE_EXPECT(!downInit);

    // D 를 누른 상태로 만든다(엣지 관측 위해 NewFrame 후 OnKey).
    input.NewFrame();
    input.OnKey(KeyCode::D, true);

    bool downD = luatest::Eval<bool>(lua, "return mye.input.is_down(mye.Key.D)");
    MYE_EXPECT(downD);
    bool pressedD = luatest::Eval<bool>(lua, "return mye.input.was_pressed(mye.Key.D)");
    MYE_EXPECT(pressedD);

    // move_axis(A, D, S, W): D 만 눌림 → x = +1.
    Vec2 axis = luatest::Eval<Vec2>(lua,
        "return mye.input.move_axis(mye.Key.A, mye.Key.D, mye.Key.S, mye.Key.W)");
    MYE_EXPECT(axis.x == 1.0f && axis.y == 0.0f);

    // 잘못된 키 정수(범위 밖)는 크래시 없이 false.
    bool bad = luatest::Eval<bool>(lua, "return mye.input.is_down(99999)");
    MYE_EXPECT(!bad);
}

// ---------------------------------------------------------------------------
// Audio — 버스 볼륨(무음 모드)
// ---------------------------------------------------------------------------
MYE_TEST(ScriptAudioBus) {
    audio::AudioEngine engine;
    MYE_EXPECT(engine.Initialize(nullptr).HasValue());   // 무음 모드(백엔드 없음).

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<AudioBindingModule>(&engine));

    lua_State* lua = rt.State();

    luatest::Eval(lua, "mye.audio.set_bus_volume(mye.Bus.SFX, 0.25)");
    MYE_EXPECT_NEAR(engine.GetBusVolume(audio::BusId::SFX), 0.25f, 1e-4f);

    float roundtrip = luatest::Eval<float>(lua, "return mye.audio.get_bus_volume(mye.Bus.SFX)");
    MYE_EXPECT_NEAR(roundtrip, 0.25f, 1e-4f);

    // set_listener 는 크래시 없이 수행(공간화 기준). 값 확인.
    luatest::Eval(lua, "mye.audio.set_listener(3, 4)");
    MYE_EXPECT(engine.GetListener() == (Vec2{3.0f, 4.0f}));

    // 리졸버 미설치 시 play_cue 는 안전 no-op(크래시 없음).
    auto r = rt.DoString("mye.audio.play_cue('nonexistent')", "audio_test");
    MYE_EXPECT(r.HasValue());

    engine.Shutdown();
}

// ---------------------------------------------------------------------------
// Audio — 큐 리졸버 경유 재생
// ---------------------------------------------------------------------------
MYE_TEST(ScriptAudioCueResolver) {
    audio::AudioEngine engine;
    MYE_EXPECT(engine.Initialize(nullptr).HasValue());

    // 간단한 큐(빈 클립 목록 — PostCue 는 유효성 검사로 no-op 이지만 리졸버·경계는 검증).
    audio::AudioCue cue;
    cue.bus = audio::BusId::SFX;

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    auto audioMod = std::make_unique<AudioBindingModule>(&engine);
    int resolveCount = 0;
    audioMod->SetCueResolver([&](std::string_view name) -> const audio::AudioCue* {
        ++resolveCount;
        return name == "footstep" ? &cue : nullptr;
    });
    rt.AddBindingModule(std::move(audioMod));

    auto r = rt.DoString("mye.audio.play_cue('footstep', 1.0, 2.0)", "cue_test");
    MYE_EXPECT(r.HasValue());
    MYE_EXPECT(resolveCount == 1);

    engine.Shutdown();
}

// ---------------------------------------------------------------------------
// Events — on/emit 왕복
// ---------------------------------------------------------------------------
MYE_TEST(ScriptEvents) {
    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<EventBindingModule>());

    lua_State* lua = rt.State();

    int captured = luatest::Eval<int>(lua, R"(
        local got = 0
        mye.events.on('score', function(amount) got = got + amount end)
        mye.events.emit('score', 10)
        mye.events.emit('score', 5)
        return got
    )");
    MYE_EXPECT(captured == 15);

    // off 후 더 이상 수신 안 함.
    int afterOff = luatest::Eval<int>(lua, R"(
        local got = 0
        local id = mye.events.on('tick', function() got = got + 1 end)
        mye.events.emit('tick')
        mye.events.off('tick', id)
        mye.events.emit('tick')
        return got
    )");
    MYE_EXPECT(afterOff == 1);
}

// ---------------------------------------------------------------------------
// 안전 — 잘못된 인자는 크래시 대신 Lua 에러
// ---------------------------------------------------------------------------
MYE_TEST(ScriptSafeErrors) {
    ecs::World world;

    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());
    rt.AddBindingModule(std::make_unique<EcsBindingModule>(&world));

    // Vec2 생성자에 문자열 전달 → Lua 타입 에러(DoString Expected 에러). 프로세스는 계속.
    auto r1 = rt.DoString("return mye.Vec2('bad', 'args')", "err1");
    MYE_EXPECT(!r1.HasValue());

    // 존재하지 않는 메서드 호출 → Lua 에러.
    auto r2 = rt.DoString("return mye.Vec2(1,2):no_such_method()", "err2");
    MYE_EXPECT(!r2.HasValue());

    // 정상 스크립트는 성공(에러 격리가 이후 실행을 막지 않음).
    auto r3 = rt.DoString("return mye.Vec2(1,2).x", "ok");
    MYE_EXPECT(r3.HasValue());
}

MYE_TEST(ScriptNamedInputReadsOneConsumedTickWithoutMutatingIt) {
    InputState input;
    InputActions actions;
    MYE_EXPECT(actions.Configure({{{"attack", .2f, {{InputDevice::Key, static_cast<int>(KeyCode::F)}}},
                                  {"right", .2f, {{InputDevice::Key, static_cast<int>(KeyCode::D)}}},
                                  {"up", .2f, {{InputDevice::Key, static_cast<int>(KeyCode::W)}}}}}));
    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());
    auto binding = std::make_unique<InputBindingModule>(&input);
    auto* api = binding.get();
    rt.AddBindingModule(std::move(binding));
    MYE_EXPECT(rt.DoString("assert(not mye.input.is_action_just_pressed('attack'))", "no-tick.lua"));
    input.NewFrame(); input.OnKey(KeyCode::F, true); input.OnKey(KeyCode::F, false);
    input.OnKey(KeyCode::D, true); input.OnKey(KeyCode::W, true);
    actions.Capture(input); actions.ConsumeTick(); api->SetActions(&actions);
    for (int i = 0; i < 2; ++i) MYE_EXPECT(rt.DoString(R"(
        assert(mye.input.is_action_just_pressed('attack'))
        assert(mye.input.is_action_just_released('attack'))
        assert(not mye.input.is_action_pressed('attack'))
        assert(mye.input.get_action_strength('attack') == 0)
        assert(mye.input.get_action_raw_strength('right') == 1)
        local vector = mye.input.get_vector('left', 'right', 'down', 'up')
        assert(math.abs(vector:length() - 1) < .00001 and vector.x > 0 and vector.y > 0)
        assert(not mye.input.is_action_pressed('missing'))
        assert(mye.input.get_action_strength('missing') == 0)
    )", "one-tick.lua"));
    actions.ConsumeTick();
    MYE_EXPECT(rt.DoString("assert(not mye.input.is_action_just_pressed('attack') and mye.input.is_action_pressed('right'))", "next-tick.lua"));
    MYE_EXPECT(!rt.DoString("mye.input.is_action_pressed(1)", "bad-name.lua"));
    MYE_EXPECT(!rt.DoString("mye.input.get_action_strength(string.rep('a',65))", "long-name.lua"));
    MYE_EXPECT(!rt.DoString("mye.input.get_vector('left','right','down',nil)", "bad-vector.lua"));
    MYE_EXPECT(rt.DoString("assert(not mye.input.is_action_pressed('right\\0other'))", "embedded-null.lua"));
    api->SetActions(nullptr);
    MYE_EXPECT(rt.DoString("assert(mye.input.get_action_strength('right') == 0 and mye.input.get_vector('left','right','down','up') == mye.Vec2())", "cleared-tick.lua"));
}

MYE_TEST(ScriptNativeArgumentBounds) {
    InputState input;
    ScriptRuntime runtime;
    runtime.Initialize({}, nullptr, nullptr);
    runtime.AddBindingModule(std::make_unique<MathBindingModule>());
    runtime.AddBindingModule(std::make_unique<InputBindingModule>(&input));
    runtime.AddBindingModule(std::make_unique<runtime::RuntimeBindings>(nullptr, nullptr, nullptr, nullptr, nullptr));
    MYE_EXPECT(luatest::Eval<bool>(runtime.State(), R"(
        return not mye.input.pad_connected(4294967296)
           and not mye.input.pad_down(mye.Pad.A, 4294967296)
           and mye.input.left_stick(4294967296) == mye.Vec2()
    )"));
    // Large values must fail before narrowing into a different valid slot/index.
    MYE_EXPECT(!runtime.DoString("mye.save.write(4294967296)", "slot.lua").HasValue());
    MYE_EXPECT(!runtime.DoString("mye.dialogue.pick(4294967296)", "choice.lua").HasValue());
    MYE_EXPECT(runtime.DoString("assert(mye.save.write(0) == false)", "missing-service.lua").HasValue());
}

MYE_TEST(ScriptTwoDCameraZoomShakeAndLogicalCoordinates) {
    ecs::World world;
    scene::RegisterCoreComponents(world);
    const auto entity = world.Create();
    world.Add<scene::LocalTransform>(entity).position = {2, 3, 0};
    world.Add<scene::WorldTransform>(entity);
    auto& camera = world.Add<scene::Camera2D>(entity);
    scene::UpdateWorldTransforms(world);
    MYE_EXPECT(scene::UpdateGameCamera2D(world, 0));
    ScriptRuntime rt;
    rt.Initialize(DefaultPolicy(), nullptr, nullptr);
    rt.AddBindingModule(std::make_unique<MathBindingModule>());
    rt.AddBindingModule(std::make_unique<EcsBindingModule>(&world));
    luatest::SetInteger(rt.State(), "_camera", static_cast<lua_Integer>(entity.Packed()));
    MYE_EXPECT(rt.DoString(R"(
        camera = mye.world.entity_from_packed(_camera)
        camera:set_camera_zoom(2)
        camera:shake_camera(.2, .5)
        local point = mye.Vec2(3, 4)
        local pixel = camera:world_to_screen(point)
        assert(pixel == mye.Vec2(576, 174))
        assert(camera:screen_to_world(pixel) == point)
    )", "camera.lua"));
    MYE_EXPECT(scene::UpdateGameCamera2D(world, .1f));
    MYE_EXPECT(camera.zoom == 2 && camera.view.IsShaking());
    const auto position = camera.view.Position();
    for (const auto* source : {"camera:set_camera_zoom(0/0)", "camera:set_camera_zoom(9)",
        "camera:shake_camera(-1, 1)", "camera:shake_camera(1, math.huge)",
        "camera:world_to_screen(mye.Vec2(0/0, 0))"})
        MYE_EXPECT(!rt.DoString(source, "bad-camera.lua"));
    MYE_EXPECT(camera.zoom == 2 && camera.view.Position() == position);
    world.Remove<scene::WorldTransform>(entity);
    MYE_EXPECT(!rt.DoString("camera:screen_to_world(mye.Vec2())", "missing-transform.lua"));
    world.Destroy(entity);
    MYE_EXPECT(!rt.DoString("camera:set_camera_zoom(1)", "destroyed-camera.lua"));
}

MYE_TEST(ScriptAnimationStateParametersRefuseWrongNamesTypesAndNonfiniteValues) {
    ecs::World world; auto entity = world.Create(); auto& animator = world.Add<anim::SpriteAnimator>(entity);
    animator.stateMachine.guid = {1, 1};
    animator.params = {{"moving", anim::ParamType::Bool, 0}, {"speed", anim::ParamType::Float, .375f},
                       {"attack", anim::ParamType::Trigger, 0}};
    ScriptRuntime runtime; runtime.Initialize(DefaultPolicy(), nullptr, nullptr);
    runtime.AddBindingModule(std::make_unique<EcsBindingModule>(&world));
    luatest::SetInteger(runtime.State(), "_entity", static_cast<lua_Integer>(entity.Packed()));
    MYE_EXPECT(runtime.DoString("ent=mye.world.entity_from_packed(_entity); ent:set_bool('moving',true); ent:set_float('speed',2); ent:set_trigger('attack')","animation-state-check"));
    MYE_EXPECT(animator.GetBool("moving") && animator.GetFloat("speed") == 2 && animator.GetBool("attack"));
    for (const auto* source : {"ent:set_bool('missing',true)", "ent:set_float('moving',1)",
        "ent:set_bool('attack',true)", "ent:set_trigger('speed')", "ent:get_float('moving')", "ent:get_bool('speed')",
        "ent:set_float('speed',0/0)", "ent:set_float('speed',math.huge)", "ent:set_float('speed',1e100)", "ent:set_float('speed',-1e100)",
        "ent:set_bool('',true)", "ent:set_bool(3,true)", "ent:set_bool(string.rep('x',65),true)",
        "ent:set_bool('moving'..string.char(0),true)"}) MYE_EXPECT(!runtime.DoString(source,"animation-state-check"));
    MYE_EXPECT(animator.params.size() == 3 && animator.GetFloat("speed") == 2 && animator.GetBool("moving"));
    MYE_EXPECT(runtime.DoString("assert(ent:get_bool('attack')); assert(ent:get_float('speed')==2)","animation-state-check"));
    animator.stateMachine.guid = {};
    MYE_EXPECT(runtime.DoString("ent:set_bool('legacy',true)","animation-state-check")); MYE_EXPECT(animator.GetBool("legacy"));
}
