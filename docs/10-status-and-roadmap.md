# 10. 구현 현황 & 로드맵 (Status & Roadmap)

> **"지금 무엇이 구현되어 있고, 앞으로 무엇을 어떤 순서로 구현할 것인가"**를 체계화한 종합 문서.
> 상위 엔진 설계([00](00-overview.md)~[08](08-mcp.md))와 픽셀 MMORPG 도메인 설계([mmorpg/00](mmorpg/00-overview.md)~[mmorpg/09](mmorpg/09-liveops-security.md))를 잇는 **실행 계획**이다.
> Part A(현황 매트릭스) → Part B(갭 요약) → Part C(마일스톤 M7+) → Part D(즉시 착수 3선).
> 성숙도 표기: **Strong**(프로덕션급 동작·테스트), **Partial**(구현됐으나 미배선/제약), **Missing**(부재/스텁/계약만).

---

## Part A. 현재 구현 매트릭스

엔진 인벤토리 종합. 근거는 소스 경로·테스트로 검증된 항목만 기재.

### A-1. Core · Platform · Input · Jobs (`engine/core`) — 전반 Strong

| 기능 | 성숙도 | 근거 |
|---|---|---|
| 자체 수학(Vec/Mat4/Quat/Rect/Color, 픽셀스냅) | Strong | `Math.h/.cpp`, MathTests |
| Expected<T,E> 예외불사용·FNV-1a 해시·EventTypeId/ServiceId | Strong | `Base.h`, CoreTests static_asserts |
| EventBus(즉시 Publish·스레드세이프 Enqueue·우선순위·재귀가드) | Strong | `Events.cpp`, ScopedSubscription |
| Time(QPC ns Clock·고정 60Hz·보간 alpha) | Strong | `Time.cpp` |
| 봉인된 고정스텝 메인 루프(accumulator·spiral clamp) | Strong | `App.cpp` GuardedMain |
| 모듈 라이프사이클·위상정렬·EngineContext 서비스 게이트웨이 | Strong | `Module.cpp`, TopoSorter |
| Config(레이어 JSON)·Log(카테고리×심각도)·Assert 3종 | Strong | `Config.cpp`·`Log.cpp`·`Assert.cpp` |
| win32 Window(보더리스풀스크린·메시지훅체인·DPI) | Strong | `Win32Window.cpp` |
| Raw Input(물리 스캔코드·UTF-8 WM_CHAR·IME) · XInput 4패드 | Strong | `Win32Input.cpp`, InputTests |
| JobSystem(Compute/IO 큐·카운터조인·busy-help·안전 셧다운) | Strong | `Jobs.cpp`, JobsShutdownIdleNoHang |
| FrameAllocator(더블버퍼 범프) | Strong | `Memory.cpp` |
| Crash 핸들러(리치 필터는 dead code, 인라인 필터만) | Partial | `Assert.cpp` InstallCrashHandler 미호출, 미니덤프 없음 |
| ConfigChangedEvent(SetEventBus가 부트에서 미호출) | Partial | GuardedMain 미배선 |
| **네트워킹(소켓·비트스트림·채널·RPC·와이어 프로토콜)** | **Missing** | core 전체에 0건 |
| **네트워크 시계(서버권위·RTT·offset 동기)** | **Missing** | TimeSystem은 로컬 렌더 시계뿐 |
| OS 추상화 seam(Linux 헤드리스 서버 빌드) | Missing | Time/Config/Input/Log/App이 Windows.h 하드코딩 |
| Arena/Pool 할당자·메모리 태그 추적 | Missing | FrameAllocator만, MemoryTag 무시 |
| EnumerateDisplays·work-stealing·비동기 로깅 | Missing | 스텁/Phase 2 |

### A-2. RHI · Render (`engine/rhi`, `engine/render`) — Strong 코어 + 핵심 스텁

| 기능 | 성숙도 | 근거 |
|---|---|---|
| DX11 디바이스·핸들풀(gen 검증)·지연파괴 큐 | Strong | `Dx11Device.cpp` |
| 리소스 생성·PSO 분해·상태캐시·FLIP_DISCARD 스왑체인 | Strong | `Dx11Device.cpp` |
| CopyTexture/WriteTexture·CaptureBackbuffer(BMP) | Strong | `Dx11Device.cpp` |
| Camera2D(픽셀스냅·서브픽셀잔차·Screen↔World·Follow) | Strong | `Camera2D.cpp` |
| PixelPerfectTarget(내부 RT·정수배 레터박스 업스케일) | Strong | `PixelPerfectTarget.cpp` |
| SpriteBatch(Y-sort·premultiplied·텍스처런 분할·머티리얼 캐시) | Strong | `SpriteBatch.cpp` |
| HybridRenderer(단일 뎁스버퍼 2D+3D·DepthEncoder·AnchorBiased) | Strong | `HybridRenderer.cpp`·`DepthEncoder.cpp` |
| 동적버퍼 MAP_WRITE_DISCARD(부분 링 미구현) | Partial | `MapDynamic` 매핑마다 전체 DISCARD |
| 타일 청크 깊이(아틀라스 UV는 전체텍스처 placeholder) | Partial | `BuildTileChunkQuads` UV 미배선 |
| 3D 라이팅(단일 방향광+앰비언트 Lambert) | Partial | `kMeshHlsl`, 그림자·다중광원 없음 |
| **GPU 리드백(CopyTextureToBuffer/EnqueueReadback) — ID픽킹** | **Missing** | DX11 빈 반환 스텁 |
| **GPU 타임스탬프(WriteTimestamp/ResolveTimestamps)** | **Missing** | 빈 본문 스텁 |
| **인스턴싱·StructuredBuffer·인스턴스 VB 슬롯** | **Missing** | CreateBuffer Structured 거부 |
| **2D 라이팅·파티클·포스트프로세싱** | **Missing** | render 모듈에 0건 |
| 반투명 back-to-front 패스 | Missing | 하드 알파 cutout만 |
| DX12/Vulkan 백엔드 | Missing | 주석만 |
| RHI·Render 전용 단위테스트 | Missing | DepthEncoder/Camera2D 테스트 0건 |

### A-3. Scene · ECS · Physics · Animation · Nav (`engine/scene`) — Strong 서브스트레이트

| 기능 | 성숙도 | 근거 |
|---|---|---|
| 스파스셋 ECS·64비트 엔티티·런타임 컴포넌트 등록 | Strong | `ComponentPool.cpp`, SceneTests |
| View 교집합·5페이즈 스케줄러(위상정렬)·SceneModule 배선 | Strong | `View.inl`·`SystemScheduler.cpp` |
| Transform 계층(깊이정렬·dirty전파·keepWorld 재부모화) | Strong | `Transform.cpp` |
| 타일맵(32×32 청크·다중컬럼·높이·경사·다리) | Strong | `Tilemap.cpp`, TilemapTests |
| RenderExtract(Sprite/Mesh/Tile→RenderProxyList) | Strong | `RenderExtract.cpp` |
| 충돌 내로우페이즈·공간해시 broadphase·move-and-slide·트리거 | Strong | `PhysicsWorld2D.cpp`, PhysTests |
| 8방향 애니(상태머신·무손실 이벤트)·그리드 A*(동기/비동기) | Strong | `AnimationSystem.cpp`·`Pathfinding.cpp` |
| CommandBuffer(deferred create가 즉시 mutate) | Partial | 진짜 deferral은 M3 |
| Raycast(broadphase 가속 없음·선형스캔) | Partial | `PhysicsWorld2D.cpp` |
| **타일-바디 충돌·snapToGround(ITileCollision)** | **Missing** | 순수가상, 구현 없음, snapToGround 미판독 |
| **SceneManager·프리팹 인스턴스·씬 (역)직렬화·additive** | **Missing** | Scene.h 주석 시그니처만 |
| **컴포넌트 변경추적/dirty 이벤트(복제·영속 전제)** | **Missing** | 아무도 발행 안 함 |
| 시스템 병렬 디스패치(reads/writes 선언만) | Missing | RunPhase 직렬 |
| interest management·AoI·네트워크 안정 ID | Missing | 로컬 단일 World 컴퓨트 |

### A-4. Asset · Audio (`engine/asset`, `engine/audio`) — Strong 로직 + 상위 배선 공백

| 기능 | 성숙도 | 근거 |
|---|---|---|
| VFS(loose+pak 마운트)·.pak 패킹·paktool | Strong | `FileSystem.cpp`·`PakFile.cpp` |
| PNG/glTF/WAV/OGG/Aseprite/그리드시트 임포터·AtlasPacker | Strong | `*Importer.cpp`·`AtlasPacker.cpp` |
| 128비트 GUID·AssetManager 슬롯테이블·AssetHandle·비동기 3단계 로딩 | Strong | `AssetManager.cpp`, LoadAsync 테스트 |
| SoftwareMixer·AudioEngine·MusicPlayer 크로스페이드·miniaudio 백엔드 | Strong | `SoftwareMixer.cpp`·`AudioEngine.cpp` |
| AudioModule 라이프사이클(오디오는 모듈 배선됨) | Strong | `AudioModule.cpp` |
| .meta/GUID(파서 완비, 런타임 미기록 → 매 로드 새 GUID) | Partial | AssetMeta 미호출, `Generate()` 매번 |
| 핫리로드(FileWatcher+AssetDatabase 구현·테스트, 미구동) | Partial | StartWatching 어디서도 호출 안 함, 서비스 미등록 |
| AudioCue(voice stealing 미구현·직렬화 없음)·스트리밍 OGG frameCount=0 | Partial | PostCue 단순 거절 |
| **엔진 레벨 AssetModule(VFS 마운트·임포터 등록·수명)** | **Missing** | 각 sample main.cpp 하드코딩 |
| 지연 GC·Pin·임포트 설정 주입·서브에셋 등록·임포트 캐시 | Missing | refCount 0 즉시 언로드 |
| 원격 스트리밍·CDN·델타패치·버전드 pak·서명 | Missing | pak 평문 |
| 오디오 DSP·리버브존·오클루전·다이나믹 뮤직·AI 생성 | Missing | MusicPlayer는 2보이스 크로스페이드만 |

### A-5. Script · UI · Runtime · Editor · MCP (`engine/script·ui·runtime·editor`, `tools/mcp`) — Strong 툴킷 + MMO 갭 집중

| 기능 | 성숙도 | 근거 |
|---|---|---|
| Lua 런타임·ScriptComponent·에러격리·핫리로드·코루틴·5바인딩모듈 | Strong | `ScriptRuntime.cpp`·`ScriptSystem.cpp`·`bindings/*` |
| 인게임 UI 위젯·입력 라우팅·FreeType 텍스트 스택(한글 줄바꿈·폴백) | Strong | `Widgets.cpp`·`UiSystem.cpp`·`text/*` |
| 대화·세이브(참여자 패턴)·NPC(배회 상태기계) | Strong | `DialogueSystem.cpp`·`SaveSystem.cpp`·`NpcSystem.cpp` |
| 에디터 셸(도킹·메뉴·단축키·레이아웃 영속)·플레이모드 이중 World 격리 | Strong | `EditorApp.cpp`·`PlayMode.cpp` |
| 씬 직렬화·리플렉션 인스펙터·Undo 커맨드·콘텐츠 패널·i18n(ko/en/ja/zh) | Strong | `SceneSerializer.cpp`·`Inspector.cpp`·`Command.cpp` |
| MCP 8툴(build/test/run/capture/logs/status + dot_write_sprite/dot_from_photo) | Strong | `tools/mcp/src/*` |
| 자체 테스트 327 MYE_TEST·40파일 | Strong | `tests/*` |
| 컷신 런타임(A* 미사용·직선 이동만) | Partial | `CutsceneRuntime.cpp` |
| 씬 전환 상태기계(SceneLoaderFn 실로더 미배선) | Partial | `SceneTransition.cpp`, 테스트 mock만 |
| 로컬라이제이션(런타임 ko/en 2로케일만) | Partial | `Localization.cpp` |
| 에디터 확장(Gizmo/Overlay/Tool 골격만) | Partial | `ExtensionRegistry.cpp` |
| SceneSerializer가 editor 모듈에 갇힘(runtime 미사용) | Partial | 런타임 재사용 불가 |
| **데이터드리븐 게임 런타임 앱(apps/game)** | **Missing** | CreateApplication은 editor/paktool만, village_demo=1639줄 하드코딩 |
| **UI TextInput/ScrollView/ListView/GridView** | **Missing** | 이벤트 타입만, 위젯 부재 → 채팅·인벤·거래 불가 |
| **게임플레이 프레임워크(스탯·전투·인벤·퀘·경제·파티·길드·채팅)** | **Missing** | engine/runtime에 0줄 |
| **넷코드·서버·멀티플레이어** | **Missing** | 프로세스 내 로컬 디스패치만 |
| 리플렉션 기반 범용 컴포넌트 Lua 접근 | Missing | LuaEntity는 Transform/Body/Animator만 |
| AI 이미지·오디오·멀티제공자 생성 연동 | Missing | dot 툴(정적 스프라이트)만 |

---

## Part B. 목표(픽셀 MMORPG) 대비 갭 요약

현황을 목표에 겹치면, 엔진은 **"단일 머신 싱글플레이 콘텐츠 제작 툴킷"으로는 성숙**하나 MMORPG의 세 축이 통째로 비어 있다.

### B-1. 갭 3대 축

| 축 | 상태 | 핵심 결손 |
|---|---|---|
| **① 싱글플레이 완성도** | 부분 | 게임 런타임 앱·데이터드리븐 씬 로딩·타일 UV·타일충돌·2D 라이팅·파티클·게임플레이 코어가 부재 → 서버 얹기 전에 먼저 채워야 함 |
| **② 온라인화** | 전무 | 넷코드(전송·복제·예측/재조정)·서버 스택·영속화 DB·계정/세션·네트워크 안정 ID가 0건 |
| **③ 라이브 서비스·확장** | 전무 | 스탠드얼론 exe·쿡/패치·텔레메트리·크래시·안티치트·채팅/제재·결제/확률공개·컴플라이언스·AI 생성 파이프라인 부재 |

### B-2. 순서를 강제하는 선행 블로커 (의존성)

1. **안정 GUID·AssetModule·핫리로드 배선** — 콘텐츠 파이프라인 전제. 미배선이면 씬/프리팹/DB/생성이 에셋을 안정 참조 불가.
2. **게임 런타임 앱(apps/game) + 데이터드리븐 씬 로딩** — 넷 클라·게임플레이·라이브옵스가 배선될 부트스트랩. 없으면 전부 hardcoded C++.
3. **타일-바디 충돌(ITileCollision)** — 서버 이동 검증·클라 예측 일치의 전제. 없으면 월핵·예측불일치 방어 불가.
4. **OS 추상화 seam** — Linux 헤드리스 서버 빌드의 선행. core의 win32 하드코딩 제거 없이는 server/ 컨테이너가 win32에 묶임.
5. **크로스컴파일 결정론 검증** — "서버=클라 동일 시뮬" 전제의 실증. 깨지면 넷코드 재조정 전체가 흔들림([mmorpg/00](mmorpg/00-overview.md) §8).

### B-3. 적대적 검수가 드러낸 "설계 자체의 공백" (로드맵 반영 필수)

- **소유권 공백**: 서버 토폴로지·복제 스키마·소셜 그래프·온보딩/리텐션이 어느 문서에도 소유되지 않음([mmorpg/00](mmorpg/00-overview.md) §7.2).
- **경계 dupe·크로스샤드 일관성**: RAM↔DB 권위 경계·핸드오프·saga가 단어 수준.
- **스케일 정량 미검증**: 존당 수천·광장 500명의 복제 예산 실측 부재.
- **AI 생성 3중 추상화 균열**: IGenProvider/IAudioGenProvider/IAiProvider 미통합 → 게이트웨이 우회.
- **제품 레이어 부재**: 튜토리얼·리텐션 루프·매치메이킹·GM 도구 깊이·모더레이션.

---

## Part C. 단계별 마일스톤 (M7 → M13)

> **원칙**: 상위 엔진 로드맵은 [M0~M6](00-overview.md#7-전역-마일스톤-로드맵-m0--m6)까지 정의되어 M6(수직 슬라이스)에서 싱글플레이 데모를 완성한다.
> 여기서는 **M7부터** 픽셀 MMORPG로의 승격을 의존성 순으로 정의한다.
> 현실적 순서: **먼저 싱글플레이 완성(M7)→게임플레이 코어(M8)→온라인 기반(M9)→영속화·계정(M10)→라이브옵스(M11)→소셜·리텐션·컴플라이언스(M12)→스케일·최적화(M13)**.
> **AI 에셋/오디오/멀티AI·콘텐츠 도구는 병행 트랙(T)**으로 M7부터 상시 진행.
> 각 마일스톤 게이트는 "눈으로 확인 가능한 데모/측정"이다.

---

### 실제 구현 진척 (2026-07, Windows-native 경로)

> 아래 마일스톤 상세는 **이상적 목표(Postgres/Redis·Linux 헤드리스)**를 담는다. 실제 진행은
> 사용자 결정에 따라 **윈도우 네이티브·외부 DB 없이 파일 기반**으로 선(先)구현하고, 스케일용
> Postgres/Redis·컨테이너는 M13에서 승격한다. **M7~M11 선구현 완료(2026-07, 테스트 402/402)**. 현재 상태:

| MS | 상태 | 실제 구현(파일 기반/Winsock) | 차이(이상 대비) |
|---|---|---|---|
| M7 | **Strong** | `apps/game` 데이터드리븐 런타임·SceneSerializer 승격·씬 로딩 | 타일 UV·2D 라이팅·파티클 일부 잔여 |
| M8 | **Strong** | `engine/gameplay`: 스탯·전투·인벤(원자적)·루트·경험치·스킬·상태이상·퀘·경제·제작 (전부 유닛테스트) | AI/스폰·ContentDB·Lua 배선 잔여 |
| M9 | **Strong** | `engine/net`: BitStream·양자화·델타·**Winsock UDP**·권위 서버·스냅샷 복제·**인증 커넥트**·안티치트 경계클램프 (127.0.0.1 루프백 검증) | 클라 예측/재조정·AoI·암호화 잔여 / Linux 대신 Winsock |
| M10 | **Strong** | `engine/persist`: AccountStore(로그인/세션/밴)·CharacterStore·**ItemLedger(보존·dupe차단·무결성)**·PersistenceService 파사드(백업/롤백)·MyServer 배선 | **JSON 파일 원장**(Postgres 대신)·Redis 캐시 미도입 → M13 승격 |
| M11 | **Strong** | `engine/liveops`: ServerConfig(CVar/피처플래그/점검모드/핫리로드)·**GachaTable(확률공개·천장·감사)**·MetricsRegistry(텔레메트리)·안티치트·계정밴·세이브백업/롤백·우아한종료·**배포 패키징 스크립트** | .mpak zstd/서명·런처/CDN·컨테이너 블루그린 잔여 → M13 승격 |

**M10 게이트 달성**: 계정 등록→로그인(세션)→캐릭터 생성/월드상태 저장→아이템 거래(원장 보존)→
재부팅 시 진행 보존, 동시 이동은 원장 잔고 검증으로 dupe 0. 서버가 등록 계정에 대해 자격증명 인증.

**M11 게이트 달성**: 서버 config.json 으로 드랍률/점검모드 무중단 조정(핫리로드), 안티치트가 월드 경계
이탈·범위초과 입력 차단, GM 밴이 로그인·세션 차단, 확률형 아이템 확률공개 JSON·천장·감사 로그,
자동저장 백업 회전으로 롤백(PIT), metrics.json 관찰성, Ctrl+C 우아한 종료, 배포 번들(exe+pak+config) 생성.
현재 테스트 **402/402** 통과.

---

### M7 — 싱글플레이 런타임 완성: 데이터로 게임이 부팅된다
**목표**: 하드코딩 없이 프로젝트(씬·에셋·스크립트)를 로드해 에디터 없이 실행되는 게임 exe를 만들고, 픽셀 렌더의 결손(타일 UV·라이팅·파티클)을 채운다.

**핵심 항목**
- **P0** 안정 GUID·.meta 영속 배선 + 엔진 레벨 `AssetModule`(VFS 마운트·임포터 등록·수명) + 핫리로드 실구동(FileWatcher/AssetDatabase 서비스 등록) — [mmorpg/05](mmorpg/05-pixel-assets-ai.md)
- **P0** `apps/game`(데이터드리븐 스탠드얼론 exe): `CreateApplication`을 프로젝트 로더로 구현, RuntimeModule 배선 재사용 — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** 데이터드리븐 씬 로딩: SceneSerializer를 editor→runtime 승격, SceneTransition `SceneLoaderFn`을 실로더에 배선 — [mmorpg/01](mmorpg/01-client-rendering.md)
- **P0** 타일 아틀라스 UV 배선(`BuildTileChunkQuads` placeholder 제거) + 뷰 컬링 — [mmorpg/01](mmorpg/01-client-rendering.md)
- **P1** 2D 라이팅(포인트·앰비언트·데이나이트, RGBA16F 라이트버퍼) + 파티클 렌더(Transparent 인스턴싱) + 강화 카메라(데드존·경계·셰이크·줌) — [mmorpg/01](mmorpg/01-client-rendering.md)
- **P1** ID 버퍼 픽킹·GPU 타임스탬프 DX11 스텁 채움(리드백) — [mmorpg/01](mmorpg/01-client-rendering.md)
- **P1** UI TextInput/ScrollView/ListView 위젯 신설(채팅·인벤 UI 전제) — [mmorpg/04](mmorpg/04-gameplay-systems.md)

**의존**: M6(수직 슬라이스). **관련**: [mmorpg/01](mmorpg/01-client-rendering.md)·[mmorpg/05](mmorpg/05-pixel-assets-ai.md)·[mmorpg/09](mmorpg/09-liveops-security.md)
**게이트**: 빈 프로젝트 폴더 → `game.exe --project X`로 마을 씬이 부팅되고, 낮/밤 조명·타격 파티클이 보이며, 인벤토리 목록을 스크롤한다.

---

### M8 — 게임플레이 코어 (싱글): 스탯·전투·인벤·퀘가 데이터로 돈다
**목표**: 서버 없이 로컬에서 스탯·스킬·전투·인벤·루트·몹 AI·퀘스트가 데이터드리븐+Lua로 동작. (온라인화 전에 규칙을 확정)

**핵심 항목**
- **P0** 타일-바디 충돌(`ITileCollision` 구현)·snapToGround — 이동 판정의 전제 — [../03-scene-world](03-scene-world.md)·[mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P0** `engine/gameplay` 신설: StatSystem·ResourceComponent·SkillSystem·CombatSystem·BuffSystem·DeathSystem — [mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P0** InventorySystem·Equipment·LootSystem(결정론 RNG·원자적 이동) — [mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P0** AiSystem(NpcSystem 일반화·A* 경로추종)·SpawnSystem — [mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P0** 리플렉션 기반 범용 컴포넌트 Lua 접근(LuaEntity 확장) + GameplayBindings — [../05-scripting-plugins](05-scripting-plugins.md)·[mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P1** QuestSystem·CraftingSystem·EconomySystem(NPC상점)·상호작용(채집·포탈) — [mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P1** `engine/content` 착수: ContentDB·ContentValidator·AssetRef·RefGraph(참조무결성) — [mmorpg/08](mmorpg/08-content-tooling.md)
- **P2** 컴포넌트 변경추적(ChangeTracker) — 이후 복제·영속 델타의 전제 — [../03-scene-world](03-scene-world.md)

**의존**: M7. **관련**: [mmorpg/04](mmorpg/04-gameplay-systems.md)·[mmorpg/08](mmorpg/08-content-tooling.md)
**게이트**: 로컬에서 몹을 스킬로 잡아 루트를 얻고 인벤에 넣고, 퀘스트 목표가 진행되며, 데이터 파일 수정이 핫리로드된다.

---

### M9 — 온라인 기반: 두 클라가 같은 존을 본다 (최대 리스크)
**목표**: 헤드리스 `mye_scene` 공유 서버 + 클라 예측/재조정/보간으로 2인 이상이 같은 존에서 서로를 부드럽게 본다. **크로스컴파일 결정론을 실증**한다.

**핵심 항목**
- **P0** OS 추상화 seam(core win32 하드코딩 제거) → Linux 헤드리스 빌드 — 선행 블로커 — [../01-core-platform](01-core-platform.md)·[mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** **크로스컴파일 결정론 검증**(Linux/GCC 서버 = Windows/MSVC 클라 비트/tolerance) — 게이트 조건 — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P0** `engine/net` 신설: NetId·BitStream·Protocol·`ITransport`(UDP)·Channel·Connection·Snapshot·DeltaCodec·NetClock — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P0** `server/world_server`(헤드리스 Application·ZoneManager)·CMake 헤드리스 링크 그래프 — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P0** 클라 예측(CSP)·서버 재조정(replay)·원격 엔티티 스냅샷 보간(SnapshotInterpolator) — [mmorpg/02](mmorpg/02-netcode-server.md)·[mmorpg/01](mmorpg/01-client-rendering.md)
- **P0** 연결 상태기계·NetIdComponent·서버측 이동 검증(속도/좌표 sanity) — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P0** **[신설 문서]** 서버 토폴로지(게이트웨이/로그인/월드 분리·존 오케스트레이션·페일오버) — 소유권 공백 해소 — [mmorpg/00](mmorpg/00-overview.md) §7.2
- **P0** **[신설 문서]** 복제 스키마·권한 규칙(복제 대상·server-only vs client-visible) — [mmorpg/00](mmorpg/00-overview.md) §7.2
- **P1** AoI(SpatialHash 재사용)·ReplicationSystem(구독 diff)·전송 암호화(DTLS류) — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P1** RTT/clock offset 동기·재접속 grace·이중로그인·전투 로그아웃 방어 — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P2** 지연보상 rewind(창 상한·RTT 검증)·크로스존 핸드오프(in-transit 상태기계) — [mmorpg/02](mmorpg/02-netcode-server.md)

**의존**: M8(게임플레이 규칙). **관련**: [mmorpg/02](mmorpg/02-netcode-server.md)·[mmorpg/01](mmorpg/01-client-rendering.md)
**게이트**: 두 클라가 접속해 서로 걷는 모습이 지터 없이 보이고, 클라 예측 이동이 서버 재조정으로 부드럽게 수렴하며, 서버가 순간이동/스피드핵 입력을 거부한다. **결정론 실측 리포트 제출.**

---

### M10 — 영속화 · 계정: 로그인하고 진행이 저장된다
**목표**: 계정·캐릭터·아이템·경제가 Postgres 원장에 영속되고, 아이템 복사/유실이 원천 차단된다.

**핵심 항목**
- **P0** `server/gateway`+`server/account`+`server/login`: 가입·로그인·세션토큰·중복로그인 단일화·재접속 — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P0** `server/persist`: Snowflake·PgConnection·Transaction·Idempotency·JsonbBridge(reflect↔JSONB)·Migrator — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P0** 아이템 원장·`ItemOps.MoveItem`(SELECT FOR UPDATE+row_version+멱등키+감사) — dupe 방지 — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P0** 캐릭터 로드/저장(로그인→헤드리스 ECS 스폰, write-behind 스냅샷)·재화 이중부기 원장 — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P0** `server/cache`(Redis 캐시·분산락 캐릭터 단일소유·만료큐) — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P0** **RAM 권위↔DB 원장 동기 규약**(경계 dupe 방어) — 03+02 봉합 — [mmorpg/00](mmorpg/00-overview.md) §8
- **P1** 우편·플레이어 거래(escrow 원자 커밋)·인벤/은행 컨테이너 서버검증 — [mmorpg/03](mmorpg/03-persistence-accounts.md)·[mmorpg/04](mmorpg/04-gameplay-systems.md)
- **P1** 스키마 마이그레이션 체인·write-behind 저널·백업/PITR·감사로그 — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P1** OAuth·2FA·계정 차단/rate-limit·GDPR 삭제/추출 — [mmorpg/03](mmorpg/03-persistence-accounts.md)

**의존**: M9(서버 스택). **관련**: [mmorpg/03](mmorpg/03-persistence-accounts.md)·[mmorpg/04](mmorpg/04-gameplay-systems.md)
**게이트**: 계정 가입→로그인→캐릭터 생성→아이템 획득/거래→로그아웃→재로그인 시 진행 보존. 동일 아이템 동시 이동 시도가 하나만 성공(dupe 0).

---

### M11 — 라이브옵스 · 배포: 서비스로 켜진다
**목표**: 클라 exe를 쿡·패치·배포하고, 텔레메트리·크래시·CVar·안티치트로 관찰·운영한다.

**핵심 항목**
- **P0** `.mpak` v2(zstd 압축·CRC32·Ed25519 서명) + `tools/cook`(결정적 쿡)·`tools/export` — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** `engine/telemetry`(EngineContext 서비스·오프라인 링버퍼·배치 업로드) + 크래시 리포팅(미니덤프+세션/빌드 메타) — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** 서버 푸시 CVar/피처플래그·점검모드(ConfigChangedEvent 배선·SetEventBus) + 원격 로그 싱크 — [mmorpg/09](mmorpg/09-liveops-security.md)·[../01-core-platform](01-core-platform.md)
- **P0** `server/anticheat`(스피드/텔레포트 탐지·레이트리밋, 서버 권위 이동검증 후킹) — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** 확률형아이템 확률공개(한국 게임법: 확률표 데이터화·서버 롤 감사·공개 API·CI 검증) — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P1** `apps/launcher`(버전게이트·무결성·델타패치)·CDN·**런타임 인게임 에셋 스트리밍**(소유 미정 해소) — [mmorpg/00](mmorpg/00-overview.md) K10
- **P1** `server/deploy`(컨테이너·블루그린·카나리·드레인)·백업/DR·우아한 종료 — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P1** GM 도구 깊이(RBAC 권한티어·2인 승인·감사·CS 티켓)·밴/제재 실행 — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P2** AB테스트·SLO·알림·대시보드(`server/ops`·`tools/ops`)·경제 이상탐지/RMT — [mmorpg/09](mmorpg/09-liveops-security.md)

**의존**: M10(영속화). **관련**: [mmorpg/09](mmorpg/09-liveops-security.md)
**게이트**: 클라를 쿡→서명 pak→배포→런처로 패치, 서버 CVar로 드랍률을 무중단 조정, 스피드핵 봇이 탐지·제재되고, 크래시가 대시보드에 집계된다.

---

### M12 — 소셜 · 리텐션 · 컴플라이언스: 계속 접속할 이유
**목표**: 제품 레이어(온보딩·소셜·리텐션 루프)와 커뮤니티·상용화·법규를 채운다. (적대적 검수의 최대 공백)

**핵심 항목**
- **P0** **[신설 문서]** 온보딩/FTUE(튜토리얼 흐름·진행 영속·재개·신규유저 보호·초반 페이싱) — [mmorpg/00](mmorpg/00-overview.md) §7.2
- **P0** **[신설 문서]** 소셜 그래프(친구·차단·프레즌스 팬아웃·귓속말 라우팅·추천친구·소셜 스팸) — [mmorpg/00](mmorpg/00-overview.md) §7.2
- **P0** `server/chat`(지역/귓속말/파티/길드 라우팅·욕설/이름 필터·NFKC·homoglyph·신고큐) — [mmorpg/04](mmorpg/04-gameplay-systems.md)·[mmorpg/09](mmorpg/09-liveops-security.md)
- **P0** 로그인 대기열 UX(순번·예상시간·우선순위·재진입 보존) — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P1** 리텐션 루프(일일/주간 리셋·출석·시즌·복귀유저 캐치업·배틀패스 상태머신·미수령 우편 폴백) — 신설
- **P1** `server/pay`(영수증 서버검증·멱등 지급·캐시샵)·연령등급·과몰입 보호·자기제외·지역별 규제(현행 게임시간 선택제 반영) — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P1** 파티·길드·길드은행·경매장(크로스샤드 saga 일관성·핫로우 경합) — [mmorpg/04](mmorpg/04-gameplay-systems.md)·[mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P2** 모더레이션 깊이(UGC 이미지·미성년 보호·에스컬레이션 SLA)·PvP/인스턴스 이탈 공정성 — [mmorpg/09](mmorpg/09-liveops-security.md)
- **P2** 매치메이킹 실체(MMR·역할구성·백필·신규격리)·인스턴스 좌석 예약·크래시 체크포인트 — [mmorpg/04](mmorpg/04-gameplay-systems.md)

**의존**: M11(운영 인프라). **관련**: [mmorpg/04](mmorpg/04-gameplay-systems.md)·[mmorpg/09](mmorpg/09-liveops-security.md)
**게이트**: 신규 유저가 튜토리얼로 첫 60분을 안내받고, 친구를 맺고 채팅하며, 출석/시즌 보상으로 재방문하고, 결제가 확률공개·연령 게이트를 통과한다.

---

### M13 — 스케일 · 최적화 · 확장 백엔드
**목표**: 목표 스케일(존당 수천·광장 500명)을 정량 검증하고 병목을 제거한다.

**핵심 항목**
- **P0** 부하테스트(`tools/loadtest` 헤드리스 봇 N세션) + 복제 예산 실측(관찰자당 KB/s·존당 Mbps·틱 예산) — [mmorpg/02](mmorpg/02-netcode-server.md)
- **P1** 스프라이트 인스턴싱 배칭(아틀라스 통합+DrawIndexedInstanced)·엔티티 LOD·월드 오버레이 대량 배칭 — [mmorpg/01](mmorpg/01-client-rendering.md)
- **P1** 우선순위 예산 스냅샷(PriorityAccumulator)·병렬 인코딩·시스템 병렬 디스패치 — [mmorpg/02](mmorpg/02-netcode-server.md)·[../03-scene-world](03-scene-world.md)
- **P1** 샤딩·파티셔닝·리전·캐릭터 이전·서버 통합(merge)·크로스샤드 saga 완성 — [mmorpg/03](mmorpg/03-persistence-accounts.md)
- **P2** 채널·인스턴스 던전·크로스존 핸드오프 스케일·QualityScaler(동적 품질) — [mmorpg/02](mmorpg/02-netcode-server.md)·[mmorpg/01](mmorpg/01-client-rendering.md)
- **P3** WebSocket/KCP/ENet 백엔드·DX12 백엔드·포스트프로세싱(블룸·그레이딩·CRT) — [mmorpg/02](mmorpg/02-netcode-server.md)·[mmorpg/01](mmorpg/01-client-rendering.md)

**의존**: M12. **관련**: [mmorpg/02](mmorpg/02-netcode-server.md)·[mmorpg/01](mmorpg/01-client-rendering.md)
**게이트**: 봇 500 접속 광장에서 서버 tick<16.6ms·클라 60fps 유지, 대역 예산이 목표치 내, 밀집 전투 정합 유지.

---

### 병행 트랙 T — AI 생성 & 콘텐츠 도구 (M7부터 상시)
**목표**: 1인/소수 팀이 대량 콘텐츠를 제작하도록 AI 에셋·오디오 생성과 데이터 저작 도구를 상시 확장.

**핵심 항목**
- **P0** [K8 정본 결정] 단일 `IGenProvider` 계층 통합 + "모든 생성은 게이트웨이 경유" 강제 — [mmorpg/07](mmorpg/07-ai-orchestration.md)
- **P0** `server/ai-gateway`(KeyVault·RateLimiter·CostBudget·SafetyFilter·ResponseCache) — 이미지/오디오에도 강제 — [mmorpg/07](mmorpg/07-ai-orchestration.md)
- **P1** 텍스트→스프라이트/타일셋 생성·배경제거·팔레트락·8방향/페이퍼돌 조립·검수 게이트(GenReviewPanel) — [mmorpg/05](mmorpg/05-pixel-assets-ai.md)
- **P1** 텍스트→SFX/음악 생성·다이나믹 뮤직·리버브존·미리듣기/채택(SoundGenPanel) — [mmorpg/06](mmorpg/06-ai-audio.md)
- **P1** `engine/content` 완성(Migrator·Cook·NodeGraph)·콘텐츠 그래프 에디터(퀘/대화/BT)·서버/클라 이원 쿡 — [mmorpg/08](mmorpg/08-content-tooling.md)
- **P1** 멀티 AI 제공자 어댑터(Anthropic/OpenAI/Gemini/Ollama)·데이터드리븐 라우팅·폴백·프롬프트 라이브러리 — [mmorpg/07](mmorpg/07-ai-orchestration.md)
- **P2** 배치 생성(도감·세트)·라이선스/프로버넌스 감사(DB 승격)·스타일 일관성 강제 루프·아틀라스 append-only 배포 — [mmorpg/05](mmorpg/05-pixel-assets-ai.md)·[mmorpg/07](mmorpg/07-ai-orchestration.md)
- **P2** MCP 툴군(gen_*·sound_*·content_*·ai_generate_*)·에디터 AI 패널·코드 어시스트 — [mmorpg/07](mmorpg/07-ai-orchestration.md)·[mmorpg/08](mmorpg/08-content-tooling.md)

**의존**: M7의 안정 GUID·AssetModule·핫리로드(공유 선행). **관련**: [mmorpg/05](mmorpg/05-pixel-assets-ai.md)·[mmorpg/06](mmorpg/06-ai-audio.md)·[mmorpg/07](mmorpg/07-ai-orchestration.md)·[mmorpg/08](mmorpg/08-content-tooling.md)

---

### 마일스톤 의존성 그래프

```
M6(수직슬라이스, 기존) ──▶ M7(싱글 런타임) ──▶ M8(게임플레이 코어)
                                                      │
                                                      ▼
                                              M9(온라인 기반·결정론 실증)
                                                      │
                                                      ▼
                                              M10(영속화·계정)
                                                      │
                                                      ▼
                                              M11(라이브옵스·배포)
                                                      │
                                                      ▼
                                              M12(소셜·리텐션·컴플라이언스)
                                                      │
                                                      ▼
                                              M13(스케일·최적화)

    T(AI 생성·콘텐츠 도구) ═══ M7부터 상시 병행 ═══▶
```

---

## Part D. 즉시 착수 권장 3가지 (임팩트 최상)

지금 당장 시작하면 이후 모든 마일스톤을 가속하고, 미루면 전부가 막히는 세 가지.

### D-1. 안정 GUID + AssetModule + 핫리로드 배선 (M7 P0, 병행 트랙 T의 공유 선행)
**왜 지금**: `.meta`/GUID 파서·FileWatcher·AssetDatabase가 **이미 구현·테스트됐으나 배선만 안 됨**(매 로드 `AssetGuid::Generate()`).
이것 없이는 씬·프리팹·콘텐츠 DB·AI 생성이 에셋을 안정 참조할 수 없어 콘텐츠 파이프라인 전제가 붕괴한다.
**착수 크기**: 작음(기존 코드 배선 중심). **해제하는 것**: 씬 로딩·콘텐츠 도구·AI 생성 전부.
→ [mmorpg/05](mmorpg/05-pixel-assets-ai.md)

### D-2. 데이터드리븐 게임 런타임 앱 `apps/game` + 씬 로딩 승격 (M7 P0)
**왜 지금**: `CreateApplication`이 editor/paktool만 있고 village_demo가 1639줄 하드코딩이라, 넷 클라·게임플레이·라이브옵스가
**배선될 부트스트랩 자체가 없다.** SceneSerializer는 editor 모듈에 갇혀 runtime이 못 쓴다.
게임 exe + 데이터 씬 로딩은 M9~M12의 모든 시스템이 얹힐 골격이다.
**착수 크기**: 중간. **해제하는 것**: 온라인·라이브옵스로 가는 모든 경로.
→ [mmorpg/09](mmorpg/09-liveops-security.md)·[mmorpg/01](mmorpg/01-client-rendering.md)

### D-3. 크로스컴파일 결정론 스파이크 + OS 추상화 seam 착수 (M9 P0의 선행 실증)
**왜 지금**: 넷코드 전체가 "서버(Linux) = 클라(Windows) 동일 시뮬로 재조정 공짜"라는 **미검증 전제** 위에 서 있다.
이게 깨지면 M9~M13이 근본부터 흔들린다. 지금 작은 스파이크(move-and-slide·물리·A*를 양 플랫폼에서 돌려 tolerance 측정)로
**float+tolerance로 충분한지 vs 고정소수 승격이 필요한지**를 조기에 판정하고, core의 win32 하드코딩 제거(seam)를 병행 착수하면
M9 진입 시 리스크가 이미 닫혀 있다.
**착수 크기**: 작음(스파이크) + 중간(seam). **해제하는 것**: 넷코드 전체의 아키텍처 확신.
→ [mmorpg/02](mmorpg/02-netcode-server.md)·[../01-core-platform](01-core-platform.md)

---

## 이 도메인 요약 3줄

- 현재 MyEngine은 **단일 머신 싱글플레이 콘텐츠 툴킷으로는 성숙**(core·scene·render·asset·audio·script·editor 다수 Strong)하나, **넷코드·서버·영속화·게임플레이 프레임워크·게임 런타임 앱·AI 생성은 전무 또는 스텁**이다.
- 로드맵은 **M7(싱글 런타임 완성)→M8(게임플레이 코어)→M9(온라인·결정론 실증)→M10(영속화)→M11(라이브옵스)→M12(소셜·리텐션·컴플라이언스)→M13(스케일)** + **AI/콘텐츠 병행 트랙 T**로, 적대적 검수가 드러낸 소유권 공백·경계 dupe·스케일 정량·제품 레이어 부재를 각 단계에 흡수한다.
- 즉시 착수 3선은 **① 안정 GUID·AssetModule·핫리로드 배선(기존 코드 배선) ② apps/game+씬 로딩 승격(골격) ③ 크로스컴파일 결정론 스파이크+OS seam(리스크 조기 종결)** — 모두 이후 전 마일스톤을 가속하는 선행 잠금 해제다.

*문서 버전: 2026-07. 5개 서브시스템 인벤토리 + 9개 도메인 요약 + 3개 적대적 검수 렌즈 종합.*
