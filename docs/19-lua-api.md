# Lua 스크립팅 가이드

오브젝트를 선택하고 **Lua** 작업대에서 클래스 테이블을 반환하는 스크립트를 작성한 뒤 저장하고 실행한다. MyEngine은 Lua 5.4를 직접 호출한다. C++ 바인딩 라이브러리는 사용하지 않는다.

## 에디터에서 바로 쓰는 예제

```lua
local Object = {}

function Object:on_init()
    self.state.visits = 0
end

function Object:on_interact()
    self.state.visits = self.state.visits + 1
    local entity = mye.world.entity_from_packed(self.entity)
    local position = entity:get_position()
    mye.log("방문", self.state.visits, "위치", position.x, position.y)
end

return Object
```

선택한 오브젝트에 **상호작용** 컴포넌트를 추가하면 플레이어가 반경 안에서 상호작용할 때 `on_interact`를 호출한다. 콜라이더를 트리거로 설정하면 `on_trigger_enter`와 `on_trigger_exit`도 사용할 수 있다. Lua 코드 자체는 오브젝트의 동작 컴포넌트에 저장한다. 편집 중에는 실행하지 않으며 Play에서 별도 월드에 인스턴스를 만든다.

## 사용 가능한 API 범위

| 실행 경로 | 등록되는 API |
|---|---|
| 에디터·프로젝트의 `ObjectSystem` | 로그, 코루틴, 수학 값, ECS 오브젝트 접근, 이름 기반 입력 액션(현재 main 소스) |
| `MakeStandardBindings`를 사용하는 앱 | 위 API와 입력, 오디오, Lua 사용자 이벤트 |
| 앱이 추가 등록한 모듈 | 리플렉션, 데이터 컴포넌트, 대화·컷신·카메라·저장·번역·씬 전환, NPC |

아래의 **추가 모듈**은 라이브러리 API다. 에디터에 모두 자동 등록되는 기능으로 가정하지 않는다. `if mye.dialogue then ... end`처럼 등록 여부를 확인할 수 있지만, 실제 동작에는 해당 C++ 서비스 초기화와 갱신도 필요하다. 이름이 `__` 또는 `_`로 시작하는 엔진 내부 테이블은 호출·수정하지 않는다.

## 변수·수명·단위

| 값 | 의미 |
|---|---|
| `self` | 이 오브젝트의 Lua 인스턴스. 콜론 메서드의 첫 인자 |
| `self.entity` | 세대 번호가 포함된 정수 엔티티 핸들. 직접 숫자를 만들어 넣지 않는다 |
| `self.state` | 인스턴스 상태 테이블. 에셋 핫 리로드 시 보존되며 Stop 후 다음 Play에는 새로 생성 |
| `dt` | 직전 시뮬레이션 갱신의 초 단위 시간 |
| 위치·반경 | 월드 단위. 기본 48px = 1 unit, +Y는 위쪽 |
| 속도 | 월드 단위/초 |
| Lua 배열 | 첫 항목 인덱스 1. 대화 선택 인덱스만 0부터 시작 |

`get_position()`은 부모에 대한 **로컬 XY** 좌표를 반환한다. `set_position()`은 XY만 바꾸며 Z를 유지하고 트랜스폼을 갱신 대상으로 표시한다. Vec2 등 반환 값은 복사본이므로 `entity:get_position().x = 3`만으로 ECS가 변하지 않는다. 수정한 벡터를 `set_position()`에 다시 전달한다.

기본 캐릭터 조작 컴포넌트는 매 틱 속도를 결정한다. Lua로 이동을 직접 제어하려면 **캐릭터 조작 컴포넌트를 제거**하고 키네마틱 바디·콜라이더를 유지한다. 비활성 조작 컴포넌트도 현재 경로에서는 속도를 0으로 덮어쓴다. `set_position`은 순간 위치 변경이므로 일반 이동·충돌에는 `set_velocity`를 사용한다.

## 콜백

파일 또는 오브젝트 코드는 마지막에 클래스 테이블을 `return`해야 한다. 필요한 콜백만 선언한다.

| 콜백 | 호출 시점·인자 |
|---|---|
| `on_init()` | 인스턴스 생성 후 한 번 |
| `on_start()` | 첫 갱신 직전 한 번 |
| `on_update(dt)` | 매 갱신 |
| `on_late_update(dt)` | 해당 갱신의 `on_update` 뒤 |
| `on_event(name, payload)` | `ScriptSystem::DispatchEvent`가 전달한 이름 있는 이벤트 |
| `on_trigger_enter(other)` | 트리거 진입. `other`는 정수 핸들 |
| `on_trigger_exit(other)` | 트리거 이탈 |
| `on_interact()` | 에디터 오브젝트 상호작용. 기본 라이프사이클 외의 커스텀 콜백 |
| `on_hot_reload()` | `.lua` 에셋 재로드 성공 후. `on_init`을 재호출하지 않음 |
| `on_destroy()` | 삭제를 다음 갱신에서 감지하거나 Play 월드를 종료할 때 |

애니메이션 이벤트는 `on_event("animation", payload)`로 전달한다. `payload`에는 `name`, 문자열 `arg`, 숫자 `value`, 프레임 `frame`이 들어간다. `on_destroy`가 삭제 감지로 호출될 때 엔티티는 이미 무효일 수 있다.

main 소스의 애니메이션 첫 프레임/진입 이벤트와 후속 모션 전환은 고정 틱에서 처리한다. 화면 샘플은 커서·전이 트리거·첫 이벤트를 소비하지 않는다. 2D 캐릭터 조작은 Lua 갱신 이후 방향을 결정하므로 on_update의 facing_index/facing_vector는 이전 완료 틱의 방향이다. 인증 온라인 클라이언트에는 로컬 Lua를 실행하지 않는다. [2D 모션](26-2d-animation.md)의 버전 1/2 기본·8방향 클립, 반전/대체 규칙과 남은 진행률/행동 전환을 확인한다.

콜백 오류는 해당 스크립트 실행을 멈추고 파일·행·traceback을 보고한다. 다른 오브젝트는 계속 실행한다. `.lua` 에셋 핫 리로드는 AssetManager와 월드 이벤트 버스를 연결한 앱에서 동작한다. 에디터의 인라인 Lua는 씬에 저장되며 수정 뒤 다시 Play하여 반영한다.

## 로그·코루틴

| 함수 | 결과·사용법 |
|---|---|
| `print(...)`, `mye.log(...)` | 엔진 로그에 인자 출력 |
| `mye.log_error(...)` | 오류 로그 출력 |
| `mye.co.start(function)` | 전역 코루틴 즉시 시작, 정수 작업 ID 반환 |
| `mye.co.start_for(entity, function)` | 정수 핸들 소유의 코루틴 시작. 엔티티 삭제 때 취소 |
| `mye.co.wait_seconds(seconds)` | 유한한 0 이상의 초를 기다림 |
| `mye.co.wait_event(name)` | 문자열 이벤트를 기다리고 payload 반환 |
| `mye.co.yield()` | 다음 스케줄러 갱신까지 양보 |

```lua
function Object:on_start()
    mye.co.start_for(self.entity, function()
        mye.log("시작")
        mye.co.wait_seconds(1.0)
        mye.log("1초 경과")
    end)
end
```

대기 함수는 코루틴 안에서 호출한다. `mye.events.emit`은 Lua 이벤트 핸들러용이며 `wait_event`를 깨우는 C++ `ScriptSystem::DispatchEvent`와 별개다. 같은 이벤트가 다음 틱 전 여러 번 도착하면 payload를 순서대로 소비한다. 코루틴 오류는 해당 작업을 종료하고 소유 핸들과 함께 보고한다. 반환된 ID에 대한 Lua 취소·조회 API는 현재 제공하지 않는다.

## 수학 값

| 타입·함수 | 표면 |
|---|---|
| `mye.Vec2([x=0, y=0])` | 쓰기 가능한 `x`, `y`; `+`, `-`, 단항 `-`, 스칼라 곱·나눗셈, `==`, 문자열 출력 |
| Vec2 메서드 | `:length()`, `:length_sq()`, `:normalized()`, `:dot(vec)`, `:lerp(vec, t)` |
| `mye.vec2_dot(a, b)` | 두 Vec2의 내적 |
| `mye.vec2_distance(a, b)` | 두 Vec2 사이 거리 |
| `mye.Vec3([x=0, y=0, z=0])` | 쓰기 가능한 `x`, `y`, `z`, `==` |
| `mye.Color([r=0, g=0, b=0, a=1])` | 쓰기 가능한 RGBA, `==`; `mye.Color.white()`, `.black()`, `.transparent()` |
| `mye.Rect([x=0, y=0, w=0, h=0])` | 쓰기 가능한 `x`, `y`, `w`, `h`, `==`; `:contains(Vec2)`, `:overlaps(Rect)` |

색상 채널은 보통 0~1을 사용한다. 생성자는 `.new(...)` 형식으로도 호출할 수 있다. Vec3 바인딩이 있다는 사실이 3D 물리·3D 캐릭터 조작을 뜻하지 않는다.

## ECS 오브젝트

```lua
local entity = mye.world.entity_from_packed(self.entity)
if entity:is_valid() then
    entity:set_velocity(mye.Vec2(1, 0))
end
```

| 함수·메서드 | 결과·제약 |
|---|---|
| `mye.world.entity_from_packed(handle)` | 현재 월드에 속한 Entity 접근 값 |
| `mye.world.valid(entity)`, `entity:is_valid()` | 엔티티 세대·월드 유효성 확인 |
| `entity:packed()` | 정수 핸들 |
| `entity:get_position()`, `:set_position(Vec2)` | LocalTransform XY 읽기·쓰기. 없으면 읽기 0 벡터·쓰기 무동작 |
| `entity:get_velocity()`, `:set_velocity(Vec2)` | KinematicBody2D 속도. 없으면 읽기 0 벡터·쓰기 무동작 |
| `entity:hit_wall()` | 바디의 최근 충돌 여부. 바디가 없으면 false |
| `entity:has_animator()`, `:has_body()` | 해당 컴포넌트 존재 여부 |
| `entity:set_bool(name, bool)`, `:get_bool(name)` | 애니메이터 bool 파라미터. 없으면 false |
| `entity:set_float(name, number)`, `:get_float(name)` | 애니메이터 float 파라미터. 없으면 0 |
| `entity:set_trigger(name)` | 애니메이터 트리거 설정 |
| `entity:face_move(Vec2)` | 이동 방향에 맞춰 애니메이터 8방향 변경 |
| `entity:facing_vector()`, `:facing_index()` | 애니메이터 바라보는 방향 벡터·인덱스 |
| `mye.world.spawn()` | 빈 엔티티 예약 생성. Sprite·Collider 등은 자동 추가하지 않음 |
| `mye.world.destroy(entity)`, `entity:destroy()` | 페이즈 경계의 CommandBuffer에서 삭제 |

### 2D 게임 카메라 Entity

개발 main 소스의 Camera2D 오브젝트를 사용한다. 로컬 MyGame과 에디터 Play의 ObjectSystem에 등록되며 인증 온라인 MyGame은 Lua를 실행하지 않는다. 저장 설정·추종·경계·휠 입력은 [2D 카메라 가이드](24-2d-camera.md)를 참고한다.

| 메서드 | 결과·제약 |
|---|---|
| `entity:set_camera_zoom(zoom)` | 유한한 0.25~8 배. PlayWorld/런타임의 줌만 변경 |
| `entity:shake_camera(amplitude, seconds)` | 0~10 월드 unit, 0~60초. 0은 무동작. 고정 틱으로 감쇠하며 렌더에만 적용 |
| `entity:world_to_screen(Vec2)` | 월드 XY → 내부 960×540 픽셀, 좌상단/+Y 아래 |
| `entity:screen_to_world(Vec2)` | 내부 픽셀 → 월드 XY. 논리 카메라 기준으로 왕복 |

좌표 변환은 흔들림·픽셀 스냅을 제외한다. 네이티브 창 좌표는 레터박스/배율을 제거한 뒤 넣는다. 같은 Lua 콜백의 트랜스폼 변경은 후속 틱 경계에서 반영된다. 삭제된 Entity·없는 Camera2D·비유한/범위 밖 인수·잘못된 카메라 설정은 오류다. 기존 `mye.camera` 추가 모듈과 다른 실제 Play/MyGame 경로다.

Entity 접근 값은 월드의 소유권을 가지지 않는다. Play 종료·씬 교체 뒤에는 다시 얻는다. 스크립트에 없는 컴포넌트를 임의로 추가하는 범용 API나 프리팹 스폰 API는 현재 없다.

## 프로젝트 입력 액션

**파일 → 입력 설정…**에서 `attack`을 추가하고 F를 연결한 뒤 로컬 Play/MyGame의 `on_update`에서 조회한다. [입력 설정](25-input-actions.md)에 저장·취소·바인딩 작성과 전체 계약이 있다. 인증 클라이언트는 Lua를 실행하지 않는다.

```lua
return {
    on_update = function(self, dt)
        if mye.input.is_action_just_pressed("attack") then mye.log("첫 누름") end
        local direction = mye.input.get_vector("move_left", "move_right", "move_down", "move_up")
    end
}
```

| API | 결과 |
|---|---|
| `mye.input.is_action_pressed(name)` | held bool; 적용 세기 > 0 |
| `.is_action_just_pressed(name)`, `.is_action_just_released(name)` | 현재 고정 틱의 누름·해제 bool |
| `.get_action_strength(name)`, `.get_action_raw_strength(name)` | 데드존 적용 후·전 0~1 |
| `.get_vector(left, right, down, up)` | 원형 데드존·길이 1 이하 Vec2 |

조회는 소비하지 않으며 한 틱의 모든 오브젝트가 같은 상태를 읽는다. 짧은 탭은 누름/해제가 모두 true, held/세기는 0이다. 없는 이름은 중립 값이다. 빈 이름·65바이트 이상·문자열이 아닌 인자는 오류다. 초기 프로젝트의 on_init·틱 밖 콜백·월드 종료의 on_destroy는 중립이다. 틱 안에서 생성한 인스턴스/콜백과 삭제 감지의 on_destroy는 해당 틱을 읽는다. 입력 버퍼는 틱 동안만 비소유로 연결하고 반환 시 해제한다. 원시 키/패드 함수는 공식 ObjectSystem에 장치를 연결하지 않아 중립 값을 반환한다.

## 추가 모듈: 입력·오디오·사용자 이벤트

입력은 `InputBindingModule`, 오디오는 `AudioBindingModule`, 이벤트는 `EventBindingModule`을 등록한 앱에서 사용한다.

| 함수 | 결과 |
|---|---|
| `mye.input.is_down(key)`, `.was_pressed(key)`, `.was_released(key)` | 유지·누름·뗌 bool |
| `mye.input.move_axis(negativeX, positiveX, negativeY, positiveY)` | 각 축 -1~1인 Vec2. 대각선을 자동 정규화하지 않음 |
| `mye.input.pad_connected([pad=0])` | 0~3 패드 연결 여부 |
| `mye.input.pad_down(button[, pad=0])`, `.pad_pressed(...)`, `.pad_released(...)` | 패드 버튼 bool |
| `mye.input.left_stick([pad=0])`, `.right_stick(...)` | 스틱 Vec2 |
| `mye.input.left_trigger([pad=0])`, `.right_trigger(...)` | 트리거 숫자 |

이 선택 모듈의 was_pressed/was_released는 입력 **프레임**의 이벤트다. main 소스는 같은 프레임의 짧은 키/마우스 탭도 두 엣지를 보존한다. 여러 고정 틱에 직접 조회하면 프레임 엣지를 반복해서 읽을 수 있으므로 틱 소비를 직접 관리해야 한다. 공식 Play/MyGame은 [프로젝트 입력 설정](25-input-actions.md)과 GameInputBuffer에서 한 번 소비하며 이름 기반 Lua 액션은 그 고정 틱 상태를 읽는다.

선택 모듈의 left_stick/right_stick·트리거 함수는 기존 플랫폼 필터를 유지한다. 프로젝트 이름 기반 액션은 원시 축/트리거에 저장 데드존을 한 번 적용하므로 작은 입력의 결과가 다를 수 있다. 액션의 get_action_raw_strength는 해당 방향의 데드존 전 0~1 세기다. 기본 조작/사용자 액션에는 이름 기반 함수를 사용한다.
| `mye.audio.play_cue(name[, x, y])` | 큐 재생. x와 y를 함께 주면 공간화 |
| `mye.audio.play_music(name[, fade=0.5])`, `.stop_music([fade=0])` | 음악 재생·중지. fade는 초 |
| `mye.audio.set_bus_volume(bus, volume)`, `.get_bus_volume(bus)` | 버스 볼륨 쓰기·읽기 |
| `mye.audio.set_listener(x, y)` | 청취 위치 |
| `mye.events.on(name, function)` | 핸들러 ID 반환 |
| `mye.events.off(name, id)` | 구독 해제 |
| `mye.events.emit(name, ...)` | 등록 순서의 핸들러 호출. 오류를 보고하고 다음 핸들러 진행 |

키 상수는 `mye.Key.A`~`Z`, `NUM0`~`NUM9`, `SPACE`, `ENTER`, `ESCAPE`, `TAB`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `LSHIFT`, `LCTRL`, `LALT`다. 패드 상수는 `mye.Pad.A/B/X/Y`, `DUP/DDOWN/DLEFT/DRIGHT`, `LB/RB`, `LSTICK/RSTICK`, `START/BACK`이다. 버스는 `mye.Bus.MASTER/BGM/SFX/UI/VOICE`다. 범위를 벗어난 키·패드는 false 또는 0으로 처리한다.

오디오 이름은 앱이 설치한 Cue/Clip resolver가 해석한다. resolver가 없거나 이름을 찾지 못하면 재생하지 않는다. 함수 존재와 실제 출력 장치 연결은 구분한다.

## 추가 모듈: 대화·연출·저장·번역·씬

`RuntimeBindings`와 해당 서비스가 필요하다. 대기 함수는 코루틴에서 호출한다.

| 함수 | 결과·인자 |
|---|---|
| `mye.dialogue.say(speaker, body)` | 문자열 대사 표시, 진행 입력까지 대기. 시작 실패 false |
| `mye.dialogue.say_key(speakerKey, bodyKey)` | 번역 키 버전 |
| `mye.dialogue.choose(options)` | 선택까지 대기, 0 기반 인덱스 반환. 실패 -1 |
| `mye.dialogue.advance()` | 대사 진행 |
| `mye.dialogue.pick(index)` | 0 기반 선택 확정 |
| `mye.dialogue.is_active()`, `.picked()` | 활성 bool·마지막 선택 인덱스 |
| `mye.cutscene.move_to(handle, x, y[, speed=3])` | 이동 완료까지 대기. 정수 핸들, 속도는 월드/초 |
| `mye.cutscene.is_move_done(handle)` | 이동 완료 bool |
| `mye.cutscene.wait(seconds)` | 코루틴 시간 대기 |
| `mye.camera.focus(x, y[, speed=0])` | 위치 포커스 시작. speed ≤ 0이면 즉시 |
| `mye.camera.focus_wait(x, y[, speed=0])` | 포커스 완료까지 대기 |
| `mye.camera.follow(handle[, speed=5])`, `.clear_follow()` | 엔티티 추종 시작·해제 |
| `mye.camera.is_focus_done()` | 고정 포커스 완료 bool |
| `mye.save.exists(slot)` | 저장 존재 여부 |
| `mye.save.write(slot[, title="", playTime=0])`, `.read(slot)` | 저장·불러오기 bool. 실패 원인은 로그에 기록 |
| `mye.loc.text(key[, args])` | 번역 문자열. args는 문자열 키→문자열 값 테이블 |
| `mye.loc.set_locale(tag)`, `.locale()` | 언어 설정·현재 태그 |
| `mye.scene.change(vpath)` | 기본 전환 시작 bool |
| `mye.scene.change_wait(vpath)` | 전환 완료까지 대기, 시작 실패 false |
| `mye.scene.is_transitioning()` | 전환 중 bool |

선택지는 `{ "첫째", "둘째" }` 또는 `{ {text="본문", ["goto"]="다음 ID"}, {key="번역 키"} }` 형식이다. `goto`는 Lua 예약어이므로 코드에서 직접 키를 선언할 때는 `["goto"]="다음 ID"`로 작성한다. 카메라 speed는 월드/초 단위의 목표 접근 속도다. 이동 연출의 기본 MoveController는 직선 위치 변경으로 충돌 회피·경로 탐색을 자동 수행하지 않는다.

게임 저장 슬롯은 0 이상의 32비트 정수를 사용한다. -1은 설정, -2는 퀵세이브 예약 슬롯이다. `playTime`은 초다. 카메라는 렌더 적용 콜백, 저장은 SaveParticipant, 씬은 로더·갱신 루프가 앱에서 연결되어야 한다.

## 추가 모듈: NPC

`NpcBindings`·`NpcSystem`과 이동 서비스가 필요하다. 엔티티는 정수 핸들을 사용한다.

```lua
mye.npc.register {
    entity = npcHandle,
    id = "merchant",
    mode = "patrol",
    waypoints = { {x=0, y=0}, {x=2, y=0} },
    on_interact = function()
        mye.dialogue.say("상인", "어서 오세요.")
    end
}
```

| API·설정 | 결과·기본값 |
|---|---|
| `mye.npc.register(description)` | 등록·대체 bool |
| `description.entity`, `.id` | 정수 핸들 필수, 문자열 논리 ID |
| `.mode`, `.waypoints`, `.loop` | `patrol`/`random`/`none`, XY 점 배열, true. loop=false는 왕복 |
| `.speed`, `.wait_min`, `.wait_max` | 2 unit/s, 0.8초, 2초 |
| `.wander_radius`, `.alert_radius`, `.interact_radius` | 3, 2.5, 1.5 월드 단위 |
| `.face_player`, `.on_interact` | true, 인자 없는 상호작용 함수 |
| `mye.npc.on_interact(id, function)` | ID의 핸들러 교체 |
| `mye.npc.set_player(handle)` | 접근·상호작용 거리 기준 플레이어 |
| `mye.npc.unregister(handle)`, `.clear()`, `.count()` | 해제·전체 해제·등록 수 |
| `mye.npc.interact()` | 가장 가까운 NPC와 상호작용, 없으면 0 핸들 |
| `mye.npc.interact_with(handle)` | 지정 NPC 개시 bool |
| `mye.npc.is_interacting()`, `.interacting_npc()` | 진행 여부·현재 핸들 |

핸들러는 코루틴에서 실행한다. 오류가 나도 상호작용 진행 플래그를 해제한다. NpcSystem의 틱, 플레이어 입력 제한, 카메라 연출은 앱의 책임이다.

## 추가 모듈: 리플렉션·데이터 컴포넌트

`ReflectBindingModule`은 등록된 C++ 리플렉션 타입에 접근한다.

| API | 결과 |
|---|---|
| `mye.reflect.has_type(name)`, `.new(name)` | 존재 bool, 새 구조체 또는 nil |
| `object:type_name()`, `:has_field(name)`, `:has_method(name)` | 타입 이름·존재 여부 |
| `object:get(name)`, `:set(name, value)` | 필드 값·쓰기 bool |
| `object:call(name, ...)` | 메서드 결과. 없는 메서드·인자 불일치·네이티브 오류는 nil |

원시형 bool·정수·실수·문자열만 쓰기·메서드 인자로 변환한다. 구조체 필드는 참조로 읽을 수 있으며 부모의 Lua 소유권을 유지한다. 메타테이블과 해제 함수는 공개하지 않으며 GC가 인스턴스를 해제한다. 엔진의 모든 C++ 함수가 자동 노출되는 것은 아니다. 등록된 타입·필드·메서드만 접근한다.

`DdcBindingModule`은 ECS와 별도의 DynamicComponentStore를 소유한다. 데이터 스키마가 네이티브 ECS 컴포넌트나 Inspector UI를 자동 생성하지 않는다.

| API | 결과 |
|---|---|
| `mye.ddc.define(json)` | 스키마 등록 bool. 필드 타입 `i32/i64/f32/f64/bool/string` |
| `mye.ddc.has_schema(name)`, `.new(name)` | 존재 bool, 독립 인스턴스 또는 nil |
| `component:type_name()`, `:has(field)`, `:get(field)`, `:set(field, value)` | 이름·존재·읽기·쓰기 bool. 없는 읽기는 nil |
| `mye.ddc.attach(handle, schema)`, `.get(handle, schema)` | 저장소 참조 또는 nil |
| `mye.ddc.remove(handle, schema)`, `.has(handle, schema)` | 삭제·존재 bool |
| `reference:valid()`, `:entity()` | 저장소 참조 유효성·핸들 |
| `mye.system(schema, function(handle, component, dt))` | 해당 데이터 컴포넌트마다 실행할 함수 등록 |

앱은 `ScriptRuntime::UpdateBindings(dt)`를 호출해야 한다. 실행 중 추가된 시스템은 다음 틱부터 적용한다. 컴포넌트가 제거되면 해당 인스턴스의 남은 함수 호출을 건너뛴다. 서로 다른 스키마의 시스템 실행 순서는 계약하지 않는다.

## 표준 Lua·실패 처리

기본 제공 표준 라이브러리는 base, table, string, math, coroutine, utf8다. `io`, `os`, `package`, `require`, `debug`, `load`, `loadfile`, `dofile`, `collectgarbage`는 기본 게임 VM에서 사용할 수 없다. 파일은 엔진 에셋 경로로 읽고 GC는 엔진이 관리한다. 무한 루프·임의 메모리 사용을 제한하는 실행 시간·메모리 할당 상한은 현재 없다. 신뢰할 수 없는 스크립트를 실행하는 보안 샌드박스로 취급하지 않는다.

잘못된 타입·정수 범위는 Lua 오류로 전달한다. 선택 서비스의 부재·없는 오브젝트/필드는 위 표의 false/nil/0 또는 무동작 계약을 따른다. 실패 반환값을 검사하고 로그를 확인한다.

```lua
local ok, message = pcall(function()
    local entity = mye.world.entity_from_packed(self.entity)
    assert(entity:is_valid(), "오브젝트가 이미 삭제되었습니다")
end)
if not ok then mye.log_error(message) end
```

API 근거는 [ObjectSystem](../engine/runtime/src/ObjectSystem.cpp), [바인딩 구현](../engine/script/src/bindings), [RuntimeBindings](../engine/runtime/src/RuntimeBindings.cpp), [NpcBindings](../engine/runtime/src/NpcBindings.cpp), [ScriptSystem](../engine/script/src/ScriptSystem.cpp)이다. Lua 언어·C API 원칙은 [Lua 5.4 공식 매뉴얼](https://www.lua.org/manual/5.4/manual.html)을 참고한다. 확인일: 2026-10-02.

## XYZ 오브젝트 API

로컬 Play와 로컬 MyGame에서 `mye.world.entity_from_packed(self.entity)`로 엔티티를 얻는다. 기존 XY API는 변경하지 않는다.

| 함수 | 결과·제약 |
|---|---|
| `entity:get_position3d()` | LocalTransform의 로컬 XYZ Vec3 복사본 |
| `entity:set_position3d(mye.Vec3(x,y,z))` | 유한 XYZ로 순간 이동, 바디 초기 상태 재설정. 다음 물리 틱에서 충돌 검증 |
| `entity:get_velocity3d()` | 이전 물리 틱의 XYZ 단위/초 Vec3 |
| `entity:is_on_floor3d()` | 이전 물리 틱의 접지 bool |
| `entity:hit_wall3d()` | 이전 물리 틱의 벽 접촉 bool |

CharacterController3D가 고정 틱의 이동을 처리한다. XYZ 위치 쓰기는 경로 탐색이나 물리 이동 함수가 아니다. 깊게 충돌한 위치는 오류로 거부한다. 일반 이동 속도·중력은 KinematicBody3D.settings에서 설정한다. 트리거는 기존 `on_trigger_enter/exit`와 이벤트 연결을 사용한다. 온라인 MyGame은 권위 상태를 적용하며 클라이언트 Lua·포털을 실행하지 않는다. [XYZ/온라인](21-3d-play-and-online.md)에 지원 범위를 정리했다.
