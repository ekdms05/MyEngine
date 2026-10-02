# 03. 씬·월드

`mye_scene`은 ECS·트랜스폼·시스템 실행·타일맵·물리·애니메이션·경로 탐색·씬 직렬화를 제공한다. 현재 ECS는 자체 sparse-set 구현이다. 제품별 시스템 조합은 [현재 구조](13-architecture-and-features.md)와 앱 진입점에서 확인한다.

## 엔티티와 컴포넌트

| API·데이터 | 역할 | 소스 |
|---|---|---|
| Entity·World | 64비트 핸들·생성/파괴·컴포넌트 접근 | [World.h](../engine/scene/include/mye/ecs/World.h) |
| ComponentTypeDesc | 타입 ID·크기·정렬·생성/파괴/이동·선택적 리플렉션 | [ComponentType.h](../engine/scene/include/mye/ecs/ComponentType.h) |
| ComponentPool·View | 타입별 sparse-set 저장과 조회 | [ComponentPool.h](../engine/scene/include/mye/ecs/ComponentPool.h), [View.h](../engine/scene/include/mye/ecs/View.h) |
| CommandBuffer | 구조 변경 명령의 지정 경계 처리 | [CommandBuffer.h](../engine/scene/include/mye/ecs/CommandBuffer.h) |
| Local/WorldTransform·Parent | 계층과 월드 변환 | [Transform.h](../engine/scene/include/mye/scene/Transform.h) |

타입 식별은 이름 기반 FNV-1a 64비트 해시를 사용한다. 엔티티의 수명과 비소유 참조를 구분하고, 조회 중 구조 변경은 기존 CommandBuffer 계약을 따른다. 모든 명령이 동일한 지연 방식으로 동작한다고 가정하지 않고 실제 메서드 구현을 확인한다.

## 씬 시스템 실행

[SceneModule](../engine/scene/src/scene/SceneModule.cpp)은 World·월드별 EventBus·SystemScheduler를 소유한다. 코어의 업데이트에 Input·FixedUpdate·Update·PostUpdate·RenderExtract를 연결한다. 기본 등록 시스템은 월드 트랜스폼 갱신이다.

시스템은 이름·페이즈·읽기/쓰기 집합·실행 함수를 등록한다. [SystemScheduler](../engine/scene/src/scene/SystemScheduler.cpp)는 실행 순서를 관리하며 현재 페이즈 실행은 직렬이다. 물리·애니메이션·Lua·렌더 추출은 소비 앱이 필요한 경로를 등록해야 한다.

편집과 플레이의 월드는 분리하고 게임플레이 이벤트도 각 월드의 버스로 전달한다. 렌더 입력은 ECS 전체를 직접 그리는 대신 [RenderExtract](../engine/scene/src/scene/RenderExtract.cpp)의 `RenderProxyList`로 전달한다. 추출은 정렬 필드를 공급하고 GPU 깊이 인코딩은 렌더러가 담당한다.

## 지형·층·충돌

[TilemapWorld](../engine/scene/include/mye/tilemap/Tilemap.h)는 32×32 청크와 셀별 다중 컬럼·높이·경사를 표현한다. 다리 위/아래는 같은 화면 위치에 다른 층의 컬럼을 두어 표현한다. `FloorLevel`과 렌더 sortLayer의 연결은 [렌더 계약](02-rendering.md)을 따른다.

[Motion2D](../engine/scene/include/mye/phys/Motion2D.h)의 `MoveAndSlide2D`는 값 타입으로 상자/원·층/레이어를 계산하고 `Expected<MotionResult2D, Error>`를 반환한다. 내부 `CastMotion2D`는 이동 구간의 첫 접촉을 반환하는 연속 형상 캐스트다. `mye_physics2d`는 core만 링크하며 ECS·에셋·렌더를 포함하지 않는다. [PhysicsWorld2D](../engine/scene/include/mye/phys/PhysicsWorld2D.h)는 기존 World에서 중심 위치(변환+오프셋)와 충돌 데이터를 추출하는 어댑터다. MyEditor Play와 MyGame은 `ObjectSystem::Tick → PhysicsWorld2D::Step → MoveAndSlide2D`의 고정 틱 경로를 사용한다. `Step`도 `Expected<void, Error>`를 반환하며 계산/검증 실패 시 그 틱의 물리 위치·lastMove/hitWall·트리거 이력을 반영하지 않는다. 앞서 실행한 Lua·조작 입력까지 롤백하는 트랜잭션은 아니다.

공통 함수는 유한한 크기/범위/속도, 층 `0..7`, dt `(0,1]`, 반복 상한 `1..16`을 검사한다. 이동체는 root LocalTransform이 필요하며, 부모 변환 역변환을 자동 수행하지 않는다. 공간 해시는 잘못된 AABB를 거부하고 큰 영역이나 셀 정수 범위 밖의 항목을 선형 검사해 누락·셀 반복 폭증을 피한다. 한 항목/쿼리의 셀 등록 상한은 4,096이며 월드 크기나 충돌 크기의 제한이 아니다. 큰 항목끼리의 후보 검사 비용은 최악 O(n²)이다.

충돌 크기 합과 원 거리 계산의 중간값은 double/hypot을 사용해 유한한 float 입력의 합·제곱 오버플로를 피한다. 저장 좌표와 반환 이동은 기존 Vec2(float)이며 float로 표현할 수 없는 결과는 이동 경계에서 오류로 거부한다. 이 처리는 좌표 정밀도나 큰 월드 렌더 지원을 확대하는 기능이 아니다.

현재 소스의 이동은 이산 substep 대신 연속 상자/원 캐스트를 사용한다. 원-상자는 사각형 면과 둥근 모서리를 정확히 구분한다. 초기 겹침은 정지 상태에서도 제한된 반복으로 회복하며 좁은 틈에서 회복할 수 없으면 오류를 반환한다. 접촉까지 이동하고 남은 이동의 안쪽 법선 성분을 제거하므로 접선 이동을 중복 적용하지 않는다. 마지막 허용 접선도 다시 캐스트해 다른 벽을 통과하지 않는다. 접촉 후 float 반올림으로 다시 파고들지 않도록 법선 방향으로 한 표현 단위만 이동한다. 극단적인 좌표/이동 규모 때문에 최종 겹침을 피할 수 없으면 성공으로 처리하지 않는다.

`CastMotion2D`는 변위의 `[0,1]` 비율·장애물 밖을 향한 단위 법선·충돌 ID를 반환한다. 초기 겹침은 비율 0, 접촉 없는 결과는 빈 optional이다. 트리거·동일한 비영 ID·층/레이어가 맞지 않는 항목은 막지 않는다. 정확한 동률은 접촉 비율→법선 x/y→ID 순으로 정해 입력 배열 순서의 영향을 줄인다. 현재 각 캐스트는 선형 탐색이며 동일 목표 장면의 측정이 후보 탐색 병목을 보여줄 때 기존 broadphase를 연결한다. 동적 장애물의 상대 속도/푸시·회전 형상·통과 중 트리거 이벤트는 제공하지 않는다.

이 변경은 main의 개발 소스이며 기존 0.3.0 배포본에는 없다. 서버/예측과 공식 앱이 같은 계산을 소비한다. 타일 높이·층 전이 통합은 남았다. skin·snapToGround는 저장 호환 필드로 유지하지만 2D 계산에는 적용하지 않는다. 호출자가 없는 PhysicsSystem/IPhysicsWorld와 구현·소비가 없는 ITileCollision/SetTileCollision은 제거했다. 타일 데이터와 경로 탐색은 보존하며, D10에서 작성한 타일의 정적 충돌을 같은 CollisionBody2D로 연결한다. 별도 물리 구현/실제 스케줄러 소비가 생기면 수명·오류 전달 요구를 기준으로 재검토한다.

[Pathfinding](../engine/scene/include/mye/nav/Pathfinding.h)은 그리드·층을 고려한 A*와 비동기 경로 요청을 제공한다. 모든 NPC·컷신이 이를 자동 사용하지 않는다. 컷신의 현재 이동과 NPC 경로 추종은 각 소비자의 호출을 확인한다.

## 온라인 2D 장면 계약 준비

개발 소스의 [LoadOnlineScene2D](../engine/runtime/include/mye/runtime/OnlineScene.h)는 `.myeproj`와 프로젝트 내부 `.scene`을 공용 SceneSerializer/ObjectComponents로 검증한다. 캐릭터의 Collider2D 중심·offset·층/레이어, 속도와 반복 상한, Transform 원점 스폰을 추출하며 캐릭터를 제외한 정적 충돌/트리거를 값 타입으로 보관한다. `GatherCollisionBodies2D`를 로컬 PhysicsWorld2D와 함께 사용하므로 계층 변환·오프셋·FloorLevel의 우선순위가 달라지지 않는다. 그래픽 파일 로딩·Lua 실행·클라이언트가 제공한 충돌 데이터는 이 경로에 포함하지 않는다.

활성 CharacterController2D는 정확히 하나여야 하고, 트리거 캐릭터·추가 KinematicBody2D·3D 물리 컴포넌트는 거부한다. 삽입된 3D 시각 에셋을 삭제하지 않는다. `ValidateSpawn2D`는 지정한 본체 중심의 유한 범위와 실제 막는 형상의 겹침을 공통 캐스트로 검사하며 접촉은 허용하고 잘못된 좌표를 자동으로 밀어내지 않는다. 두 차원의 장면 로더는 같은 파일 경계에서 버전/name/mainScene, 절대/드라이브/NUL/잘못된 UTF-8 경로와 assets 밖의 canonical 위치를 거부하며 장면 ID를 정규화한다. hash는 파일 내용의 호환 지문이며 인증 증명이 아니다.

`LoadOnlineScene`은 한 번 검증한 장면의 활성 캐릭터로 2D/3D를 선택하고 잘못된 2D 계약에서 다른 차원으로 재시도하지 않는다. 개발 소스의 `MyServer --project`·`MyGame --connect`가 이 결과를 소비한다. [공식 2D 플레이 가이드](23-2d-online-play.md)와 tools/verify-online2d.ps1의 두 앱 실행·픽셀/퇴장·저장/재접속 검증을 라이브러리 회귀와 구분한다. 기존 0.3.0 바이너리에는 이 연결이 없다.

## 인증된 2D 이동의 라이브러리 연결

개발 소스의 `StepMotion2D`는 MotionSettings2D의 캐릭터 형상/offset·속도·반복 상한과 MotionState2D의 원점·방향·층을 공통 MoveAndSlide2D에 전달한다. NetGameServer::Configure2D/NetClient::Configure2D가 같은 값과 정적 충돌 span을 보관하며 서버 Tick, 즉시 예측과 ack 이후 재실행에서 이 함수를 사용한다. 이 연결은 실제 UDP 라이브러리 및 MyServer/MyGame 두 실행 파일에서 검증했다. 충돌 span은 비소유 참조이므로 설정부터 연결 종료까지 장면 데이터의 수명/내용을 유지한다.

2D 입력은 60 Hz 틱당 정규화된 XY 방향과 순서만 전달한다. 서버가 실제 계산을 마친 입력에만 ack를 주며 틱당 하나를 처리한다. 미확인 입력은 최대 240개, 재전송은 앞의 8개다. 입력 생성이 멈춰도 NetClient.Receive가 100ms 송신 간격을 확인해 남은 입력을 재전송하며 새 순서/예측을 추가하지 않는다. 예약된 jump 비트는 2D에서 false만 허용한다. 2D 메시지는 protocol version 3/type 11..15로 구분하여 legacy XY(version 1)와 XYZ(version 2)를 오인하지 않는다. 두 인증 차원은 nonce/무작위 세션 token·송신 주소·길이/padding·연속 순서 검사와 접속/해제 경계를 공유한다.

상태는 float32 원점/lastMove·방향과 0..7 층/벽 접촉을 전달한다. 원점은 각 축 ±100,000, 고정 틱 lastMove 길이는 2 이하, 방향은 +Y가 0이고 +X가 π/2다. 최대 40개 엔티티의 전체 스냅샷은 1400 bytes 이하다. 이것은 데이터그램 상한이며 처리량/목표 동접 측정이 아니다. 더 오래된 tick, 이미 확인한 입력보다 낮은 ack, 아직 전송하지 않은 ack, 잘못된 token/형상 상태/층/중복 ID·잔여 바이트는 예측을 바꾸지 않는다.

NetGameServer의 2D admission은 계정 소유권·단일 세션·저장 sceneId/world3D/floorLevel과 물리 배치를 Join 전에 검사한다. 신규 캐릭터는 작성 스폰/층, 기존 캐릭터는 저장된 원점/방향을 사용한다. 다른 장면/층이나 겹친 저장 위치를 자동 변환하지 않는다. 권위 상태는 기존 GameServer/CharacterStore의 위치·방향·층으로 저장하며 floorLevel이 없는 구 기록은 0이다. 인증 콜백을 앱에서 교체해도 장면 계약이 있는 캐릭터는 legacy XY의 세션 생성 경계에서 거부한다.

2D 권위 기록은 Z=0이어야 한다. 저장된 비영 Z를 조용히 지우지 않고 admission에서 거부한다. 장면의 시각 높이/3D 에셋과 서버의 XY 이동 좌표는 별개다. 현재 한계는 동일 정적 장면·고정 층과 정적 형상에 대한 이동이다. 캐릭터끼리의 solid 충돌, 층 전환, 온라인 Lua/게임 규칙·포털·UI·보간은 후속 작업이다. 온라인 클라이언트는 ObjectSystem/Lua를 생성하지 않고 권위 상태를 표현한다. 전송은 기존 loopback UDP이며 공개 보호 전송으로 설명하지 않는다.

## 애니메이션·씬 데이터

[AnimationSystem](../engine/scene/include/mye/anim/AnimationSystem.h)·SpriteAnimator·ClipPlayback은 클립·방향 세트·상태·이벤트를 처리한다. 발소리·공격 타이밍 같은 이벤트의 최종 소비자는 게임/오디오/스크립트 시스템이다.

공용 [SceneSerializer](../engine/scene/include/mye/scene/SceneSerializer.h)는 리플렉션 기반 씬 데이터를 다룬다. 에디터의 프로젝트 OpenScene·Play 복제·검증된 맵 전환 후보와 런타임 앱의 로드가 이 경로를 사용한다. 오브젝트 이름·충돌·조작·포털·동작의 저장 계약은 [17](17-object-workflow.md)을 따른다. 자동 스트리밍까지 연결된 것은 아니다. 자동 씬 스트리밍·중첩 프리팹·분산 World는 현재 사용 경로에 포함하지 않는다.

에셋 참조는 `{ "guid": "00112233-4455-6677-8899-aabbccddeeff", "type": "0" }` 형식이다. GUID는 32자리 hex 또는 정확한 8-4-4-4-12 형식이며 빈 문자열/모두 0은 미지정이다. `type`은 정밀도 보존을 위한 u64 10진 문자열이고 기존 비음수 JSON 정수도 읽는다. 잘못된 GUID·뒤따르는 문자·type 범위 초과를 미지정 에셋으로 바꾸지 않고, 엔티티 로컬 ID·컴포넌트·필드와 함께 거부한다. 이 검증은 생성 전에 수행하므로 기존 World를 변경하지 않는다. 파일 로드는 씬 경로도 오류에 포함한다. 유효한 GUID가 가리키는 파일의 누락/손상은 별도의 [렌더 오류 경계](02-rendering.md)에서 처리한다.

## 검증과 개선

ECS·타일·물리·애니메이션·경로와 오브젝트 동작은 `mye_tests`에서 검증한다. 플레이어의 프로젝트·맵 전환·스폰·Lua 연결은 `tools/verify-foundation.ps1`에서 실제 앱으로 확인한다. 복사한 씬의 Lua가 상자/원 본체를 1000 unit/s로 이동시키며 두께 0.01 unit 벽 앞에서 멈추는지 검사한다. `MyGame --ticks N`은 실제 고정 틱 N회 후 최종 프레임을 표시/캡처하고 종료하므로 렌더 프레임 수를 물리 틱 수로 간주하지 않는다. NPC·대화·컷신의 라이브러리 테스트를 플레이어 통합 완료로 표시하지 않는다. 서버 이동 계약·층 판정 연결의 완료 조건은 [14](14-development-priorities.md)에 있다.
