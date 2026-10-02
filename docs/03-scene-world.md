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

[Motion2D](../engine/scene/include/mye/phys/Motion2D.h)의 `MoveAndSlide2D`는 값 타입으로 상자/원·층/레이어를 계산하고 `Expected<MotionResult2D, Error>`를 반환한다. `mye_physics2d`는 core만 링크하며 ECS·에셋·렌더를 포함하지 않는다. [PhysicsWorld2D](../engine/scene/include/mye/phys/PhysicsWorld2D.h)는 기존 World에서 중심 위치(변환+오프셋)와 충돌 데이터를 추출하는 어댑터다. MyEditor Play와 MyGame은 `ObjectSystem::Tick → PhysicsWorld2D::Step → MoveAndSlide2D`의 고정 틱 경로를 사용한다. `Step`도 `Expected<void, Error>`를 반환하며 계산/검증 실패 시 그 틱의 물리 위치·lastMove/hitWall·트리거 이력을 반영하지 않는다. 앞서 실행한 Lua·조작 입력까지 롤백하는 트랜잭션은 아니다.

공통 함수는 유한한 크기/범위/속도, 층 `0..7`, dt `(0,1]`, 반복 상한 `1..16`을 검사한다. 이동체는 root LocalTransform이 필요하며, 부모 변환 역변환을 자동 수행하지 않는다. 공간 해시는 잘못된 AABB를 거부하고 큰 영역이나 셀 정수 범위 밖의 항목을 선형 검사해 누락·셀 반복 폭증을 피한다. 한 항목/쿼리의 셀 등록 상한은 4,096이며 월드 크기나 충돌 크기의 제한이 아니다. 큰 항목끼리의 후보 검사 비용은 최악 O(n²)이다.

충돌 크기 합과 원 거리 계산의 중간값은 double/hypot을 사용해 유한한 float 입력의 합·제곱 오버플로를 피한다. 저장 좌표와 반환 이동은 기존 Vec2(float)이며 float로 표현할 수 없는 결과는 이동 경계에서 오류로 거부한다. 이 처리는 좌표 정밀도나 큰 월드 렌더 지원을 확대하는 기능이 아니다.

현재 이동은 최대 16개 이산 substep이다. 얇은 장애물에 대한 연속 형상 캐스트와 서버/예측 소비는 아직 없다. skin·snapToGround는 보존된 데이터이며 2D 계산에서 사용하지 않는다. 타일 높이·층 전이의 완전한 통합도 별도 완료 조건이다. 호출자가 없는 PhysicsSystem 등록 함수와 단일 구현용 IPhysicsWorld는 제거했다. 실제 교체 구현/스케줄러 소비가 생기면 그 수명·오류 전달 요구를 기준으로 다시 검토한다.

[Pathfinding](../engine/scene/include/mye/nav/Pathfinding.h)은 그리드·층을 고려한 A*와 비동기 경로 요청을 제공한다. 모든 NPC·컷신이 이를 자동 사용하지 않는다. 컷신의 현재 이동과 NPC 경로 추종은 각 소비자의 호출을 확인한다.

## 애니메이션·씬 데이터

[AnimationSystem](../engine/scene/include/mye/anim/AnimationSystem.h)·SpriteAnimator·ClipPlayback은 클립·방향 세트·상태·이벤트를 처리한다. 발소리·공격 타이밍 같은 이벤트의 최종 소비자는 게임/오디오/스크립트 시스템이다.

공용 [SceneSerializer](../engine/scene/include/mye/scene/SceneSerializer.h)는 리플렉션 기반 씬 데이터를 다룬다. 에디터의 프로젝트 OpenScene·Play 복제·검증된 맵 전환 후보와 런타임 앱의 로드가 이 경로를 사용한다. 오브젝트 이름·충돌·조작·포털·동작의 저장 계약은 [17](17-object-workflow.md)을 따른다. 자동 스트리밍까지 연결된 것은 아니다. 자동 씬 스트리밍·중첩 프리팹·분산 World는 현재 사용 경로에 포함하지 않는다.

에셋 참조는 `{ "guid": "00112233-4455-6677-8899-aabbccddeeff", "type": "0" }` 형식이다. GUID는 32자리 hex 또는 정확한 8-4-4-4-12 형식이며 빈 문자열/모두 0은 미지정이다. `type`은 정밀도 보존을 위한 u64 10진 문자열이고 기존 비음수 JSON 정수도 읽는다. 잘못된 GUID·뒤따르는 문자·type 범위 초과를 미지정 에셋으로 바꾸지 않고, 엔티티 로컬 ID·컴포넌트·필드와 함께 거부한다. 이 검증은 생성 전에 수행하므로 기존 World를 변경하지 않는다. 파일 로드는 씬 경로도 오류에 포함한다. 유효한 GUID가 가리키는 파일의 누락/손상은 별도의 [렌더 오류 경계](02-rendering.md)에서 처리한다.

## 검증과 개선

ECS·타일·물리·애니메이션·경로와 오브젝트 동작은 `mye_tests`에서 검증한다. 플레이어의 프로젝트·맵 전환·스폰·Lua 연결은 `tools/verify-foundation.ps1`에서 실제 앱으로 확인한다. NPC·대화·컷신의 라이브러리 테스트를 플레이어 통합 완료로 표시하지 않는다. 서버 이동 계약·층 판정 연결의 완료 조건은 [14](14-development-priorities.md)에 있다.
