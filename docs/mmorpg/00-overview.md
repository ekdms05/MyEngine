# MMORPG 00 — 픽셀 2.5D MMORPG 개요 (Overview)

> 테일즈위버풍 **2D 도트 + 3D 하이브리드** 온라인 RPG를 MyEngine 위에 올리기 위한 최상위 설계 입구.
> 이 문서는 [01](01-client-rendering.md)~[09](09-liveops-security.md) 도메인 문서 전체의 색인이자 요약이며,
> 상위 엔진 설계([../00-overview](../00-overview.md), [../01](../01-core-platform.md)~[../08](../08-mcp.md))의 **연장선**이다.
> 상세 규약이 충돌해 보이면 **각 주제의 소유 문서가 정본**이다(§7 소유권 지도 참조).
> 관련: [../00-overview](../00-overview.md) · [../01-core-platform](../01-core-platform.md) · [../03-scene-world](../03-scene-world.md) · [10-status-and-roadmap](../10-status-and-roadmap.md)

---

## 1. 목표 · 범위 — 이 문서가 결정하는 것

이 문서는 **"싱글플레이용 확장형 픽셀 엔진(MyEngine)"을 "라이브 서비스 가능한 픽셀 MMORPG 플랫폼"으로 승격**하는
전체 그림을 규정한다. 개별 시스템의 상세는 01~09가 소유하고, 여기서는 (1) 비전·타깃, (2) 전체 아키텍처,
(3) MyEngine 자산 재사용 매핑, (4) 도메인 문서 색인, (5) 반드시 확정할 핵심 기술선택,
(6) 소유권 지도(문서 간 계약)만 다룬다.

이 문서가 다루지 **않는** 것: 각 시스템의 스키마·엣지케이스·마일스톤(→ 01~09), 구현 현황·로드맵(→ [10-status-and-roadmap](../10-status-and-roadmap.md)).

---

## 2. 비전 · 핵심 재미 · 타깃

### 2.1 비전 한 줄

> **테일즈위버풍 2.5D 픽셀 세계에서, 도트 캐릭터가 경사·다리·3D 프랍이 어우러진 하이브리드 맵을
> 수백 명과 함께 실시간으로 돌아다니며 성장·전투·거래·소셜을 하는 라이브 온라인 RPG.**

### 2.2 핵심 재미 (Core Pillars)

| 기둥 | 내용 | 소유 도메인 |
|---|---|---|
| **감성 픽셀 2.5D** | 단일 뎁스버퍼 하이브리드 정렬 + 픽셀퍼펙트 내부 RT. "다리 위/아래·큰 3D 뒤로 돌아 들어가기"가 온라인에서도 정합 | [01](01-client-rendering.md) |
| **함께 있는 세계** | 원격 플레이어·몹이 부드럽게 보간되어 같은 화면에 존재. 밀집 광장(500명)에서도 60fps | [01](01-client-rendering.md)·[02](02-netcode-server.md) |
| **성장·전투 루프** | 스탯·스킬·전투·루트·퀘스트의 데이터드리븐 진행. 서버 권위로 공정 | [04](04-gameplay-systems.md) |
| **살아있는 경제·소셜** | 아이템·거래·경매·우편·파티·길드·채팅. 복사·유실 없는 원장 | [03](03-persistence-accounts.md)·[04](04-gameplay-systems.md) |
| **끝없는 콘텐츠** | AI 에셋·오디오 생성 + 데이터 도구로 1인/소수 팀이 대량 콘텐츠 제작 | [05](05-pixel-assets-ai.md)·[06](06-ai-audio.md)·[07](07-ai-orchestration.md)·[08](08-content-tooling.md) |
| **지속 운영** | 스탠드얼론 exe·쿡·패치·텔레메트리·안티치트·컴플라이언스 | [09](09-liveops-security.md) |

### 2.3 타깃 스케일 (설계 전제)

| 축 | 목표치 | 근거·비고 |
|---|---|---|
| 존(zone)당 동접 | 수백~수천, 밀집 광장 500명 | [02](02-netcode-server.md) 복제 예산이 실제 상한 결정(§8 리스크) |
| 왕복 지연(RTT) | 30~250ms, 손실 0~5% | 클라 예측 + 서버 재조정 + 원격 보간으로 흡수 |
| 시뮬레이션 틱 | 서버 헤드리스 고정 60Hz(설정), 스냅샷 10~30Hz | 클라 렌더 60~300Hz 보간 |
| 클라 렌더 | 내부 960×540 → 정수배 업스케일, 1080p/4K | 저사양~고주사율 스펙트럼(QualityScaler) |
| 운영 | 24/7 라이브, 다지역, 다국어(ko/en/ja/zh) | 배포·컴플라이언스·모더레이션 |

### 2.4 안티-비전 (하지 않는 것 — 1인/소수 개발 현실주의)

- **커널 안티치트 미채택** — 서버 권위 시뮬 + 행동통계로 방어. 봇/멀티박싱은 완전 근절이 아니라 **비용 상승**이 목표선([09](09-liveops-security.md) §안티치트).
- **롤백 넷코드(격투게임식) 비주력** — MMO 스케일에 부적합. PvP 소인원 인스턴스 한정 선택지(P4).
- **풀 3D 전환 없음** — 어디까지나 2.5D 하이브리드. 3D는 삽입 프랍·지형 볼륨 용도.
- **자체 LLM/이미지 모델 학습 없음** — 외부 제공자 오케스트레이션([07](07-ai-orchestration.md))으로 흡수.

---

## 3. 전체 아키텍처 (텍스트 다이어그램)

핵심 원리: **클라이언트와 서버는 `engine/scene`(ECS·물리·타일맵·A*)을 헤드리스로 공유**한다.
서버는 `render`·`rhi`·`audio`·`ui`·`imgui`를 링크하지 않는다 → "서버 물리 = 클라 물리" 결정론이 공짜로 성립한다([02](02-netcode-server.md) §1.3).

```
                             ┌───────────────────────────────────────────────┐
                             │                 플레이어 단말                    │
                             │  ┌─────────────────────────────────────────┐  │
   ┌──────────┐             │  │  apps/game  (스탠드얼론 클라 exe, 09/신규)  │  │
   │ launcher │──패치·검증──▶│  │  ─────────────────────────────────────  │  │
   │ (09/신규) │             │  │  L0 core · L1 rhi(DX11) · L2 render/asset │  │
   └──────────┘             │  │  L3 scene(ECS·물리·타일·A*) · L4 script/ui │  │
        │ CDN               │  │  net client: 예측·재조정·보간·스냅샷      │  │
        ▼                    │  │  gameplay(스탯·전투·인벤·UI) · audio      │  │
   ┌──────────┐             │  └─────────────────────────────────────────┘  │
   │  객체     │             └───────────────┬───────────────────────────────┘
   │ 스토리지  │                             │ 신뢰성 UDP(1차) / WebSocket(웹)
   │ (에셋/백업)│                             │  암호화 채널(DTLS류, 09 보안)
   └──────────┘                             ▼
┌───────────────────────────────────────────────────────────────────────────────┐
│                              서버 사이드 (server/)                               │
│                                                                                 │
│   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐    │
│   │  gateway     │   │ login/account│   │   chat       │   │  ai-gateway  │    │
│   │ (버전게이트·  │   │ (인증·세션·  │   │ (필터·신고·  │   │ (키·비용·    │    │
│   │  라우팅·큐·  │   │  OAuth·2FA·  │   │  라우팅·     │   │  레이트·캐시·│    │
│   │  레이트리밋) │   │  중복로그인) │   │  팬아웃)     │   │  안전필터)   │    │
│   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘    │
│          │                  │                  │                  │            │
│          ▼                  ▼                  ▼                  ▼            │
│   ┌─────────────────────────────────────────────────────────┐  ┌──────────┐  │
│   │            world_server (헤드리스 mye_scene 공유)          │  │ 외부 AI  │  │
│   │  ZoneManager(다중 World) · AoiSystem · ReplicationSystem   │  │ 제공자   │  │
│   │  MovementValidation · LagCompensation · Handoff · Session │  │(Claude·  │  │
│   │  gameplay 권위 시뮬(전투·인벤·루트·스폰·경제)              │  │ GPT·SD·  │  │
│   │  server/persist · server/cache 브리지                     │  │ ...)     │  │
│   └───────────────────────┬─────────────────────────────────┘  └──────────┘  │
│                           │                                                    │
│   ┌───────────────────────┴─────────────────────────────────────────────┐    │
│   │                       데이터 · 캐시 계층                              │    │
│   │   Postgres(단일 권위 원장: 계정·캐릭·아이템·경제·월드상태·감사)        │    │
│   │   Redis(캐시·분산락·세션·만료큐·레이트리밋)                           │    │
│   │   객체 스토리지(에셋 pak·백업/PITR·UGC)                                │    │
│   └───────────────────────────────────────────────────────────────────┘    │
│                                                                                 │
│   ┌───────────────────────────────────────────────────────────────────────┐  │
│   │  라이브옵스: telemetry · crash · CVar/피처플래그 · anticheat · sanction │  │
│   │  · pay(결제·확률공개) · ops(대시보드·GM콘솔·AB·SLO) · deploy(블루그린)  │  │
│   └───────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│  개발·콘텐츠 사이드 (게임 프로세스 아님 — 제작 파이프라인)                        │
│   apps/editor(MyEditor) · tools/mcp · tools/cook · tools/gen* · apps/gentool   │
│   engine/content(ContentDB·Validator·Cook) · AI 에셋/오디오 생성 서비스        │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.1 프로세스 경계 원칙

- **게이트웨이 = 유일 진입점**: 버전 게이트·레이트리밋·토큰 검증·존 라우팅·로그인 큐. L7 자원고갈 방어의 1선.
- **로그인/계정 분리**: 인증·세션 유일성(중복 로그인 킥)·OAuth·2FA는 world_server와 별도 프로세스.
- **world_server = 존 오케스트레이션**: `ZoneManager`가 여러 `World` 인스턴스(필드 존·던전 인스턴스·채널)를 호스팅, 크로스존 핸드오프는 2단계 원자 이전.
- **채팅·AI 게이트웨이 분리**: 채팅 팬아웃과 AI 호출은 복제 대역·틱 예산과 경쟁하지 않도록 별도 서비스.
- **데이터는 Postgres 한 곳이 진실**: RAM(존 서버 ECS)은 캐시/권위-시뮬 상태, 원장은 DB. 두 권위의 동기 규약이 dupe 방어의 핵심([03](03-persistence-accounts.md), §8 리스크).

> **주의(설계 공백)**: 위 다이어그램의 **게이트웨이/로그인/채팅/존 오케스트레이션 프로세스 분리·리더 선출·페일오버**를
> 정본으로 소유하는 "서버 토폴로지" 문서가 아직 없다. 여러 도메인 문서가 이를 존재하지 않는 슬러그(`01-server-topology` 등)로
> 위임한다 → §7·§8에서 **소유권 공백**으로 명시하고, 로드맵([10](../10-status-and-roadmap.md) M9)에서 신설을 권한다.

---

## 4. MyEngine 자산 재사용 매핑

픽셀 MMORPG는 **기존 MyEngine 위에 얹는다**(재구현 금지). 어디를 어떻게 재사용하는지:

| MyEngine 자산 | 재사용처 | 방식 | 소유 도메인 |
|---|---|---|---|
| `engine/scene`(ECS·SystemScheduler·PhysicsWorld2D·Tilemap·NavSystem) | **클라 + 서버 공유** | 서버는 헤드리스 링크(render/rhi/audio/ui 미링크) → 결정론 공짜 | [02](02-netcode-server.md) |
| `engine/core`(메인 루프·TimeSystem·`--headless`·JobSystem·EventBus·Config·Log) | 서버 루프·클라 루프 | `App`/`GuardedMain` 재사용, `Application` 구현으로 world_server·apps/game | [02](02-netcode-server.md)·[09](09-liveops-security.md) |
| `engine/render`(HybridRenderer·DepthEncoder·Camera2D·PixelPerfectTarget·SpriteBatch) | 클라 렌더 | 재사용 + 인스턴싱·컬링·라이팅·파티클·오버레이 확장 | [01](01-client-rendering.md) |
| `engine/rhi`(DX11 핸들풀·PSO·스왑체인) | 클라 GPU | 인스턴싱·StructuredBuffer·리드백(픽킹)·타임스탬프 스텁 채움 | [01](01-client-rendering.md) |
| `engine/asset`(VFS·임포터·.pak·.meta/GUID·핫리로드) | 에셋 로드·쿡·패치 | AssetModule 신설·안정 GUID 배선·.mpak v2(zstd·서명) | [05](05-pixel-assets-ai.md)·[09](09-liveops-security.md) |
| `engine/audio`(믹서·버스·큐·크로스페이드·공간화) | 클라 오디오 | 소비(재구현 금지) + 다이나믹 뮤직·리버브존·AI 생성 배선 | [06](06-ai-audio.md) |
| `engine/reflect`(Reflect<T>·JsonArchive·PropertyPath) | 직렬화 전반 | 씬·프리팹·콘텐츠 레코드·DB JSONB 브리지·세이브 마이그레이션 | [03](03-persistence-accounts.md)·[08](08-content-tooling.md) |
| `engine/script`(Lua 5.4·ScriptComponent·핫리로드·코루틴·바인딩) | 게임 로직 | 게임플레이 바인딩(스탯/스킬/인벤)·LuaEntity refl 범용 접근 확장 | [04](04-gameplay-systems.md) |
| `engine/ui`(위젯·FreeType·i18n) | 인게임 UI | TextInput/ScrollView/ListView 신설(채팅·인벤·거래) | [04](04-gameplay-systems.md)·[09](09-liveops-security.md) |
| `engine/runtime`(대화·컷신·NPC·세이브·로컬라이즈·씬전환) | 게임 런타임 | SaveSystem 참여자 패턴 → server IPersistParticipant, SceneLoaderFn 배선 | [03](03-persistence-accounts.md)·[04](04-gameplay-systems.md) |
| `engine/editor`(인스펙터·Undo·플레이모드·씬직렬화·타일/애니/도트 에디터·확장 레지스트리) | 콘텐츠 저작 | SceneSerializer를 runtime로 승격, 확장 API로 콘텐츠 그래프 패널 | [08](08-content-tooling.md) |
| `tools/mcp`(build/test/run/capture + dot 툴) | AI 개발·생성 | gen_*·sound_*·content_*·ai_generate_* 툴군 확장 | [05](05-pixel-assets-ai.md)·[06](06-ai-audio.md)·[07](07-ai-orchestration.md)·[08](08-content-tooling.md) |
| `apps/paktool`·`.pak` v1 | 배포 | .mpak v2로 확장(압축·CRC·서명·델타) | [09](09-liveops-security.md) |
| `phys::SpatialHash`(멀티셀 삽입) | AoI·근접 쿼리 | 물리 broadphase를 서버 AoI 셀로 재사용 | [02](02-netcode-server.md) |
| JobSystem IO 큐 · EventBus `Enqueue`(스레드세이프) | 넷코드 프리미티브 | 소켓 IO·복제 인코딩 병렬화·스레드경계 이벤트 | [02](02-netcode-server.md) |

**신규로 만들어야 하는 것(엔진에 0줄)**: `engine/net`(전송·직렬화·복제), `engine/gameplay`(스탯~채팅),
`engine/content`(레코드 DB·검증·쿡), `engine/ai`·`engine/audiogen`(생성 추상), `engine/telemetry`,
`server/*`(world/gateway/persist/cache/account/chat/pay 등), `apps/game`·`apps/launcher`.
자세한 신규 모듈 목록은 각 도메인 문서 §6 + [10](../10-status-and-roadmap.md).

---

## 5. 도메인 문서 색인 (01~09)

> ⚠️ 개별 문서 상단의 상호참조 슬러그(`01-server-topology`·`03-replication-state`·`06-social-chat` 등)는
> **실제 파일명과 어긋난다**(병행 작성 산물). 아래가 **실파일 기준 정본 색인**이다. §7에서 재정합을 규정한다.

| # | 파일 | 한 줄 |
|---|---|---|
| 01 | [01-client-rendering.md](01-client-rendering.md) | 클라이언트 아키텍처 & 픽셀 2.5D 렌더링 — 하이브리드 뎁스·픽셀퍼펙트 재사용 위에 인스턴싱·컬링·LOD·원격 스냅샷 보간·월드 오버레이·스트리밍·2D 라이팅·파티클·포스트·강화 카메라 |
| 02 | [02-netcode-server.md](02-netcode-server.md) | 넷코드 & 권위 서버 — 헤드리스 `mye_scene` 공유로 결정론 확보, 순수 전송/직렬화 `engine/net` + `server/world_server`로 예측·재조정·AoI·복제·지연보상·재접속·치트방지 |
| 03 | [03-persistence-accounts.md](03-persistence-accounts.md) | 영속성·계정·월드 상태 — Postgres 원장 + Redis 캐시/락 + 객체 스토리지 3계층, 아이템 이동 단일 트랜잭션(복사/유실 차단), reflect JsonArchive↔JSONB 브리지 |
| 04 | [04-gameplay-systems.md](04-gameplay-systems.md) | 게임플레이(데이터드리븐+Lua) — 스탯·스킬·전투·인벤·루트·AI·퀘·경제·파티·길드·채팅·PvP·인스턴스를 신규 `engine/gameplay`로, ECS·스케줄러·월드버스·A*·리플렉션 재사용, 서버 권위 |
| 05 | [05-pixel-assets-ai.md](05-pixel-assets-ai.md) | 픽셀 에셋 파이프라인 & AI 생성 — 도트 생성→검수→후처리→임포트(.meta/GUID)→아틀라스→핫리로드→등록 전 과정, 멀티 provider·8방향/페이퍼돌/오토타일·배치·시드/저작권 추적 |
| 06 | [06-ai-audio.md](06-ai-audio.md) | AI 오디오 생성 & 사운드 디자인 — 기존 `mye_audio` 소비 위에 AudioCue/MusicSet/ReverbZone 데이터화 + 텍스트→음악/SFX 생성 + 다이나믹 뮤직·2.5D 공간 사운드 |
| 07 | [07-ai-orchestration.md](07-ai-orchestration.md) | 멀티 AI 제공자 오케스트레이션 — 제공자 무관 `IAiProvider` + 서버 `ai-gateway`(키·비용·안전)로 코드/콘텐츠 생성 통합, 데이터드리븐 라우팅·폴백·앙상블·스키마검증 품질게이트 |
| 08 | [08-content-tooling.md](08-content-tooling.md) | 콘텐츠 제작 도구 & 에디터 확장 — 데이터드리븐 저작(맵·스폰·퀘·대화·드랍·스킬·밸런스)을 신규 `engine/content`(ContentDB·Validator·RefGraph·Cook·NodeGraph)로 통일, 참조무결성·서버/클라 쿡·머지 |
| 09 | [09-liveops-security.md](09-liveops-security.md) | 라이브옵스·배포·확장·보안 — "만드는 도구"를 "돌아가는 서비스"로 승격: 스탠드얼론 exe·쿡/익스포트·런처·서버 배포·텔레메트리·안티치트·채팅/제재·결제/확률공개·DR |

**상위 엔진 문서(참조 대상)**: [../00-overview](../00-overview.md) · [../01-core-platform](../01-core-platform.md) ·
[../02-rendering](../02-rendering.md) · [../03-scene-world](../03-scene-world.md) · [../04-asset-pipeline](../04-asset-pipeline.md) ·
[../05-scripting-plugins](../05-scripting-plugins.md) · [../06-runtime-systems](../06-runtime-systems.md) ·
[../07-editor-ui](../07-editor-ui.md) · [../08-mcp](../08-mcp.md)

**현황·로드맵**: [10-status-and-roadmap](../10-status-and-roadmap.md)

---

## 6. 반드시 확정할 핵심 기술선택 (권장 포함)

각 도메인 문서를 착수하기 전에 **한 번** 정본으로 결정해야 하는 교차-도메인 선택. `(소유)`는 확정 후 반영할 문서.

| # | 결정 | 선택지 | **권장** | 근거 | 소유 |
|---|---|---|---|---|---|
| K1 | **전송 계층** | 신뢰성 UDP(자체 채널) / KCP / ENet / WebSocket / TCP | **자체 신뢰성 UDP 1차 + WebSocket(웹클라) 백엔드**. `ITransport` 뒤로 추상 | UDP=지연 최적·MMO 표준. `ITransport`로 백엔드 교체 여지(엔진 백엔드-인터페이스화 원칙) | 02 |
| K2 | **전송 암호화** | 평문 / 자체 암호화 / DTLS / QUIC | **DTLS류(UDP) + wss(WebSocket)**. 세션 토큰·거래·채팅 기밀성 필수 | 평문 UDP는 스니핑·connId 스푸핑·세션 하이재킹 표면(§8 리스크) | 02·09 |
| K3 | **데이터베이스** | Postgres / MySQL / MongoDB / 자체 | **Postgres(단일 권위 원장) + Redis(캐시/락)** | JSONB(reflect 브리지)·트랜잭션·낙관락·PITR. Redis 분산락으로 캐릭터 단일소유 | 03 |
| K4 | **서버 언어·런타임** | C++(엔진 공유) / Go / Rust / C# | **C++20(world_server = mye_scene 공유)**, 주변 서비스(gateway·chat·ai-gateway·ops)는 **TS/Node 허용** | 결정론 = 클라와 동일 시뮬 코드 공유가 최대 승리. 상태 없는 주변부는 생산성 우선 | 02·09 |
| K5 | **서버 OS·빌드** | win32 유지 / Linux 헤드리스 | **Linux 헤드리스(컨테이너)** — 단 core의 win32 하드코딩 제거(OS 추상화 seam)가 **선행 블로커** | 컨테이너 배포·확장. 크로스컴파일 부동소수 결정론 검증 필요(§8 최상위 리스크) | 09·(엔진 core 확장) |
| K6 | **결정론 수치 모델** | float(tolerance 재조정) / 고정소수(fixed-point) 시뮬 | **1차: float + tolerance 재조정**으로 시작, 만성 불일치 측정 시 이동/물리 핵심만 **고정소수 승격** | 완전 비트결정론은 크로스컴파일에서 비쌈. 재조정 스무딩으로 흡수 가능 여부를 조기 실측 | 02 |
| K7 | **인스턴싱 배칭** | 텍스처런 분할 드로우(현재) / DrawIndexedInstanced + 아틀라스 통합 | **아틀라스 통합 + 인스턴스 VB 슬롯(DX11 배선)**. 밴드+Y-sort 깊이 불변식 유지 | 수백~수천 스프라이트 스케일의 최대 병목(01 CR-M0). RHI 계약은 있으나 DX11 미배선 | 01 |
| K8 | **AI 제공자 추상화** | 3중화 유지(IGenProvider/IAudioGenProvider/IAiProvider) / **단일 계층 통합** | **단일 `IGenProvider` 계층 + "모든 생성은 게이트웨이 경유" 강제** | 이미지/오디오가 게이트웨이 밖으로 새면 키·비용·안전필터·폴백 정책이 무력화(§8 최상위 리스크) | 07(정본) |
| K9 | **네트워크 안정 ID** | ecs::Entity(process-local) / Snowflake / NetId | **Snowflake(DB 전역유일) + NetId(세션 복제) + NetIdComponent** | `Entity`(index:32\|gen:32)는 재부팅·존이동·샤드 간 불안정 | 02·03 |
| K10 | **에셋 스트리밍 소유** | (현재 순환 위임으로 무주지대) | **09를 정본으로 확장** — 버전드 pak·델타·CDN·런타임 인게임 스트리밍 | 콘텐츠 지속 추가 MMO의 핵심 배관이 실체 없이 붕 떠 있음(§8 리스크) | 09(신규 지정) |

---

## 7. 소유권 지도 (문서 간 계약) — ⚠️ 재정합 필요

개별 도메인 문서는 병행 작성되어 **서로 다른 번호체계로 존재하지 않는 문서를 참조**한다. 통합 시 아래로 정합한다.
(비평 렌즈가 지적한 "설계 무결성 리스크 #1" — 링크 그래프 붕괴로 위임 항목이 무주지대가 되는 문제)

### 7.1 확정된 소유 (실파일 기준)

| 주제 | 정본 소유 | 인용처 |
|---|---|---|
| 클라 렌더·픽셀 정렬·카메라·오버레이·스트리밍 | **01** | 04(전투 이펙트)·06(파티클 시뮬 경계) |
| 전송·직렬화·예측/재조정·AoI·복제 코덱·핸드오프·세션 | **02** | 04(판정 타이밍)·03(핸드오프 DB 커밋) |
| DB 원장·계정·아이템 원자성·경제·마이그레이션·캐시/락 | **03** | 04(인벤/거래 규칙)·09(감사·백업) |
| 게임플레이 규칙(스탯·전투·인벤·스킬·퀘·경제·채팅 로직) | **04** | 02(복제 대상)·03(영속 대상) |
| 픽셀 에셋 생성·임포트·아틀라스·8방향/페이퍼돌·저작권 | **05** | 08(콘텐츠 참조)·07(제공자) |
| 오디오 생성·다이나믹 뮤직·공간 사운드·라이선스 | **06** | 05(제공자 공유)·02(사운드 이벤트 복제) |
| AI 제공자 추상·게이트웨이·라우팅·품질게이트·안전 | **07** | 05·06·08(생성 경로 통합) |
| 콘텐츠 레코드 DB·검증·참조무결성·쿡·노드그래프 | **08** | 04(데이터 스키마)·05(에셋 참조) |
| 배포·exe·쿡·패치·텔레메트리·안티치트·제재·결제·컴플라이언스 | **09** | 전 도메인(운영 훅) |

### 7.2 소유권 공백 (아무도 소유하지 않음 — 신설 필요, [10](../10-status-and-roadmap.md) 로드맵 반영)

| 무주지대 주제 | 현 상태 | 처리 |
|---|---|---|
| **서버 프로세스 토폴로지** (게이트웨이/로그인/월드/채팅 분리·존 오케스트레이션·리더선출·페일오버·수평확장) | 02·03·09가 존재하지 않는 `01-server-topology`로 위임 | **신규 문서 지정**(가칭 10번대) 또는 02 확장. M9 착수 전 확정 |
| **복제 스키마·권한 규칙** (무엇을 복제, 누가 권위, server-only vs client-visible 필드) | 02가 존재하지 않는 `03-replication-state`로 위임 | 02+04 합작으로 필드 권한 매트릭스 신설. changeMask 비트 배정의 전제 |
| **소셜 그래프** (친구·차단·프레즌스·귓속말 UX·추천친구·소셜 스팸) | 여러 문서가 존재하지 않는 `06-social-chat`로 위임, 실제 07은 AI라 증발 | **신규 문서 지정** — 리텐션 핵심(친구가 있으면 안 떠남) |
| **온보딩/FTUE·리텐션 루프** (튜토리얼·출석·시즌·배틀패스·복귀유저·로그인 큐 UX) | 9개 문서에 등장 0건(grep) | **신규 문서 지정** — D1 이탈의 최대 요인, P0급 공백 |
| **크로스샤드 일관성** (길드·경매·파티·우편이 샤드를 넘을 때 saga 보상 트랜잭션) | 03 "단일 샤드 내 설계" vs 04 "존 초월 세션 서비스" 충돌 | 03+04 봉합. saga·핫로우 경합 프로토콜 명문화 |

### 7.3 재번호 규칙(권장)

- 각 도메인 문서 상단 상호참조 블록을 **실파일 슬러그로 일괄 교체**(예: `[04-netcode-server]`→`[02-netcode-server]`).
- 위임 문구는 §7.1 표를 정본으로 재작성. 위임 대상이 §7.2(무주지대)면 "**미소유 — 신설 예정**"으로 명시(암묵적 위임 금지).

---

## 8. 최상위 리스크 (착수 전 인지)

세 개 적대적 검수 렌즈가 공통 지적한 최상위 항목. 상세·완화는 각 도메인 문서 + [10](../10-status-and-roadmap.md).

1. **결정론 전제 미검증** — "서버(Linux/GCC) = 클라(Windows/MSVC) 동일 시뮬로 재조정 공짜"가 크로스컴파일 부동소수에서 비트 동일하다는 보장 없음. 깨지면 full resync 폭주. → K5·K6 조기 실측(M9 게이트).
2. **소유권 공백** — §7.2. 통합/재번호 없이는 dupe·정합 방어의 종단 계약이 성립하지 않음.
3. **경계 dupe** — DB 단일 트랜잭션은 견고하나, 존 서버 RAM 권위↔DB 원장 경계·크로스존 핸드오프·write-behind 창의 dupe가 "2단계 커밋" 단어 수준으로만 덮임. → 03+02 in-transit 상태기계 구체화.
4. **스케일 정량 미검증** — 존당 수천·광장 500명의 복제 O(관찰자×대상) 폭발, 대역 예산(KB/s·Mbps·NIC), 핫로우 락 직렬화 병목이 정량 근거 없이 반복. → 02 부하테스트로 예산 실측.
5. **AI 생성 게이트웨이 우회** — 이미지/오디오가 게이트웨이 밖(genserver 직접호출)이면 키·비용·안전이 무력화. → K8 단일 계층 통합.
6. **온보딩·리텐션·소셜 부재** — 기술 인프라는 깊으나 "왜 계속 접속하는가" 제품 레이어가 구조적 누락. → §7.2 신규 문서.

---

## 이 도메인 요약 3줄

- 픽셀 2.5D MMORPG는 **MyEngine 위에 얹는다**: `engine/scene`을 클라·서버가 헤드리스 공유해 결정론을 공짜로 얻고, `engine/net`·`server/*`·`engine/gameplay` 등을 신설한다.
- 착수 전 **10대 기술선택(전송 UDP·DTLS·Postgres·C++ 서버·Linux 헤드리스·float+tolerance·인스턴싱·단일 AI 게이트웨이·Snowflake·에셋 스트리밍 소유)**을 정본으로 확정하고, 병행 작성으로 깨진 **문서 링크·소유권을 재정합**해야 한다.
- 최대 리스크는 **크로스컴파일 결정론 미검증·소유권 공백(서버 토폴로지·복제 스키마·소셜·온보딩)·경계 dupe·스케일 정량 미검증**이며, 로드맵([10](../10-status-and-roadmap.md))은 싱글플레이 완성→온라인→라이브옵스 순으로 이를 흡수한다.

*문서 버전: 2026-07. 인벤토리 종합 + 01~09 도메인 요약 + 3개 적대적 검수 렌즈 반영.*
