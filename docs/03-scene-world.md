# 03. 씬·ECS·월드 (Scene, ECS & World)

> 소유 레이어: **L3 (Scene/ECS·World)** — L0 Core, L1 RHI, L2 Renderer·Asset 위에 놓이며, L4 Scripting·Plugin과 L5 Editor가 이 모듈의 최대 소비자다.
> 네임스페이스: `mye::ecs`, `mye::scene`, `mye::tilemap`, `mye::anim`, `mye::phys`, `mye::nav`

---

## 목표와 책임

이 모듈은 "게임 세계의 상태"를 소유한다. 구체적으로:

- **ECS 코어**: 엔티티·컴포넌트 저장소, 쿼리, 시스템 스케줄링, 커맨드 버퍼.
- **하이브리드 씬**: 2D 레이어와 3D 노드가 한 씬에 공존하는 단일 씬 모델. 테일즈위버식(2D 우선 + 3D 삽입)과 HD-2D식(3D 월드 + 빌보드) 모두 동일한 데이터 모델로 표현.
- **Transform 계층**: 부모-자식 계층과 ECS의 결합, 월드 변환 전파.
- **타일맵**: 멀티 레이어, 높이·경사·다리(테일즈위버식 지형), 오토타일, 청크 분할·스트리밍, 타일 속성.
- **스프라이트 애니메이션**: 프레임 애니메이션, 8방향 캐릭터, 상태 머신, 애니메이션 이벤트.
- **페이퍼돌**: 장비 파츠 레이어링과 방향별 z순서.
- **충돌·간이 물리**: AABB·원·타일 충돌, 트리거, 레이캐스트, 층(높이) 인지 충돌.
- **길찾기**: 그리드 A*, 높이·다리 인지.
- **프리팹**: 중첩·오버라이드 인스턴스화.
- **씬 라이프사이클**: 로드·언로드·additive 합성·전환. (직렬화 **포맷**은 04 소유 — 본 모듈은 "무엇을 직렬화할지"의 스키마만 정의)

**책임이 아닌 것**: 실제 드로우콜·배칭·정렬 실행(02), 에셋 I/O·리플렉션 프레임워크(04), Lua 바인딩 코드 생성(05), 인게임 UI(06), 에디터 패널(07).

---

## 설계 개요

### 1. ECS 저장 구조 — 아키타입 vs 희소집합

| 기준 | 아키타입(Archetype) | 희소집합(Sparse Set) |
|---|---|---|
| 다중 컴포넌트 순회 | 매우 빠름(청크 단위 연속) | 교집합 계산 필요(최소 풀 기준) |
| 컴포넌트 추가/제거 | 느림(아키타입 이동 = 전체 복사) | O(1) |
| 구현 복잡도 | 높음(청크·그래프·엣지 캐시) | 낮음 |
| 동적 컴포넌트(플러그인·Lua 정의) | 아키타입 폭발 위험 | 풀 하나 추가로 끝 |
| 랜덤 액세스(특정 엔티티의 컴포넌트) | 간접 참조 2회 | 희소 배열 1회 |

**선택: 희소집합 — EnTT(MIT, 헤더 온리)를 내장 의존성으로 채택하고 `mye::ecs` 공개 API로 래핑.** 근거:

1. **확장성이 최상위 목표** — 플러그인·Lua가 런타임에 컴포넌트 타입을 등록/제거하는 시나리오에서 희소집합은 "풀 하나 추가"로 끝난다. 아키타입은 조합 폭발과 스키마 변경 비용이 크다.
2. 이 엔진의 목표 규모(2D RPG, 씬당 수천~수만 엔티티)에서 아키타입의 순회 이점은 병목이 아니다. 병목은 렌더 추출·타일맵이고, 이는 별도 자료구조로 해결한다.
3. 1인 개발 — ECS 저장소·쿼리를 자체 구현·디버깅하는 비용(수 주~수 개월)은 이 엔진의 차별점(하이브리드 렌더링·타일맵)이 아닌 곳에 쓰는 시간이다. EnTT는 본 문서가 채택한 설계(sparse set, 동적 풀, `group` 최적화, 런타임 타입 등록용 `meta`)를 검증된 형태로 제공한다.
4. 필요해지면 EnTT의 `group` 개념(자주 함께 쓰는 컴포넌트 풀을 정렬·동기화)을 후순위 최적화로 도입할 수 있다.

**래핑 원칙**: EnTT 타입은 외부에 직접 노출하지 않고, 본 문서의 `mye::ecs` 공개 API(64비트 `Entity` 핸들, `registerComponent`, `Query`, `CommandBuffer`, 페이즈 스케줄러)로 감싼다. 따라서 아래 API 스케치와 05(Lua)·07(에디터)의 바인딩 계약은 EnTT 채택과 무관하게 불변이며, 훗날 자체 구현으로 교체하는 경로도 열려 있다(DLL 경계 이슈는 오픈 이슈 참조).

**엔티티 핸들**: 64비트 = `index:32 | generation:32`. 파괴 시 generation 증가로 dangling 핸들 무효화. `Entity::null()` 상수 제공. 이 핸들 규약은 05(Lua)·07(에디터 선택 상태)이 그대로 사용한다.

### 2. 시스템 실행 페이즈

프레임 구조는 01의 메인 루프(고정 스텝 + 가변 렌더)에 얹힌다:

```mermaid
flowchart LR
  A[PhaseInput\n입력 스냅샷 반영] --> B[PhaseFixedUpdate\n고정 dt, N회 반복\n물리·이동·게임로직]
  B --> C[PhaseUpdate\n가변 dt\n애니메이션 파라미터 등]
  C --> D[PhasePostUpdate\nTransform 전파\n애니메이션 샘플링·카메라]
  D --> E[PhaseRenderExtract\n렌더 데이터 추출 → 02]
  E --> F[커맨드 버퍼 플러시\n구조 변경 일괄 적용]
```

**01 UpdatePhase와의 매핑** — 본 모듈의 페이즈 스케줄러는 독립 루프가 아니라, **01 메인 루프의 `UpdatePhase` 틱 콜백 내부에서 도는 하위 스케줄러**다. 각 페이즈의 실행 위치는 다음과 같이 확정한다:

| 03 Phase | 실행 위치 (01 `UpdatePhase`) | 비고 |
|---|---|---|
| `Input` | `PreUpdate` 틱 | 01 전역 버스 Flush·입력 스냅샷 확정 **이후** 실행 |
| `FixedUpdate` | `FixedUpdate` | 고정 dt, N회 반복 |
| `Update` | `Update` | 가변 dt |
| `PostUpdate` | `PostUpdate` | Transform 전파·애니 샘플링·카메라 |
| `RenderExtract` | `PreRender` **초입** | 02의 보간 렌더(PreRender) 작업보다 먼저 완료 |

커맨드 버퍼의 최종 플러시는 `RenderExtract` 직후 — 즉 02의 PreRender 렌더 작업이 시작되기 전에 완료된다. `PostRender`에는 본 모듈의 페이즈가 없다.

- 시스템은 페이즈 소속 + 페이즈 내 `before/after` 제약으로 순서를 선언한다(토폴로지 정렬). 명시적 정수 우선순위는 쓰지 않는다 — 플러그인 시스템이 끼어들 때 정수 조정 지옥을 피하기 위함.
- 시스템은 등록 시 읽기/쓰기 컴포넌트 집합을 선언한다. **MVP는 단일 스레드 실행**이되, 선언된 액세스 정보로 이후 01의 잡 시스템에 병렬 디스패치할 수 있는 구조를 유지한다.
- **구조 변경(생성·파괴·컴포넌트 추가/제거)은 순회 중 금지** — 반드시 `CommandBuffer`에 기록하고 페이즈 경계에서 플러시한다. 이터레이터 무효화와 병렬화 안전성을 동시에 해결한다.

**전역 버스 vs 월드-로컬 버스** — `World::events()`는 01 소유 `EventBus` 타입의 **월드-로컬 인스턴스**다. 발행 대상은 다음 기준으로 확정한다:

| 버스 | 대상 이벤트 | Flush 책임 |
|---|---|---|
| 01 전역 버스 | 시스템·에셋·설정·윈도우·모듈 수명 이벤트 | 01 메인 루프 |
| 월드-로컬 버스 (`World::events()`) | **게임플레이 이벤트 전부** — 충돌·트리거 Enter/Stay/Exit, 애니메이션 이벤트, 타일 이벤트, 프리팹 후처리(`OnPrefabInstantiated`) | **본 모듈의 페이즈 스케줄러**가 각 페이즈 경계의 커맨드 버퍼 플러시 직후 Flush |

게임플레이 구독자(05 Lua 핸들러, 06 오디오 발소리 등)는 특정 World 인스턴스가 아니라 **active World**의 버스에 바인딩된다 — 에디터의 편집/플레이 이중 World 상황(07 `PlayModeController::activeWorld`)에서 이벤트가 플레이 World로만 흐르게 하기 위한 규약이다.

### 3. Transform 계층과 ECS의 결합

전통적 씬 그래프(노드 객체 트리)를 두지 않고, **계층을 컴포넌트로 표현**한다:

- `LocalTransform` — 부모 기준 위치·회전·스케일. **모든 엔티티가 3D Transform 하나만 가진다.** 2D 엔티티도 3D 좌표에 존재하며, "2D스러움"은 규약(02가 확정하는 좌표계·PPU·기본 평면 규약을 따름 — 확정: PPU 48, 1 unit = 1타일 = 48px)으로 표현한다. 별도의 Transform2D는 만들지 않는다 — 하이브리드 씬에서 2D↔3D 부모자식 연결이 자유로워야 하기 때문.
- `Parent { Entity parent; }` — 자식이 부모를 가리킨다(단방향 진실).
- `Children` — 부모가 자식 목록을 캐시(파생 데이터, `Parent` 변경 시 시스템이 동기화). 순회·에디터 트리 표시용.
- `WorldTransform` — 시스템이 계산하는 캐시. 사용자는 쓰지 않는다.
- `TransformSystem`(PhasePostUpdate): dirty 플래그 전파 + 계층 깊이 기준 정렬 순회로 `WorldTransform` 갱신. 부모 재지정(reparent)은 커맨드 버퍼 경유, 월드 위치 유지 옵션 제공.

### 4. 하이브리드 씬 — 렌더러블 컴포넌트와 정렬 데이터 모델

렌더러블은 전부 컴포넌트다. 종류:

| 컴포넌트 | 용도 |
|---|---|
| `SpriteRenderer` | 2D 스프라이트. 화면 정렬은 정렬 레이어 규약 |
| `BillboardRenderer` | 3D 월드 안에서 카메라를 향하는 스프라이트(HD-2D 캐릭터) |
| `MeshRenderer` | 3D 메시(테일즈위버식 3D 오브젝트 삽입, HD-2D 월드) |
| `TilemapRenderer` | 타일맵 청크 렌더링 참조 |

**정렬 데이터 모델 — 02의 `SpriteProxy` 계약을 그대로 채택(정렬 실행·인코딩은 02):**

- 정렬 레이어의 정의·순서(**레이어 밴드 표**)는 **02가 프로젝트 설정으로 소유**한다. 본 모듈의 `SortingLayer` 레지스트리(이름 → `uint16 id`, 에셋으로 저장 — 04)는 02 밴드 표의 id에 대한 **이름 매핑일 뿐**, 독자적인 순서 정의를 갖지 않는다.
- 각 렌더러블: `{ uint16 sortLayer; int16 orderInLayer; }`. 필드명은 02 계약(`sortLayer`)을 따른다. `orderInLayer`는 같은 `sortKeyY` 값에서의 타이브레이커로, 02의 `SpriteProxy`에 타이브레이커 필드로 추가를 요청·협의 중인 항목이다("다른 모듈과의 경계" 참조). 확정 전에는 추출 시 무시된다.
- PhaseRenderExtract에서 본 모듈이 02의 `SpriteProxy` 계약대로 **`sortLayer`(uint16)와 `sortKeyY`(논리적 지면 Y — 점프 등 시각 오프셋 제외)**를 공급한다. 레이어 밴드 + 깊이 값 인코딩과 정렬 실행은 전적으로 02 소유이며, **본 모듈은 CPU측 sortKey 패킹을 하지 않는다.** 레이어 내 Y 정렬은 02의 `sortKeyY` 인코딩이 항상 담당하므로 별도의 레이어 `ySort` 플래그는 두지 않는다(`screenSpace` 여부 속성은 유지).
- **FloorLevel → 레이어 밴드 변환**: 엔티티의 `FloorLevel{k}`를 02의 `World k` 밴드 `sortLayer`로 변환하는 규칙을 **본 모듈이 추출 시점에 수행**한다 — 다리 통과 시 02가 기대하는 "sortLayer를 World k → k+1로 변경"은 이 변환으로 충족된다.
- 2D 레이어 패스와 3D 깊이 패스의 인터리브 전략도 02 소유.

### 5. 타일맵 — 테일즈위버식 높이 지형

**지형 표현 대안 비교:**

| 방식 | 설명 | 평가 |
|---|---|---|
| (a) 단일 높이 필드 | 셀당 높이값 1개 + 경사 플래그 | 단순하지만 **다리(같은 셀에 두 층)** 표현 불가 |
| (b) 층별 독립 맵 | 층마다 완전한 타일맵, 포털로 연결 | 다리·경사가 층 경계에서 부자연, 데이터 중복 |
| (c) 셀당 다중 컬럼 | 셀마다 `TileColumn` 배열(지면, 다리 등 각 층이 하나의 컬럼) | 다리·입체 교차 자연 표현. 약간 복잡 |

**선택: (c) 셀당 다중 컬럼.** 대부분 셀은 컬럼 1개이므로 SoA + 오버플로 배열로 메모리 부담이 작고, 테일즈위버의 핵심 지형(육교, 경사로, 계단, 절벽)을 하나의 모델로 커버한다.

- 높이는 **정수 레벨**(`heightLevel: int8`, 1레벨 = 타일 반 칸 높이 등 — 시각 오프셋 환산은 02의 확정 PPU 48 규약 인용: 반 칸=24px, 한 칸=48px)로 양자화한다. 경사는 `SlopeType`(N/S/E/W 방향 + 완경사/급경사)으로 표현하고, 경사 셀 위의 엔티티는 보간된 연속 높이를 갖는다.
- 엔티티는 `FloorLevel { int8 level; }` 컴포넌트로 현재 층을 갖는다. 다리 위/아래 판정, 충돌 필터, 렌더 정렬(다리 아래 캐릭터는 다리 레이어보다 뒤)이 모두 이 값을 참조한다. 경사·계단 셀 통과 시 이동 시스템이 level을 갱신하며, 렌더 정렬에는 RenderExtract가 이 값을 02의 `World k` 밴드 `sortLayer`로 변환해 반영한다(4절 참조).

**구조:**

- `TilesetAsset`(04 관리): 타일 정의 + **타일 속성 테이블**(충돌 셰이프, 이동 비용, 재질 태그(발소리), 이벤트 트리거 id, 커스텀 키-값). 셀 단위 오버라이드 허용.
- `TilemapAsset`: 레이어 목록(지면/장식/오버헤드 등 렌더 레이어) × 청크 그리드.
- **청크**: 32×32셀. 청크 단위로 (1) 렌더 배치 빌드(변경 시 dirty 리빌드 → 02에 정적 배치 제공), (2) 충돌·내비 데이터 베이크, (3) 04 비동기 로딩 단위(스트리밍: 카메라 주변 반경 유지, 대형 맵은 청크 파일 분할).
- **오토타일**: 47타일 블롭/Wang 비트마스크 룰을 `TilesetAsset`에 저장. **룰 평가는 에디터(07 브러시)와 임포트 시점에 수행**하고 런타임 맵은 해석 완료된 타일 id만 가진다. 런타임 지형 변형이 필요한 게임을 위해 룰 평가 API 자체는 런타임에서도 호출 가능하게 둔다.

### 6. 스프라이트 애니메이션과 8방향

기준 아트 스케일(02 확정, 2026-07): 타일 48×48px·PPU 48 기준으로 **권장 캐릭터 스프라이트 높이는 64~96px**이다. 8방향 캐릭터 시트·페이퍼돌 파츠 캔버스는 이 범위를 기준으로 제작한다.

- `SpriteAnimClip`(04 에셋): 프레임 배열(스프라이트 ref, duration), 루프 모드, **이벤트 트랙**(프레임 index + 이벤트 이름 + payload — 발소리, 타격 판정 프레임 등).
- `DirectionalAnimSet`: 논리 클립 이름("walk") → 방향별 실제 클립 매핑. 8방향 중 좌우 대칭 방향은 `flipX` 플래그로 공유 가능(아트 절약).
- `AnimStateMachineAsset`: 상태(클립 or 블렌드 없음 — 프레임 애니메이션이므로 즉시 전환 + 전환 시 프레임 유지 옵션), 전이(조건식: bool/int/float 파라미터 비교, trigger 소모, "클립 종료 시"), any-state 전이.
- `SpriteAnimator` 컴포넌트: 상태 머신 인스턴스 + 파라미터 블랙보드 + 현재 `Facing8`(8방향 enum). 게임 로직(Lua)은 파라미터와 방향만 쓰고, 샘플링은 PhasePostUpdate의 `SpriteAnimSystem`이 수행하여 `SpriteRenderer`의 현재 스프라이트를 갱신한다.
- **애니메이션 이벤트 전달**: 프레임 경계 통과 시 `AnimEvent`를 **월드-로컬 버스**(`World::events()`, 2절의 이원화 규약) + 엔티티 로컬 콜백(Lua 핸들러 바인딩은 05)으로 발행.

### 7. 페이퍼돌 / 장비 레이어링

대안: (a) 파츠별 정렬 시트를 런타임 레이어드 드로우, (b) 장비 변경 시 합성 텍스처 베이크, (c) 본 기반(Spine류).

**선택: (a) 런타임 레이어드 드로우** — 제작 규약으로 "모든 파츠 시트는 바디 시트와 동일 프레임 수·타이밍·캔버스(캐릭터 기준 크기 64~96px 높이, 6절)"를 강제하면 구현이 단순하고 장비 교체가 즉시 반영된다. (b)는 드로우콜 최적화가 필요해질 때 캐시 레이어로 추가(파츠 조합 해시 → 아틀라스 베이크), (c)는 범위 밖(플러그인 확장 포인트로만 열어둠).

- `Paperdoll` 컴포넌트: 슬롯(`Body, Hair, Top, Bottom, Shoes, Weapon, ...`) → 파츠 에셋 ref.
- **방향별 z순서 테이블**: `PaperdollLayerTable`(에셋) — `Facing8 × Slot → int8 zOrder` (예: 위를 볼 때 무기가 몸 뒤로). `PaperdollSystem`이 애니메이터의 현재 방향으로 테이블을 조회, 파츠별 서브 정렬 값을 산출해 렌더 추출에 공급.
- **애니메이션 도중 파츠 앞뒤 전환**(공격 스윙 중 무기가 몸 뒤→앞으로 넘어가는 경우 등)은 방향별 정적 테이블만으로 표현할 수 없으므로 두 장치를 둔다:
  - **기본 경로 — 서브 슬롯 분리(아트 제작 규약으로 고정)**: `Weapon` 등 문제 슬롯은 `WeaponFront`/`WeaponBack` **Front/Back 서브 슬롯 쌍**으로 분리 제작한다. 프레임마다 파츠 픽셀을 앞/뒤 시트에 나눠 그리므로, 창을 대각으로 들어 **한 프레임 안에서 무기가 몸 앞뒤에 동시에 걸치는** 케이스까지 커버한다. Aseprite 레이어 명명 규약(`slot.front` / `slot.back`)으로 04 임포터와 공유한다.
  - **보조 경로 — 프레임 단위 zOrder 오버라이드 트랙**: `SpriteAnimClip`에 `(프레임 index × Slot → zOrder)` 오버라이드 트랙을 두어, 파츠가 프레임 경계에서 통째로 앞/뒤만 바뀌는 단순 케이스는 시트 분리 제작 없이 처리한다. `PaperdollSystem` 조회 시 이 트랙이 `PaperdollLayerTable`보다 우선한다.
- 프레임별 **앵커/소켓**(손 위치 등)을 클립 데이터에 포함 — 무기 이펙트·투사체 발사 위치용.

### 8. 충돌·물리 — 자체 경량 2D, 본격 물리엔진은 플러그인으로

**결정: Box2D 등 본격 물리엔진을 코어에 넣지 않는다.** 근거: 테일즈위버류 RPG의 이동은 "키네마틱 이동 + 벽 슬라이드 + 트리거"가 전부다. 강체 동역학은 불필요하며, 층(FloorLevel) 개념과 기성 물리엔진의 2D 평면 가정이 충돌한다. 대신 `IPhysicsWorld` 인터페이스를 두어 플러그인이 Box2D/Jolt 등을 끼울 수 있게 한다(확장 포인트 참조).

자체 구현 범위:

- 셰이프: AABB, 원(캡슐은 확장). `Collider2D` 컴포넌트(셰이프 + 오프셋 + `isTrigger` + 레이어 마스크 + **floorMask**).
- 타일 충돌: 청크 베이크된 솔리드/경사 데이터와의 스윕 테스트.
- 브로드페이즈: 균일 공간 해시 그리드(타일맵과 셀 크기 정렬).
- `KinematicBody2D`: move-and-slide, 계단/경사 스냅, FloorLevel 전이 처리.
- 트리거: Enter/Stay/Exit 이벤트(커맨드 버퍼 플러시 후 발행, Lua 구독 가능).
- 쿼리: `Raycast`, `ShapeCast`, `OverlapArea` — 모두 floorLevel 필터 인자를 받는다. **다리 위/아래**: 같은 XY라도 floorLevel이 다르면 충돌하지 않는다.
- 실행: PhaseFixedUpdate 내 고정 스텝.

### 9. 길찾기

- 내비 데이터는 타일맵 청크 베이크 산출물: 셀×컬럼별 `{ walkable, moveCost, slopeLink }`. 노드는 `(cellX, cellY, level)` 3튜플 — **다리 위 경로와 아래 경로가 서로 다른 노드**가 되어 자연스럽게 분리된다. 경사·계단 셀이 레벨 간 엣지를 만든다.
- 알고리즘: 그리드 A*(8방향, 코너 컷 금지 옵션), 비용 = 거리 × moveCost × 콜백 보정(확장 포인트). 대형 맵은 청크 경계 게이트웨이 기반 HPA*를 확장 단계에서 도입.
- 요청은 비동기: `NavQuery` 요청 → 01 잡 시스템 워커에서 계산 → 다음 프레임 콜백/폴링. 타일 변경 시 해당 청크 내비 데이터 dirty 리베이크.
- 경로 후처리: 문자열 당기기(string pulling) 간이 스무딩(선택).

### 10. 프리팹과 씬 라이프사이클

- **프리팹** = "엔티티 서브트리의 직렬화 템플릿" 에셋. 인스턴스는 `PrefabInstance { AssetRef<Prefab> source; OverrideSet overrides; }`를 루트에 가진다.
- **오버라이드**: `(엔티티 경로, 컴포넌트 타입, 프로퍼티 경로, 값)` 목록. 프로퍼티 경로 표현과 값 직렬화는 04의 리플렉션 프레임워크를 사용한다(본 모듈은 요구사항 제공자).
- **중첩 프리팹**: 프리팹 안에 다른 프리팹 인스턴스 허용. 해석 순서 = 안쪽부터 펼친 뒤 바깥 오버라이드 적용.
- **씬** = 엔티티 집합 + 타일맵 참조 + 씬 설정(환경광 등)의 직렬화 단위. **additive 로드가 기본** — "월드"는 여러 씬(구역)의 합성이며, 대형 필드는 씬 스트리밍으로 구현한다. `SceneManager`가 로드(04 비동기)·활성화·언로드·전환을 관리하고, 씬 간 유지 엔티티는 "persistent 씬"으로 이동시키는 방식을 쓴다. 전환 연출(페이드 등)은 게임 로직(Lua) 책임.

---

## 핵심 타입·API 스케치

```cpp
namespace mye::ecs {

struct Entity {                     // 64-bit 핸들
    uint32_t index; uint32_t generation;
    static Entity null();
};

class World {                       // 씬과 독립적인 ECS 저장소(에디터 프리뷰용 다중 인스턴스 허용)
public:
    Entity   create();
    void     destroy(Entity e);                    // 즉시 금지, 내부적으로 CommandBuffer 경유
    bool     alive(Entity e) const;

    template<class C, class... Args> C&  add(Entity e, Args&&...);
    template<class C>                C*  tryGet(Entity e);
    template<class C>                void remove(Entity e);

    // 동적(비템플릿) 경로 — 플러그인·Lua·에디터·직렬화가 사용
    void*    addDynamic(Entity e, ComponentTypeId t);
    void*    tryGetDynamic(Entity e, ComponentTypeId t);

    template<class... Cs> Query<Cs...> query();    // 희소집합 교집합, 최소 풀 기준 순회
    EventBus&      events();                       // 01 소유 타입의 월드-로컬 인스턴스 — 게임플레이 이벤트 전용, Flush는 페이즈 스케줄러 담당(2절)
};

// 컴포넌트 타입 등록(네이티브·플러그인·Lua 공용) — 메타데이터는 04 리플렉션과 연동
struct ComponentTypeDesc {
    const char* name;  size_t size, align;
    void (*construct)(void*); void (*destruct)(void*); void (*moveTo)(void*, void*);
    const meta::TypeInfo* reflection;              // 04 소유 타입, 직렬화·에디터 인스펙터용
};
ComponentTypeId registerComponent(const ComponentTypeDesc&);

enum class Phase { Input, FixedUpdate, Update, PostUpdate, RenderExtract };

struct SystemDesc {
    const char* name;  Phase phase;
    std::span<const ComponentTypeId> reads, writes;    // 병렬화 대비 선언
    std::span<const char*> runBefore, runAfter;        // 페이즈 내 토폴로지 제약
    void (*fn)(World&, CommandBuffer&, const FrameTime&);
};
SystemId registerSystem(const SystemDesc&);

class CommandBuffer {               // 순회 중 구조 변경의 유일한 통로
public:
    Entity   createDeferred();
    void     destroy(Entity e);
    template<class C> void add(Entity e, C&& value);
    template<class C> void remove(Entity e);
    void     setParent(Entity child, Entity parent, bool keepWorld = true);
};
} // namespace mye::ecs
```

```cpp
namespace mye::scene {

// ---- Transform (모든 엔티티 공통, 3D 단일 규약 — 좌표계·PPU는 02 문서 인용) ----
struct LocalTransform { float3 position; quat rotation; float3 scale; };
struct WorldTransform { float4x4 matrix; };        // TransformSystem이 계산, 읽기 전용 취급
struct Parent   { ecs::Entity parent; };
struct Children { SmallVector<ecs::Entity, 4> list; };   // 파생 캐시

// ---- 렌더러블 (정렬 데이터를 02에 공급) ----
struct SortingRef  { uint16_t sortLayer; int16_t orderInLayer; };  // sortLayer는 02 레이어 밴드 id. orderInLayer는 02 SpriteProxy 타이브레이커 필드로 협의 중
struct SpriteRenderer    { AssetRef<Sprite> sprite; Color tint; bool flipX, flipY; SortingRef sort; };
struct BillboardRenderer { AssetRef<Sprite> sprite; BillboardMode mode;  SortingRef sort; };
struct MeshRenderer      { AssetRef<Mesh> mesh; AssetRef<Material> materials[/*slots*/]; };
struct TilemapRenderer   { AssetRef<TilemapAsset> map; };

struct FloorLevel { int8_t level; };               // 다리 위/아래 — 충돌·내비 참조. 렌더는 추출 시 02 World 밴드 sortLayer로 변환

// ---- 씬 ----
class SceneManager {
public:
    SceneHandle loadAdditiveAsync(AssetRef<SceneAsset>, LoadPriority = {});
    void        unload(SceneHandle);
    void        setPersistent(ecs::Entity root);   // 씬 언로드에서 제외
    ecs::Entity instantiate(AssetRef<Prefab>, const LocalTransform&, SceneHandle target = {});
};
} // namespace mye::scene
```

```cpp
namespace mye::tilemap {

enum class SlopeType : uint8_t { None, GentleN, GentleS, GentleE, GentleW, SteepN, /*...*/ StairsN, /*...*/ };

struct TileCell {                  // 셀 하나의 "컬럼" 하나(지면 또는 다리 층)
    TileId   tile;
    int8_t   heightLevel;          // 정수 층. 시각 오프셋 환산은 02 PPU 규약(확정: PPU 48)
    SlopeType slope;
    uint8_t  flags;                // solid, oneWayBridge, eventTrigger 등
};
// 저장: 청크(32x32) SoA + "다중 컬럼 셀" 오버플로 리스트

struct TileProps {                 // TilesetAsset의 타일별 속성(04 직렬화)
    CollisionShape collision;  float moveCost;
    StringId materialTag;          // 발소리 등 → 06 오디오가 소비
    StringId eventId;              // 이벤트 트리거 → Lua 핸들러(05)
    PropertyBag custom;            // 확장 키-값
};

class TilemapWorld {               // 런타임 질의 허브(렌더·충돌·내비가 공유)
public:
    std::span<const TileCell> columnsAt(int2 cell) const;
    float   sampleHeight(float2 worldXY, int8_t level) const;   // 경사 보간 포함
    void    setTile(int2 cell, int layer, TileId);              // 청크 dirty → 배치·내비 리베이크
    void    resolveAutotile(RectI2 region, int layer);          // 룰 평가(에디터·런타임 공용)
};
} // namespace mye::tilemap
```

```cpp
namespace mye::anim {

enum class Facing8 : uint8_t { S, SW, W, NW, N, NE, E, SE };

struct AnimEventDef { uint16_t frame; StringId name; PropertyBag payload; };
// SpriteAnimClip / DirectionalAnimSet / AnimStateMachineAsset 은 04가 로드하는 에셋

struct SpriteAnimator {
    AssetRef<AnimStateMachineAsset> machine;
    Blackboard params;             // set(StringId, bool/int/float), fireTrigger(StringId)
    Facing8   facing;
    // 현재 상태·프레임·경과시간은 시스템 내부 상태
};

struct Paperdoll {
    struct SlotEntry { StringId slot; AssetRef<PaperdollPart> part; };
    SmallVector<SlotEntry, 8> slots;
    AssetRef<PaperdollLayerTable> layerTable;      // Facing8 × Slot → zOrder
};
} // namespace mye::anim
```

```cpp
namespace mye::phys {

struct Collider2D { Shape2D shape; float2 offset; bool isTrigger; uint32_t layerMask; uint8_t floorMask; };
struct KinematicBody2D { float2 velocity; bool snapToGround; };

struct RayHit { ecs::Entity entity; float2 point, normal; float distance; };

class PhysicsWorld2D {             // IPhysicsWorld 구현체(기본 내장). 플러그인 교체 가능
public:
    std::optional<RayHit> raycast(float2 from, float2 dir, float maxDist,
                                  uint32_t layerMask, int8_t floorLevel) const;
    uint32_t overlapArea(const Shape2D&, float2 pos, int8_t floorLevel,
                         std::span<ecs::Entity> out) const;
    // 트리거 Enter/Stay/Exit는 World::events()로 발행
};
} // namespace mye::phys
```

```cpp
namespace mye::nav {

struct NavAgentParams { float radius; float maxSlope; uint8_t floorMask; };
struct PathPoint { float2 pos; int8_t level; };

class NavSystem {
public:
    NavRequestId requestPath(PathPoint from, PathPoint to, const NavAgentParams&);
    PathStatus   poll(NavRequestId, std::span<PathPoint>& outPath);   // 잡 시스템(01)에서 비동기 계산
    void         setCostModifier(CostModifierFn, void* user);         // 확장 포인트
};
} // namespace mye::nav
```

---

## 다른 모듈과의 경계

| 상대 | 본 모듈이 받는 것 | 본 모듈이 주는 것 |
|---|---|---|
| **01 Core** | 수학 타입, 잡 시스템, 이벤트 버스 타입, 프레임 타이밍(고정/가변 dt), 로그 | 페이즈 훅을 모듈 라이프사이클에 등록 |
| **02 Rendering** | **좌표계·축·PPU 규약(02 소유 — 본 문서는 인용만)**, 레이어 밴드 표·`SpriteProxy` 정렬 계약(깊이 인코딩 포함), 카메라 | PhaseRenderExtract에서 `sortLayer`·`sortKeyY`를 채운 프록시/RenderItem 목록, 타일맵 청크 정적 배치, 빌보드/스프라이트 변환. **협의 중**: `orderInLayer`를 `SpriteProxy`의 타이브레이커 필드로 추가 요청 |
| **04 Asset** | **리플렉션·직렬화 프레임워크(04 소유)**, `AssetRef<T>` 핸들, 비동기 로딩 | 직렬화 대상 스키마(컴포넌트 리플렉션 등록), 씬·프리팹·타일셋·클립의 "논리 구조" 정의 |
| **05 Scripting/Plugin** | Lua 스크립트 컴포넌트·시스템의 등록 호출 | `registerComponent`/`registerSystem`/CommandBuffer/쿼리 등 확장용 C API 표면, 애니메이션·트리거·충돌 이벤트(**active World의 월드-로컬 버스** 경유 — Lua 구독 API는 이 버스를 대상으로 함) |
| **06 Runtime Systems** | 입력 매핑 액션 상태(게임 시스템이 소비) | 타일 `materialTag`(발소리) — 발소리 등 게임플레이 이벤트 구독은 **active World의 월드-로컬 버스** 대상, 월드 질의(UI가 HP바 위치 계산 등). 파티클 이미터는 06 소유 — 본 모듈은 Transform 부착점만 제공 |
| **07 Editor** | (에디터가 본 모듈의 공개 API만 사용) | World 다중 인스턴스(플레이 모드 분리), Children 트리, 커맨드 버퍼 기반 편집 연산(Undo는 07 책임), 에디터 전용 컴포넌트 허용 플래그 |

경계 원칙: 02는 씬을 모른다 — 본 모듈이 "추출"하여 밀어주는 단방향 데이터 흐름. 04는 컴포넌트의 의미를 모른다 — 리플렉션 메타데이터만 소비한다.

---

## 확장 포인트

확장성이 이 엔진의 최상위 목표이므로, 본 모듈의 모든 핵심 개념은 등록형(registry-based)이다.

1. **커스텀 컴포넌트**
   - 네이티브 플러그인: `registerComponent(ComponentTypeDesc)` — DLL 로드 시 등록, 언로드 시 해당 풀 파기(핫리로드 시 04 직렬화로 상태 보존·복원). 희소집합 선택의 최대 수혜 지점.
   - Lua: `mye.component.define("Health", { hp = 100 })` — 내부적으로 "Lua 테이블 ref 1개짜리" 네이티브 풀 + 스키마 메타데이터. 인스펙터(07)·직렬화(04)는 스키마를 통해 접근.
2. **커스텀 시스템**: `registerSystem(SystemDesc)` — 페이즈 + before/after 제약으로 어느 위치에든 삽입. Lua 시스템은 지정 페이즈에서 배치 호출(쿼리 결과를 Lua로 넘기는 얇은 이터레이터, 성능 민감 시스템은 네이티브 권장을 문서화).
3. **커스텀 렌더러블**: `IRenderExtractor` 등록 — 컴포넌트 타입과 추출 콜백을 등록하면 PhaseRenderExtract가 호출해 RenderItem을 수집(예: 플러그인 제공 스켈레탈 2D, 3D 파티클).
4. **타일 속성 확장**: `TileProps.custom` PropertyBag + 타일 이벤트(`eventId`)의 Lua 핸들러 바인딩. 오토타일 룰 평가기도 인터페이스(`IAutotileRule`)로 교체 가능.
5. **애니메이션 이벤트 핸들러**: `AnimEvent`는 월드-로컬 버스로 발행 — Lua·플러그인·06(오디오 발소리)이 active World의 버스를 구독. 상태 머신 전이 조건에 커스텀 조건 함수 등록 가능.
6. **물리 교체**: `IPhysicsWorld` 인터페이스 — 기본 내장 경량 구현을 플러그인이 Box2D/커스텀으로 교체. 충돌 응답 콜백(`onResolve`) 후킹.
7. **길찾기 비용 보정**: `setCostModifier` — 지역 위험도, 진영별 통행 불가 등 게임 규칙을 코어 수정 없이 주입. A* 자체를 교체하는 `INavProvider`도 확장 단계에서 제공.
8. **프리팹 후처리 훅**: 인스턴스화 완료 시 `OnPrefabInstantiated` 이벤트 — Lua가 절차적 초기화 수행.
9. **에디터 연동(07 경유)**: 등록된 모든 컴포넌트는 04 리플렉션 메타데이터만 있으면 자동으로 인스펙터에 노출 — 본 모듈 쪽 추가 작업 불필요.

---

## 단계별 구현 범위 (MVP → 확장)

**M1 — ECS 코어**
- EnTT를 내장 의존성으로 채택하고 `mye::ecs` 공개 API(64비트 Entity 핸들, registerComponent, Query, CommandBuffer)로 래핑
- 페이즈 스케줄러(단일 스레드, before/after 토폴로지 정렬) — 01 UpdatePhase 틱에 하위 스케줄러로 연결
- LocalTransform/Parent/Children/WorldTransform + TransformSystem

**M2 — 렌더러블·기본 씬**
- SpriteRenderer/MeshRenderer + SortingLayer 레지스트리 + RenderExtract → 02 연동
- SceneManager 단일 씬 로드/언로드(04 연동), 프리팹 v1(중첩·오버라이드 없이 인스턴스화만)

**M3 — 타일맵 v1 (+높이 최소형)**
- 청크 저장(단일 컬럼), 멀티 렌더 레이어, 타일 속성, 타일 AABB 충돌
- heightLevel·SlopeType·FloorLevel 최소형(경사·계단 전이, 단일 컬럼 범위)
- 자체 충돌: Collider2D(AABB·원), 공간 해시, KinematicBody2D, 트리거, 레이캐스트

**M4 — 하이브리드 지형 검증 (02의 M2와 같은 시기)**
- 다중 컬럼 셀(다리 1케이스), FloorLevel 전이, 층 인지 충돌(다리 위/아래)
- FloorLevel→World 밴드 변환을 포함한 렌더 추출 — 02 M2의 "다리 위/아래 검증 씬" 완료 기준을 함께 충족(이 엔진의 최대 기술 리스크를 최우선 검증)
- 그리드 A*((cell, level) 노드), 비동기 요청, 청크 내비 리베이크

**M5 — 애니메이션·캐릭터**
- SpriteAnimClip/DirectionalAnimSet/8방향, 상태 머신, 애니메이션 이벤트

**M6 — 월드 스케일·확장**
- additive 씬 합성, 청크 스트리밍, BillboardRenderer + HD-2D 구도 검증
- 프리팹 v2(중첩·오버라이드), 시스템 병렬 디스패치(선언 액세스 기반), EnTT group 최적화
- HPA*, IPhysicsWorld 플러그인 교체 검증

**수직 슬라이스 이후**
- 페이퍼돌 v1(슬롯 레이어링 + 방향별 z순서 테이블 + Front/Back 서브 슬롯 규약) — 장비 파츠 레이어링은 수직 슬라이스에 불필요한 콘텐츠 기능이므로 강등. 베이크 캐시는 그 이후

---

## 오픈 이슈

1. **2D 캐릭터 기본 정렬 방식** — **해소(02 확정안 채택)**: 02가 확정한 레이어 밴드 + `sortKeyY`(논리 지면 Y) 깊이 인코딩을 그대로 따른다. 본 모듈은 추출 시점에 `sortLayer`·`sortKeyY`를 공급할 뿐 자체 정렬 방식을 두지 않는다(설계 개요 4절).
2. **높이 레벨 양자화 단위**: 1레벨 = 타일 반 칸(24px)? 한 칸(48px)? 테일즈위버 원작 감성 재현에 직결. PPU 48(48px 타일)은 확정(2026-07)되었으므로, 이 기준으로 프로토타입 맵을 만들어 검증 필요.
3. **Lua 컴포넌트 저장 방식 확정**: "네이티브 풀 + Lua 테이블 ref" 안으로 설계했으나, 대량 Lua 엔티티(수천)에서 GC 부담 측정 후 미러링(수치 필드는 네이티브 저장) 방식으로 승격할지.
4. **Children 캐시 유지 여부**: 파생 캐시 대신 매번 Parent 스캔으로 충분할 수도(엔티티 수천 규모). 에디터 트리 성능 요구가 결정 요인 — 07 워크플로우 확정 후 재검토.
5. **본격 물리 필요성**: 기획에 낙하물·넉백·투사체 반사 등 동역학 요소가 얼마나 들어가는지에 따라 Box2D 플러그인을 M6 이전에 당길지.
6. **씬 스트리밍의 실제 필요 규모**: 첫 게임의 맵 크기(한 구역 몇 청크?)가 정해져야 스트리밍(M6)을 앞당길지 뒤로 미룰지 결정 가능.
7. **애니메이션 상태 머신의 소유권 경계**: 복잡한 전투 콤보 로직까지 상태 머신 에셋으로 표현할지, 상태 머신은 시각 상태만 담당하고 로직은 Lua에 둘지(현재 설계는 후자 지향).
8. **EnTT와 DLL 경계**: 플러그인 DLL이 `registerComponent`로 타입을 등록할 때 EnTT의 컴파일 타임 타입 id가 모듈 간에 안정적인지 검증 필요. 문제가 확인되면 동적 등록 경로(`addDynamic`)를 유일한 DLL 경계 통로로 강제하거나, 해당 지점만 자체 구현으로 교체한다(래핑 덕에 공개 API는 불변).
