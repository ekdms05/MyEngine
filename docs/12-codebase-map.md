# 12. 코드베이스 구조 맵 (Codebase Map)

> 실제 소스 코드를 기반으로 작성한 사후 분석 문서.
> 설계 의도는 `00`~`11`번 문서를, 본 문서는 **현재 코드의 실제 구조**를 다룬다.
> (분석 시점: 2026-08-24, 총 소스 ~60,400 라인 / C++·TypeScript, node_modules 제외)

## 1. 프로젝트 개요

**MyEngine** — 테일즈위버 스타일 2.5D 픽셀아트 게임 엔진.
C++20 / DirectX 11 / CMake ≥ 3.26 / MSVC `/W4 /utf-8 /permissive-`.
클라이언트(엔진·에디터)와 서버(MMO 백엔드)를 모두 포함하는 모놀리식 리포지토리다.

| 영역 | 내용 | 규모(라인) |
|---|---|---|
| `engine/` | 엔진 정적 라이브러리 19개 | ~46,700 |
| `game/` | 게임 계층 라이브러리 (social, mmo) | ~2,100 |
| `apps/` | 실행 파일 4개 (editor, game, server, paktool) | ~920 |
| `samples/` | 마일스톤별 데모 8종 | ~4,500 |
| `tests/` | 단위·통합 테스트 (자체 프레임워크, 약 492 케이스) | ~16,300 |
| `tools/` | MCP 개발 도구 서버(TS) + 스크립트 | — |

## 2. 레이어 아키텍처와 모듈 의존성

모든 모듈은 `mye_*` 이름의 **정적 라이브러리**이며 `include/mye/<module>/` 공개 헤더를
PUBLIC으로 노출한다. 의존성은 아래 방향으로만 흐른다(엄격한 양파 구조).

```
L0   core ───────────────────────────────────────────────── 모든 것의 기반 (의존 0)
      │  (수학·로그·JSON·설정·이벤트·모듈/앱루프·win32·입력·잡)
      ├── rhi      (L1, core)            — RHI 추상화 + DX11 백엔드
      ├── reflect  (L1.5, core)          — 리플렉션 + 직렬화 아카이브
      ├── ddc      (core)                — 데이터 주도 컴포넌트(스키마)
      ├── plugin   (core, reflect)       — 플러그인 호스트(정적/DLL)
      ├── imgui    (core, [private rhi]) — Dear ImGui 래퍼
      │
      ├── asset    (L2, core+rhi+reflect)   — 에셋 DB·VFS·임포터·핫리로드
      ├── render   (L2, core+rhi+asset)     — 스프라이트 배치·픽셀퍼펙트·하이브리드 깊이 렌더러
      ├── audio    (L2, core+asset)         — 소프트웨어 믹서 + miniaudio 백엔드
      │
      ├── scene    (L3, core+asset+render, [private reflect])
      │             — ECS·타일맵·물리·애니메이션·내비·렌더 익스트랙트
      │
      ├── script   (L4, core+scene+asset+audio+reflect+ddc) — Lua/sol2 런타임+바인딩
      ├── ui       (L4, core+render+rhi+reflect, [private freetype]) — 인게임 UI + 한글 텍스트
      ├── gameplay (L4, core+scene)        — 순수 RPG 게임플레이 로직(스탯/전투/아이템/…)
      │
      ├── runtime  (L4 오케스트레이터, core+ui+scene+audio+asset+reflect,
      │             [private script+sol2]) — 대화·컷신·NPC·세이브·로컬라이즈·씬 전환
      │
      └── editor   (L5, core+reflect+scene+asset+render, [private imgui])
                  — 리플렉션 기반 ImGui 에디터

  ▼ 서버 계열 (클라이언트 렌더 스택과 독립적인 가지)
      net        (core)                — UDP 프로토콜·비트스트림·예측/조정
      persist    (core)                — 계정/캐릭터/아이템 장부(JSON)
      liveops    (core)                — CVar·기능플래그·가챠·메트릭
      gameserver (core+gameplay+persist+net) — 세션 관리 서버 통합층

  ▼ 게임 계층 (엔진 위, 엔진에 반대로 의존하지 않음)
      game/social (core+persist)       — 친구·채팅·파티·길드·우편·거래·경매·매치메이킹
      game/mmo    (core+gameplay)      — 3직업 파티 사냥 시뮬레이션

  ▼ 실행 파일
      apps/editor  (core+scene+editor)          — 에디터 (실제 로직은 engine/editor)
      apps/game    (core+rhi+asset+render+audio+scene) — 데이터 주도 게임 런타임
      apps/server  (core+net+persist+liveops+gameserver+gameplay) — 헤드리스 게임 서버
      apps/paktool (core+asset)                 — .pak 패키징 CLI
```

특이사항:
- `render`는 `scene`의 `RenderExtract.h` 계약을 **컴파일 전용 include**로만 참조(링크 의존성
  없음 — 순환 제거). `asset`도 `script`/`scene` 헤더를 동일 방식으로 참조한다.
- 클라이언트(`apps/game --connect`)와 서버(`apps/server`)가 실제로 연결된다: 자기 캐릭터는
  클라 예측(CSP)+스냅샷 재조정, 원격 플레이어는 `net::SnapshotInterpolator`(100ms 보간 버퍼)로
  보간 렌더. `apps/server --bots N`은 실제 NetClient 경로를 통과하는 배회 봇(M13 부하테스트 씨앗).

## 3. 프레임 루프 (엔진의 심장)

`core/App.cpp`의 `GuardedMain`이 부팅(크래시 핸들러 → 로그 → 설정 로드 → 모듈 등록 →
윈도우 → 입력 → 모듈 위상정렬 초기화)을 독점하고, **봉인된 메인 루프**를 돌린다:

```
frameDelta = min(clock.Tick, maxFrameDelta)
accumulator += delta × timeScale
frameAllocator.SwapAndReset()          // 2프레임 수명 더블버퍼
input.NewFrame() → PumpMessages() → jobs.PumpMainThreadTasks()
EventBus.Flush(PreUpdate) → Tick(PreUpdate)
while (accumulator ≥ fixedDelta) Tick(FixedUpdate)   // 고정 스텝 시뮬레이션
alpha = accumulator / fixedDelta                       // 렌더 보간용
Tick(Update) → Tick(PostUpdate) → EventBus.Flush(PostUpdate)
Tick(PreRender) → Tick(PostRender)
```

`scene`의 `SystemScheduler` 5상(Input → FixedUpdate → Update → PostUpdate → RenderExtract)가
이 루프의 PreUpdate/FixedUpdate/Update/PostUpdate/PreRender 틱에 매핑되고, 각 상위상 실행
뒤 CommandBuffer와 월드 EventBus가 플러시된다. 시뮬레이션(고정 틱)과 표현(가변 프레임)
분리는 `runtime`의 컷신(NpcSystem·MoveController = sim, CameraFocus = view)에서도 유지된다.

## 4. 모듈별 상세

### 4.1 core — `mye_core` (~5,200 라인)

기반 계층. `Base.h`(FNV-1a 64 해시, `Expected<T,Error>` — 예외 금지 규약의 핵심),
`App.h`(`CreateApplication()` + `GuardedMain`), `Math.h`(Vec/Quat/Mat4, LH +Y up, PPU 48),
`Log.h`(카테고리×심각도, 플러그 가능 싱크), `Events.h`(타입 버스 + 채널),
`Module.h`(`IModule` 수명주기 + 위상정렬 `ModuleRegistry` + `EngineContext` 서비스 게이트웨이),
`Config.h`(Engine < Project < User < RuntimeOverlay 계층 설정), `Jobs.h`(카운터 조인 잡 시스템),
`Input.h`(HID 스캔코드 + Raw Input), `Memory.h`(프레임 할당자), `Json.h`(부트스트랩용 최소 JSON),
`platform/Win32Window·Win32Input.h`(HWND 불투명 `void*` — Windows.h 비누출).
`EngineContext`가 유일한 전역 접근점: 모든 서비스(EventBus·Config·Time·JobSystem 등)를
FNV 해시 `ServiceId`로 등록/조회한다.

### 4.2 reflect — `mye_reflect` (~1,970 라인)

비침습 컴파일타임 리플렉션 + 대칭 직렬화. `TypeBuilder` 플루언트 등록
(`.Version().Field().Attr().Method().Base()`) → `TypeRegistry`(프로세스 전역, 해시 충돌 검사,
플러그인 언로드용 `Unregister`) → `SerializeDynamic`이 임의 `IArchive` 구동(현재 `JsonArchive`,
`__version` 필드 + `RenamedFrom` 마이그레이션). `PropertyPath`("transform.position.x" 형식
경로 주소)는 프리팹 오버라이드와 에디터 Undo가 공유한다.

### 4.3 rhi — `mye_rhi` (~2,320 라인)

WebGPU/DX12 스타일 얇은 추상화: 불투명 세대별 핸들(Buffer/Texture/Pipeline/BindGroup…),
단일 PSO(`GraphicsPipelineDesc`), BindGroup 슬롯 규약(0=PerFrame, 1=PerPass, 2=PerMaterial,
3=PerDraw), 명시적 렌더패스. DX11 백엔드(`src/dx11/Dx11Device.cpp` ~1,400 라인)가 전부:
상태객체 캐시(블렌드/깊이/래스터라이저 해시 키), 섀도 스테이트 바인드 생략,
`kMaxFramesInFlight=2` 지연 파괴, 리드백 스테이징, 타임스탬프 쿼리, BMP 캡처(테스트용).
DX12/Vulkan 백엔드 추가가 설계 목표.

### 4.4 asset — `mye_asset` (~4,570 라인)

에셋 파이프라인: VFS(loose + 우선순위 마운트 + `.pak`), 128비트 GUID + `.meta` 사이드카,
슬롯 테이블 `AssetManager`(동기 + 비동기 Parse[워커]/Finalize[메인, GPU 업로드] 2단계),
임포터(PNG·glTF·Aseprite/스프라이트시트·WAV/OGG), MaxRects 아틀라스 패커, 핫리로드
(ReadDirectoryChangesW → 디바운스 → `ReimportPath` → 기존 슬롯에 포인터 스왑 →
`AssetReloadedEvent` → 역의존성 전파). 핸들이 슬롯을 참조하므로 리로드 후 소비자가 자동 갱신된다.

### 4.5 render — `mye_render` (~2,400 라인)

- `Camera2D` — 직교 카메라, 픽셀 스냅 + 서브픽셀 잔여 회수, 데드존 추적, 월드 경계 클램프.
- `SpriteBatch` — 쿼드 배처, CPU Y-sort, 텍스처 변경 시 드로우 분할.
- `PixelPerfectTarget` — 960×540 내부 RT → 정수 스케일 레터박스 업스케일.
- **`DepthEncoder` — 엔진의 핵심 난제 해결**: 2D 스프라이트와 3D 메시를 **하나의 깊이 버퍼**에
  정렬한다. (sortLayer, sortKeyY, orderInLayer) → NDC z 밴드 인코딩. 다리 위·아래 동시
  통행, 3D 조형물 뒤 오클루션이 픽셀 단위로 정확하다.
- `HybridRenderer` — `scene::RenderProxyList`를 소비하는 최종 렌더러(임베디드 HLSL).

### 4.6 audio — `mye_audio` (~1,400 라인)

RHI 철학을 오디오에 적용: 백엔드 추상(`IAudioSource::PullAudio`) + miniaudio 구현.
순수 `SoftwareMixer`(버스 트리 Master←BGM/SFX/UI/Voice, 게인 램프, 피치 리샘플, 2.5D 감쇠·패닝,
결정론적 오프라인 렌더링 = 테스트 가능), `AudioCue`(랜덤/순차, 폴리포니 제한),
`MusicPlayer`(2보이스 BGM 크로스페이드). 설정 동기화(audio.* 키)와 헤드리스 무음 모드 내장.

### 4.7 scene — `mye_scene` (~5,100 라인)

월드 계층의 중심:
- **ECS** — EnTT 스타일 스파스셋 풀, 64비트 (index|generation) 엔티티, 비템플릿 동적 컴포넌트
  등록(플러그인/Lua용), 지연 구조 변경 `CommandBuffer`, 다중 컴포넌트 `View`.
- **타일맵** — 32×32 청크, 셀당 다중 컬럼(다리), 정수 높이 + 경사(완만/급/계단),
  높이 보간 `SampleHeight`. PPU 48, 높이 레벨 1 = 0.5 월드 단위.
- **물리** — AABB/원 콜라이더, 공간 해시 브로드페이스, move-and-slide(고정 스텝),
  층 비트 마스크(같은 XY의 다리 위·아래 비충돌), 트리거 enter/exit 이벤트 diff.
- **애니메이션** — 8방향 방향성 세트(flipX 미러링), 데이터 주도 상태머신, 프레임 이벤트
  마커(footstep 등 → 월드 버스).
- **내비게이션** — A* + JobSystem 기반 비동기 경로 요청 허브.
- **RenderExtract** — 렌더러 소유 정렬을 위한 순수 데이터 제공자(`RenderProxyList`).

### 4.8 script — `mye_script` (~2,980 라인)

Lua 5.4 + sol2 단일 VM. 게임 로직 = "데이터(에셋) + Lua" 철학. 엔티티당
`ScriptComponent`(클래스 테이블 + self 인스턴스, 콜백 비트마스크), 보호된 호출 에러 격리
(`ScriptErrorEvent` — file:line), **핫리로드 시 self.state 생존**(문법 에러 시 구 클래스 유지),
코루틴 스케줄러(`mye.co.*`). 바인딩 7종(Math/Ecs/Input/Audio/Event/Reflect/Ddc) 중
`ReflectBindings`는 리플렉션 메타를 그대로 노출 — 플러그인 타입도 Lua에서 바로 접근 가능.
sol2는 바인딩 .cpp TU에만 격리(`/W3 /bigobj` 벤더 경계 격리).

### 4.9 ui — `mye_ui` (~4,170 라인)

ImGui가 아닌 **인게임** UI 스택:
- `mye::text` — FreeType 래스터라이즈, 동적 글리프 아틀라스(한글 2,350음절 웜업 내장),
  리치 텍스트 태그, 한글 글자단위 + 라틴 단어단위 줄바꿈(금칙처리 포함) 레이아웃.
- `mye::ui` — 유지 위젯 트리(Panel/Label/Button/TextInput/ScrollView/ListView…),
  AnchorRect 레이아웃, 터널→타깃→버블 라우팅 이벤트, 9슬라이스 스킨, **UI-as-data**
  (`UiDocument` JSON → `WidgetFactory` — 커스텀 위젯 플러그인 포인트).

### 4.10 runtime — `mye_runtime` (~3,300 라인)

게임 런타임 오케스트레이터(M6): 대화 그래프(분기 선택, 로컬라이즈 키) + `DialogueBox`,
Lua 코루틴 컷신 백엔드(say/choose/move_to/camera.focus), NPC 배회/상호작용(상호작용 중
이동 고정, on_interact = Lua 코루틴), 참여자 기반 버전 슬롯 세이브(원자적 기록 + 마이그레이션
체인), 로케일 시스템(ko/en 폴백 체인), 페이드/로딩 씬 전환 상태머신. 하나의 `IModule`로
ui·audio·scene·script 서비스를 묶고 sim/view 틱을 분해 배치한다.

### 4.11 editor — `mye_editor` (~6,700 라인, 최대 모듈)

같은 엔진 스택 위에 얹힌 "특권 앱". 모든 편집은 `IEditorCommand` → 문서별 `CommandStack`
(트랜잭션, 400ms 드래그 병합, 512 엔트리)로 — 패널은 월드를 직접 건드리지 않는다.
리플렉션 자동 인스펙터(타입별 드로어/컴포넌트 에디터 확장점), 선택 관리자(이력 포함),
**플레이 모드**(편집 월드 스냅샷 → 별도 플레이 월드 구축, 정지 시 버림), 프리팹(서브트리 +
PropertyPath 오버라이드), 패널/메뉴/툴바 확장 레지스트리("빌트인 = 퍼스트클래스 플러그인"),
콘텐츠 도구(타일맵 편집 — 47타일 blob 오토타일, 애니메이션 편집, 도트/픽셀아트 에디터).
`EditorModule`이 RHI/스왑체인/Docking 셸을 소유하고 오프스크린 뷰포트를 렌더링한다.

### 4.12 plugin / ddc / imgui

- **plugin** (~280) — 정적/DLL 플러그인 호스트. 버전 게이트, 대칭 언로드(등록 역순 +
  TypeRegistry 반환), DLL 경계에서의 생성/파괴 소유권 규약.
- **ddc** (~550) — JSON 스키마로 컴포넌트/스탯 타입을 **재컴파일 없이** 정의.
  SchemaRegistry → DynamicComponent → DynamicComponentStore(월드 JSON 저장). Lua
  `mye.ddc.*`와 연결되어 데이터 주도 게임 로직의 기반.
- **imgui** (~480 래퍼 + 벤더 소스) — Dear ImGui(docking) DX11/win32 통합, 단일 인스턴스
  규약, `WantCaptureKeyboard/Mouse` → InputState 게이팅, 한글 폰트 로딩, 다크 스킨.

### 4.13 서버 계열 — net / persist / liveops / gameserver

- **net** (~1,050) — UDP + 비트레벨 직렬화 + 위치 16비트 양자화, 서버 권위 이동 시뮬레이션,
  클라 미리예측 + 스냅샷 조정(미승인 입력 재생), 원격 엔티티 보간(`SnapshotInterpolator`),
  입력 위반 카운터 자동 킥. 인증은 콜백 주입(`Authenticator`) — net은 persist를 모른다.
- **persist** (~1,060) — 계정(솔트 해시, 단일 세션 토큰, 밴), 캐릭터 POD 레코드,
  **추가 전용 ItemLedger**(모든 아이템/골드 이동 기록, 잔액 리플레이 재구성, 무결성 검증).
- **liveops** (~440) — 핫리로드 CVar/기능플래그, 점검 모드, 확률 공시·가챠 테이블(한국 확률
  공시 규제 대응 JSON), 메트릭 레지스트리.
- **gameserver** (~325) — NetServer + GameServer + persist를 잇는 세션 관리: 접속 시
  레코드 → 게임플레이 컴포넌트 로드, 종료 시 저장, 아이템/골드 변경은 장부와 항상 정합.

### 4.14 gameplay — `mye_gameplay` (~1,020 라인)

순수하고 결정론적인 RPG 로직 라이브러리(GPU/네트워크 무관, 서버·클라 공용):
스탯(기본+수정자→유도 공식 단일 권위), 시드 RNG 전투, 아이템 카탈로그/인벤토리(듀프 방지
원자 이동), 루팅, 성장 곡선, 스킬 쿨다운, 상태이효(sourceId 정밀 제거), 퀘스트, 경제,
제작. 자체 스케줄러 없이 호스트가 `RunStatSystem` 등을 구동한다.

### 4.15 game/ — social (~1,670) / mmo (~530)

- **social** — 로그인 큐, 친구/차단/프레즌스, 채널 채팅(금칙어), 파티, 길드(권한 위계)·길드
  뱅크, 우편, 1:1 거래, 경매장, 역할 매치메이킹. **경제 척추**: 모든 가치 이동이 ItemLedger의
  시스템별 합성 에스크로 계좌(상위 비트 ID 공간)를 통해서만 일어나 총량 보존·듀프 불가.
  현재는 테스트만 소비(앱 미연결).
- **mmo** — 3직업(검사/마법사/버퍼) 파티 사냥 시뮬레이션, 몬스터 카탈로그, 리스폰.
  결정론적 시뮬레이션 = 서버 권위 + 단위 테스트 용도.

### 4.16 apps/

| 앱 | 진입점 | 역할 |
|---|---|---|
| `apps/editor` | `GuardedMain` | 얇은 셸(106라인) — 실제 로직은 `engine/editor`의 EditorModule |
| `apps/game` | `GuardedMain` | 에디터 없이 프로젝트(씬+에셋)를 로드·렌더하는 게임 런타임. `--make-hunt` 등 CI 검증 CLI |
| `apps/server` | 일반 `main` | 헤드리스 콘솔 서버: 고정 dt 틱, 5초 주기 설정 핫리로드+메트릭 기록, 순환 백업 저장, 관리자 원샷 모드 |
| `apps/paktool` | 일반 `main` | `pack`/`list` — 배포용 .pak 패키징 |

### 4.17 samples/ — 마일스톤 데모

모든 샘플이 `--frames N --dump bmp` 자기검증 CLI를 갖는다(보이는 결과로 마일스톤 게이트).

| 샘플 | 라인 | 증명 내용 |
|---|---|---|
| hello_triangle | 377 | M0 — DX11 삼각형, 리사이즈 복원, 누수 없는 종료 |
| asset_smoke | 235 | PNG → GPU 업로드 → 리드백 픽셀 왕복(프리멀티플라이 검증) |
| sprite_demo | 576 | M1 — WASD 도트 캐릭터, 고정 스텝 + 렌더 보간 |
| bridge_demo | 783 | M2 — **하이브리드 깊이**: 다리 위/아래 + 3D 오클루션 |
| character_demo | 935 | M3 — 8방향 상태머신, 발소리 이벤트, Lua 핫리로드 |
| village_demo | 1,639 | M6 — 종단 수직 슬라이스: 마을·한글 대화·컷신·NPC·BGM |
| game_sample / mmo_demo | 에셋 | 데이터 주도 프로젝트 디렉터리(apps/game가 로드) |

### 4.18 tests/ — `mye_tests` (~16,300 라인, 약 492 케이스)

서드파티 금지 규약에 따른 자체 프레임워크(`MYE_TEST` 정적 등록 + `MYE_EXPECT`).
단일 실행 파일이 전 영역 커버: 코어/수학/JSON/잡, 에셋(핫리로드 포함), 리플렉션·ECS·씬·
타일맵·물리·내비, 스크립트(바인딩·리플렉션·ddc), 에디터(명령/Undo/플레이모드/e2e 워크플로우),
UI/텍스트(레이아웃·한글), 런타임(세이브/로케일/대화/컷신/NPC), 게임플레이 RPG 전체,
네트워킹(복제·예측·인증·게임서버 통합), 퍼시스턴스, 소셜/경제 전체, MMO 콘텐츠, 라이브옵스,
확장성(플러그인 DLL 로드 fixture 포함). CTest 등록.

### 4.19 tools/

- **mcp/** — TypeScript MCP 서버: AI 에이전트가 빌드·테스트·실행하고 **렌더된 프레임을
  이미지로 보게** 한다. 도구: engine_build/test/run/capture_frame/logs, project_status,
  dot_write_sprite/dot_from_photo. CLI/파일 경계만 사용(엔진 미연결), 모든 자식 프로세스
  타임아웃+트리 킬.
- gen_cube_glb.py, gen_mmo_sprites.ps1(48×48 스프라이트 생성), hang_stress.sh(종료
  데드락 포착), package/.

## 5. 핵심 데이터 흐름

**에셋 → 드로우**: vpath → VFS(loose/pak) → 임포터(DecodePng 등, 워커) → Finalize(GPU 업로드)
→ AssetSlot 커밋 → `AssetHandle<T>` → render/audio 소비. 핫리로드는 슬롯 포인터 스왑.

**씬 → 픽셀**: ECS 쿼리 → RenderExtract(RenderProxyList) → HybridRenderer가
DepthEncoder로 (layer, sortY, order) → NDC z 인코딩 → 스프라이트(컷아웃 깊이 기록)와 3D
메시(앵커 평탄화/바이어스/실제 깊이)가 **하나의 깊이 버퍼**에서 교차 → 960×540 내부 RT →
정수 업스케일.

**오디오**: AudioClip → ClipSource(상주 PCM/OGG 스트리밍) → PostCue(랜덤 변주·폴리포니) →
MixerVoice → 믹서(리샘플·감쇠·패닝·페이드·버스 게인) → miniaudio 풀 콜백. 결정론적
오프라인 렌더링으로 테스트.

**게임 로직**: Lua 스크립트 컴포넌트(에셋) → 보호된 콜백(에러 격리) → mye.* 바인딩 →
EcsBindings는 구조 변경을 CommandBuffer로 지연. ddc 스키마 + `mye.ddc.*`로 재컴파일
없는 데이터 주도 컴포넌트.

**서버**: 클라 입력(양자화) → NetServer 권위 시뮬 → 스냅샷 브로드캐스트 → 클라 예측 조정.
세션 종료 시 persist 기록. 모든 경제 이동은 ItemLedger 에스크로 경유.

## 6. 코드 규약 (관찰된 것)

- 네임스페이스 `mye` + 모듈별 중첩(`mye::refl`, `mye::scene`, …). 경로 = 네임스페이스.
- **예외 금지** — 모든 실패 가능 API는 `[[nodiscard]] Expected<T, Error>`.
- 단정 3단계: `MYE_ASSERT`(디버그) / `MYE_VERIFY`(항상 평가) / `MYE_CHECK`(치명).
- 모든 경계 ID(이벤트·서비스·타입·컴포넌트·스키마)는 이름의 FNV-1a 64 해시 — DLL 경계
  안정성. 매크로(`MYE_EVENT`, `MYE_COMPONENT`, `MYE_REFLECT`, …)로 심는다.
- UTF-8 전역, Win32 W-API 경계에서만 `Widen/Narrow`.
- 대부분의 시스템 클래스는 Pimpl + 복사 금지. 공개 헤더에 Windows.h/sol/FreeType 비누출.
- 등록 순서 역순 해제(모듈·서비스·플러그인·리플렉션 타입), RAII 헬퍼(`ScopedSubscription`).
- 헤더마다 한국어 목적 배너 + `docs/NN` 참조 + 마일스톤(M0~M6) 마커.
- 벤더 경계 격리: ImGui는 `/W0`, sol2/FreeType은 특정 TU에 봉쇄.

## 7. README 대비 차이 (최근 확장 방향)

README의 저장소 레이아웃은 M6 시점 기준이다. 이후 `docs/mmorpg/` 계획에 따라 **MMO 서버
스택**(net·persist·liveops·gameserver·gameplay·game/social·game/mmo·apps/server)과
**플러그인/DDC 확장성 계층**(plugin·ddc), apps/(editor·game·paktool)가 추가되었다.
README의 모듈 표를 업데이트하면 새 기여자 온보딩에 도움이 될 것이다.
