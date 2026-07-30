// mye/script/bindings/EngineBindings.h — 엔진 API Lua 바인딩 모듈들 (docs/05 §Binding 계층, M3-C)
//
// 각 바인딩 모듈은 IBindingModule 구현체로, sol::state 에 usertype·함수를 등록한다.
//   - 네이밍: 함수·필드 snake_case, 타입 PascalCase(mye.Vec2), 상수 mye.Key.SPACE (docs/05).
//   - 계약(IBindingModule): Register 는 매 VM (재)생성마다 호출되므로 sol 객체를 멤버로 저장 금지.
//     대신 엔진 서비스 원시 포인터(World·InputState·AudioEngine·EventBus)는 앱 수명 동안 안정적이므로
//     생성자로 주입해 보관한다(비소유). Lua 클로저가 이 포인터를 캡처해 C++ 상태에 접근한다.
//   - 안전: 잘못된 인자·nullptr 접근은 크래시 대신 Lua 에러(sol2 경계 예외 허용, /EHsc).
//
// 이 헤더는 sol/forward.hpp 만 포함(무거운 sol.hpp 격리). 구현 .cpp 가 <sol/sol.hpp> 를 포함한다.
#pragma once

#include "mye/script/IBindingModule.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace mye {
class EventBus;
class InputState;
}
namespace mye::ecs { class World; class CommandBuffer; }
namespace mye::audio { class AudioEngine; struct AudioCue; }
namespace mye::asset { struct AudioClip; }
namespace mye::ddc { class SchemaRegistry; class DynamicComponentStore; }
namespace sol { class state; }

namespace mye::script {

// ---------------------------------------------------------------------------
// MathBindingModule — mye.Vec2 / mye.Color / mye.Rect 값 usertype + 연산자·헬퍼.
//   핫패스 값 타입이므로 usertype(value semantics). 서비스 의존 없음.
// ---------------------------------------------------------------------------
class MathBindingModule final : public IBindingModule {
public:
    std::string_view Name() const override { return "math"; }
    void Register(sol::state& lua) override;
};

// ---------------------------------------------------------------------------
// EcsBindingModule — 엔티티 핸들 + 주요 컴포넌트 get/set(Transform·SpriteAnimator·
//   KinematicBody2D) + 스폰/파괴(CommandBuffer 경유). World 는 비소유 주입.
//   world 는 앱 수명 동안 안정적이라고 가정(게임 스테이트 1개, docs/05).
// ---------------------------------------------------------------------------
class EcsBindingModule final : public IBindingModule {
public:
    explicit EcsBindingModule(ecs::World* world);
    ~EcsBindingModule() override;
    std::string_view Name() const override { return "ecs"; }
    void Register(sol::state& lua) override;

    // 스크립트가 mye.world.spawn/destroy 로 기록한 지연 구조변경을 반영한다.
    //   앱/ScriptSystem 이 Update 페이즈 실행 직후(순회 종료 후) 매 프레임 호출해야 한다.
    void FlushDeferred();

private:
    ecs::World* m_world;   // 비소유
    // 지연 구조변경 버퍼(모듈 소유). CommandBuffer 는 sol 비의존이므로 여기서 보관 가능.
    std::unique_ptr<ecs::CommandBuffer> m_commands;
};

// ---------------------------------------------------------------------------
// InputBindingModule — mye.input.is_down / was_pressed / was_released + 이동축 헬퍼.
//   물리 KeyCode 상수(mye.Key.*) 노출. InputState 는 비소유 주입(nullptr 허용=폴백).
// ---------------------------------------------------------------------------
class InputBindingModule final : public IBindingModule {
public:
    explicit InputBindingModule(const InputState* input) : m_input(input) {}
    std::string_view Name() const override { return "input"; }
    void Register(sol::state& lua) override;

private:
    const InputState* m_input;   // 비소유
};

// ---------------------------------------------------------------------------
// AudioBindingModule — mye.audio.play_cue / play_music / set_bus_volume / set_listener.
//   AudioEngine 은 비소유 주입(nullptr 허용=no-op). 큐 이름→AudioCue 는 데모/앱이 레지스트리로
//   제공(여기선 클립 핸들·직접 재생·볼륨 표면만; 큐 레지스트리 훅은 아래 SetCueResolver).
// ---------------------------------------------------------------------------
class AudioBindingModule final : public IBindingModule {
public:
    // 큐/음악은 이름으로 재생한다(게임 디자이너 친화). 이름→리소스 해석은 앱/데모가 등록하는
    //   리졸버가 담당한다(엔진은 큐 레지스트리 비소유 — 데모가 발소리/BGM 을 배선, docs/05 M3-C).
    using CueResolver  = std::function<const audio::AudioCue*(std::string_view)>;
    using ClipResolver = std::function<const asset::AudioClip*(std::string_view)>;

    explicit AudioBindingModule(audio::AudioEngine* engine) : m_engine(engine) {}
    std::string_view Name() const override { return "audio"; }
    void Register(sol::state& lua) override;

    // 리졸버 설치(앱/데모 배선). 미설치 시 play_cue/play_music 은 안전 no-op.
    void SetCueResolver(CueResolver resolver) { m_cueResolver = std::move(resolver); }
    void SetClipResolver(ClipResolver resolver) { m_clipResolver = std::move(resolver); }

private:
    audio::AudioEngine* m_engine;   // 비소유
    CueResolver  m_cueResolver;
    ClipResolver m_clipResolver;
};

// ---------------------------------------------------------------------------
// EventBindingModule — mye.events.emit / on (월드 버스 커스텀 이벤트 발행·구독).
//   Lua↔Lua 커스텀 이벤트(문자열 이름 + Lua 값 페이로드)를 자체 디스패치 테이블로 라우팅.
//   C++ 이벤트(AnimationEvent·Trigger*)는 ScriptSystem 이 on_event 로 라우팅하므로 여기선
//   Lua 정의 이벤트만 담당한다(01 raw 버스의 POD 페이로드 자동 변환은 후속 — docs/05).
//   구독 콜백은 VM 수명. Register 마다 새 디스패처를 만든다(sol 객체 저장 금지 계약).
// ---------------------------------------------------------------------------
class EventBindingModule final : public IBindingModule {
public:
    std::string_view Name() const override { return "events"; }
    void Register(sol::state& lua) override;
};

// ---------------------------------------------------------------------------
// ReflectBindingModule — 리플렉션 기반 범용 바인딩. 손으로 쓴 per-타입 바인딩 없이
//   레지스트리에 등록된 임의 리플렉션 타입을 Lua 에서 생성·조작한다(엔진 확장성 핵심):
//     local s = mye.reflect.new("plugintest::Buff")   -- 등록된 타입 인스턴스 생성
//     s:set("power", 10);  local p = s:get("power")     -- 필드 get/set(원시형)
//     local hp = s:call("Damage", 5)                    -- 메서드 호출(MethodInfo.Invoke)
//   플러그인/게임이 GetType<T>() 로 타입만 등록하면 즉시 Lua 에서 다룰 수 있다.
//   서비스 의존 없음(TypeRegistry 전역).
// ---------------------------------------------------------------------------
class ReflectBindingModule final : public IBindingModule {
public:
    std::string_view Name() const override { return "reflect"; }
    void Register(sol::state& lua) override;
};

// ---------------------------------------------------------------------------
// DdcBindingModule — 데이터드리븐 컴포넌트 Lua 바인딩(플러그인·Lua·데이터 삼위일체 완성):
//     mye.ddc.define(jsonString)                 -- 스키마를 데이터로 정의(런타임)
//     local c = mye.ddc.new("Stats")             -- 독립 인스턴스(값)
//     c:set("hp", 70);  local hp = c:get("hp")   -- 필드 get/set(타입드)
//   [엔티티 부착·시스템] 게임 로직 전체를 데이터+Lua 로:
//     mye.ddc.attach(entityId, "Health")         -- 엔티티에 컴포넌트 부착(참조 핸들)
//     mye.system("Health", function(e, c) ... end)  -- 컴포넌트별 per-frame 시스템(Lua)
//   모듈이 SchemaRegistry + DynamicComponentStore 소유. Tick(dt) 이 Lua 시스템을 스토어 위에서 실행.
//   [주의] throw 금지(ReflectBindings 와 동일) — 오류는 nil/false/no-op.
// ---------------------------------------------------------------------------
class DdcBindingModule final : public IBindingModule {
public:
    DdcBindingModule();
    ~DdcBindingModule() override;
    std::string_view Name() const override { return "ddc"; }
    void Register(sol::state& lua) override;

    // 등록된 Lua 시스템을 스토어의 해당 컴포넌트 엔티티마다 실행(매 프레임 앱/ScriptSystem 호출).
    void Tick(float dt);

    // 앱/플러그인이 미리 로드할 수 있게 노출(비소유 참조).
    ddc::SchemaRegistry&        Registry();
    ddc::DynamicComponentStore& Store();

private:
    std::unique_ptr<ddc::SchemaRegistry>        m_registry;
    std::unique_ptr<ddc::DynamicComponentStore> m_store;
    sol::state* m_lua = nullptr;   // Register 에서 설정(비소유 원시 포인터 — 계약 안전)
};

// ---------------------------------------------------------------------------
// 편의 팩토리 — 표준 엔진 바인딩 모듈 묶음을 생성해 소유 벡터로 반환한다.
//   데모/App 배선: for (auto& m : MakeStandardBindings(...)) rt.AddBindingModule(std::move(m));
//   각 인자는 nullptr 허용(해당 표면은 안전 no-op 또는 폴백).
// ---------------------------------------------------------------------------
std::vector<std::unique_ptr<IBindingModule>> MakeStandardBindings(
    ecs::World* world, const InputState* input, audio::AudioEngine* audioEngine);

} // namespace mye::script
