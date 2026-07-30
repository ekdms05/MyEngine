# 11. 확장성 아키텍처 (Extensibility)

> **제1원칙**: 순수 엔진은 **확장 메커니즘**만 제공하고, 게임(픽셀 MMORPG)은 그 위에 **플러그인 + Lua + 데이터**로 얹는다.
> 게임 로직·콘텐츠는 **엔진 코어에 포함되지 않는다.** 엔진에는 "기능"이 아니라 "확장점"을 추가한다.

엔진과 게임의 경계:

| 계층 | 모듈 | 성격 |
|---|---|---|
| **엔진(게임 무관)** | core·rhi·render·scene(ECS)·asset·audio·script(Lua)·reflect·**plugin**·**ddc**·ui·editor·runtime·net | 특정 게임에 안 묶임 |
| **게임 레이어(엔진 아님)** | 플러그인(.cpp)·Lua 스크립트·데이터(JSON) | MMORPG 규칙·콘텐츠 |

---

## 확장의 세 축

### 1) 리플렉션 (engine/reflect)
비침투 `Reflect<T>` 로 필드·**메서드**를 등록하면 직렬화·에디터·Lua·플러그인이 공용으로 쓴다.

```cpp
struct Buff { int power; int Damage(int d){ power -= d; return power; } };
MYE_REFLECT(Buff);
template<> void mye::refl::Reflect(TypeBuilder<Buff>& b){
    b.Field("power", &Buff::power).Method<&Buff::Damage>("Damage");
}
```
- 필드: `TypeInfo::FindField` → `GetPtr`/타입.
- 메서드: `TypeInfo::FindMethod` → `MethodInfo::Invoke(inst, args, ret)`.

### 2) 플러그인 (engine/plugin)
게임 로직을 엔진 밖 확장 단위로. `IPlugin` 이 OnLoad 에서 리플렉션 타입·per-frame 시스템을 등록하고, `PluginHost` 가 엔진버전 게이팅·언로드 시 대칭 정리(TypeRegistry Unregister)를 한다.

```cpp
struct MyGamePlugin : plugin::IPlugin {
    PluginInfo Info() const override { return {"my.game", 1, 0}; }
    void OnLoad(PluginContext& ctx) override {
        ctx.RegisterType<MyComponent>();
        ctx.RegisterSystem("ai", [](float dt){ /* ... */ });
    }
    void OnUnload(PluginContext&) override {}
};
host.Load(std::make_unique<MyGamePlugin>());
host.Tick(dt);   // 등록된 시스템 실행
```
*(인프로세스 1단계. 네이티브 DLL 로더는 후속.)*

### 3) 데이터드리븐 컴포넌트 (engine/ddc)
고정 `str/agi/int/vit` 대신 게임이 **데이터로 컴포넌트를 정의**한다. 스키마는 타입드 필드(i32/i64/f32/f64/bool/string)+기본값.

```json
{ "name": "Stats", "fields": [
    { "name": "hp",   "type": "i32",    "default": 100 },
    { "name": "crit", "type": "f32",    "default": 0.1 },
    { "name": "name", "type": "string", "default": "용사" }
] }
```
- `SchemaRegistry.LoadFromJson(...)` 로 등록 → `DynamicComponent` 인스턴스.
- `DynamicComponentStore`: 엔티티(uint64 = `ecs::Entity::Packed()`)에 부착·질의(`ForEach`)·월드 세이브(`ToJson/FromJson`).

---

## 게임 로직을 데이터+Lua 로 (캡스톤)

컴포넌트를 **데이터**로 정의하고, 시스템을 **Lua**로 작성한다. 엔진 C++ 한 줄 안 건드린다.

```lua
-- 컴포넌트 정의(데이터)
mye.ddc.define([[{ "name":"Health", "fields":[
    {"name":"cur","type":"i32","default":1},
    {"name":"max","type":"i32","default":10},
    {"name":"regen","type":"i32","default":3}
] }]])

-- 엔티티에 부착
mye.ddc.attach(playerEntityId, "Health")

-- 시스템(Lua) — 컴포넌트별 매 프레임 실행
mye.system("Health", function(e, c, dt)
    local n = c:get("cur") + c:get("regen")
    if n > c:get("max") then n = c:get("max") end
    c:set("cur", n)
end)
```
`DdcBindingModule.Tick(dt)` 를 매 프레임 호출하면 등록된 Lua 시스템이 해당 컴포넌트 엔티티마다 실행된다(엔티티 스냅샷으로 반복 중 부착/제거 안전, 시스템 에러는 격리·로그).

리플렉션 C++ 타입도 Lua 에서 직접 조작:
```lua
local a = mye.reflect.new("Buff")   -- 등록 리플렉션 타입
a:set("power", 10);  a:call("Damage", 3)
```

---

## 안전 규약 (필수)

- **Lua 바인딩은 절대 C++ 예외를 던지지 않는다.** `SOL_EXCEPTIONS_SAFE_PROPAGATION` + C-Lua 조합에서 바인딩 throw 는 Lua 프레임 통과 중 **행(hang)** 을 유발한다. 오류는 `nil`/`false`/no-op + 로그로 처리하고, `is<T>()` 로 타입 검사 후 변환한다.
- **원시형 임시값은 정확한 C++ 타입 홀더로 마샬**한다(리플렉션 원시형 TypeInfo 에는 Construct 훅이 없어 raw 버퍼+`std::string` 대입은 UB).
- **IBindingModule 은 sol 객체를 멤버로 저장하지 않는다**(VM 수명). Lua 시스템은 Lua 소유 테이블에 보관하고 Tick 에서 원시 상태 포인터로 조회한다.

---

## 요약

- 엔진 = 확장 메커니즘(리플렉션·플러그인·데이터드리븐 컴포넌트·Lua 바인딩).
- 게임 = 플러그인(C++) + 시스템/조작(Lua) + 컴포넌트/콘텐츠(데이터). 엔진 코어 무수정.
- 검증: `PluginGameIntegrationTests`(플러그인+데이터+시스템)·`ScriptDdcSystemTests`(Lua 시스템)·`ScriptReflectTests`(리플렉션 Lua). 전체 테스트 427/427.
