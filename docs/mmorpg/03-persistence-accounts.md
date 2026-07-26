# MMORPG 03. 영속성 · 계정 · 월드 상태 (Persistence · Accounts · World State)

> 소유 도메인: **계정/인증**, **캐릭터/인벤토리/장비/은행/우편**, **월드상태(퀘스트·플래그·스폰·소유권)**,
> **경제(재화·거래로그·경매·세금)**, **저장소 선택(Postgres + Redis + Object Storage)**, **스키마·마이그레이션·버저닝**,
> **샤딩·파티셔닝·리전**, **트랜잭션·원자성·멱등성(아이템 복사·롤백 방지)**, **백업·복구·PITR**,
> **저장주기·저널·write-behind**, **감사로그**, **캐릭터 이전·서버통합**, **GDPR/개인정보/청소년보호**,
> **리플렉션 직렬화(JsonArchive)와 DB 매핑**.
>
> 이 문서는 "권위 있는 진실(source of truth)이 어디에 어떻게 저장되고, 어떻게 절대 유실·복제되지 않는가"를 정한다.
> 게임플레이 규칙(전투·스탯 공식)은 [06 게임플레이](06-gameplay-systems.md), 그 데이터가 오가는 방식은
> [02 넷코드](02-netcode-replication.md)가 소유하며, 이 문서는 **그 데이터가 프로세스가 죽어도 살아남는 계층**을 소유한다.

---

## 1. 목표 · 범위

### 1.1 목표

- **단 하나의 원장(ledger)**: 플레이어의 계정·캐릭터·아이템·재화는 **서버 권위 DB**에만 존재한다. 클라이언트와 게임 서버의 메모리는 전부 캐시이며, "진실"은 Postgres 한 곳이다.
- **아이템은 절대 복사되지 않고 절대 사라지지 않는다**: 거래·우편·드롭·죽음·서버 크래시·중복 로그인·네트워크 재시도 어느 경우에도 아이템 총량 보존(conservation of items). 이것이 이 도메인의 제1 요구사항이며 모든 설계 결정의 심판 기준이다.
- **크래시 안전성**: 게임 월드 서버(zone server)가 언제 죽어도, 마지막 커밋 이후의 손실은 "정책상 허용된 저장 주기(≤ N초)" 이내로 유계(bounded)이고, 부분 저장으로 인한 **불일치(예: 골드는 빠졌는데 아이템은 안 들어옴)는 발생하지 않는다**.
- **1인 개발 현실주의**: MyEngine의 클라이언트/에디터는 순수 C++20·예외 비사용이지만, **서버·DB 계층은 별개의 배포 단위**다. DB 접근은 libpq/hiredis 같은 검증된 C 라이브러리를 얇은 `Expected<T,Error>` 래퍼 뒤에 둔다. 차별점이 아닌 곳은 기성품을 쓴다([00-overview](../00-overview.md) 보조 원칙 계승).
- **리플렉션 재사용**: 게임플레이 컴포넌트를 `Reflect<T>`에 한 번 등록하면 클라 직렬화·에디터 인스펙터·Lua·**그리고 DB 매핑(JSONB 컬럼)** 까지 자동으로 따라오게 한다 — [00-overview](../00-overview.md) "리플렉션 원-스톱 루프"를 서버 영속성으로 확장.

### 1.2 범위 (In / Out)

| In (이 문서 소유) | Out (참조만) |
|---|---|
| 계정·세션·인증(가입/로그인/OAuth/2FA/토큰/차단) | 실시간 이동·전투 복제 → [02 넷코드](02-netcode-replication.md) |
| 캐릭터 CRUD·슬롯·외형·스탯 영속 | 전투/스탯 **공식**·인벤 UI → [06 게임플레이](06-gameplay-systems.md) |
| 인벤/장비/은행/창고/우편 **저장 스키마·원자적 이동** | 채팅 전송·소셜 그래프 → [07 소셜](07-social-chat.md) |
| 월드상태(퀘스트 진행·플래그·스폰·소유권) 영속 | 필드 스폰 **AI 동작** → [06 게임플레이](06-gameplay-systems.md) |
| 경제(재화 원장·거래로그·경매·세금) | 경매 UI·가격 표시 → [06 게임플레이](06-gameplay-systems.md) |
| 저장소 토폴로지(RDB/캐시/오브젝트) | 배포·오토스케일 → [09 운영·인프라](09-liveops-infra.md) |
| 스키마·마이그레이션·버저닝·롤백 | 치트 탐지 로직 → [08 보안·안티치트](08-security-anticheat.md) |
| 트랜잭션·멱등성·중복방지 | 서버 프레임 루프 → [01 서버 아키텍처](01-server-architecture.md) |
| 백업·PITR·저장주기·저널 | 로그 수집 파이프라인 → [09 운영·인프라](09-liveops-infra.md) |
| 감사로그(economy audit) | GM 툴 UI → [09 운영·인프라](09-liveops-infra.md) |
| 캐릭터 이전·서버통합(merge) | 매치메이킹/방 → [01 서버 아키텍처](01-server-architecture.md) |
| GDPR 삭제/추출·청소년보호 시간제 | 결제(PG 연동) → [09 운영·인프라](09-liveops-infra.md) |

> **주의**: MyEngine은 오늘 클라이언트/에디터 엔진이다. 이 문서의 대부분은 `server/` 아래 **신규 배포 단위**를 정의한다. 기존 엔진과의 접점은 (1) `engine/reflect`(직렬화 재사용), (2) `engine/runtime`의 `SaveSystem`(싱글플레이 폴백·오프라인 모드), (3) `engine/scene`의 컴포넌트 타입(서버가 헤드리스로 같은 ECS를 돌림 — [01](01-server-architecture.md))이다.

---

## 2. 핵심 개념 · 아키텍처

### 2.1 저장 계층 3분할 (Storage Tiers)

```
                    ┌────────────────────────────────────────────┐
   게임서버(zone)   │  Live World State (RAM, ECS World)          │  ← 휘발성, 권위=서버
   ─────────────    │   Character/Inventory 컴포넌트 = 진실의 캐시  │
   [01] 서버 루프    └───────────────┬────────────────────────────┘
                                     │ write-behind (더티 큐, ≤5s 또는 트랜잭션 즉시)
                    ┌────────────────▼────────────────┐  ┌─────────────────────┐
   Tier-1 캐시      │  Redis (핫 세션·락·큐·leaderboard) │  │ 세션토큰·zone라우팅   │
   ─────────────    │   char:{id} 핫캐시, 분산락, 우편 배지 │  │ presence, rate-limit │
                    └────────────────┬────────────────┘  └─────────────────────┘
                    ┌────────────────▼─────────────────────────────────────────┐
   Tier-0 원장      │  PostgreSQL (권위 있는 진실 — accounts·characters·items·  │
   ─────────────    │   economy_ledger·world_state·audit) + 논리 복제 + PITR    │
                    └────────────────┬─────────────────────────────────────────┘
                    ┌────────────────▼────────────────┐
   Tier-2 콜드      │  Object Storage (S3 호환)          │
   ─────────────    │   백업·스냅샷·GDPR 추출 dump·리플레이 │
                    └──────────────────────────────────┘
```

- **Tier-0 Postgres = 유일한 권위**. 재화·아이템·거래 로그는 반드시 여기서 ACID 트랜잭션으로 이동한다. Redis는 절대 재화의 진실을 가지지 않는다(캐시·락·큐만).
- **Tier-1 Redis**: (a) 세션/프레즌스, (b) 분산락(캐릭터 단일 소유권·경매 입찰 직렬화), (c) 지연 큐(우편·경매 만료), (d) 랭킹 ZSET, (e) 캐릭터 핫캐시(로그인 시 DB→Redis, write-behind로 Postgres에 flush).
- **Tier-2 오브젝트 스토리지**: 논리 백업(pg_dump·WAL 아카이브), GDPR 데이터 추출본, 전투/거래 리플레이 blob, 큰 UGC(스크린샷·길드 엠블럼 PNG). MyEngine의 `IFileSystem`([04-asset-pipeline](../04-asset-pipeline.md)) 확장점으로 S3 백엔드를 붙일 수 있으나, 서버 측은 별도 SDK 사용이 단순.

### 2.2 권위·캐시 규약 (Authority)

| 데이터 | 권위(진실) | 캐시 | 쓰기 정책 |
|---|---|---|---|
| 재화(gold/캐시) | Postgres `economy_ledger` | Redis balance | **write-through**(즉시 커밋 후 캐시 갱신) |
| 아이템 소유·위치 | Postgres `items` | ECS 컴포넌트(RAM) | 트랜잭션 이동 즉시 커밋 |
| 캐릭터 스탯·좌표 | Postgres `characters` | ECS 컴포넌트(RAM) | **write-behind**(주기 스냅샷 ≤5s + 이벤트 트리거) |
| 퀘스트 진행·플래그 | Postgres `world_state` | ECS 컴포넌트 | write-behind + 완료 시 즉시 |
| 세션·프레즌스 | Redis(권위 = ephemeral) | — | write-through(짧은 TTL) |
| 우편·경매 | Postgres | Redis 만료큐 | 트랜잭션 |

핵심 규칙: **"돈이 걸린 것은 write-through, 잃어도 되는 편의는 write-behind"**. 좌표는 5초 유실돼도 되지만(부활 지점으로 복귀), 골드/아이템은 절대 유실 불가.

### 2.3 리플렉션 → DB 매핑 (재사용 핵심)

MyEngine의 컴포넌트는 이미 `Reflect<T>`로 필드 메타를 갖는다([04-asset-pipeline](../04-asset-pipeline.md)). 이를 서버가 그대로 재사용한다:

- **행(row) = 정형 컬럼 + JSONB 페이로드 하이브리드**. 인덱싱·쿼리·정합성 제약이 필요한 필드(`account_id`, `character_id`, `gold`, `item_template_id`, `location`)는 **정형 SQL 컬럼**으로 승격(promoted). 나머지 가변 게임플레이 필드(버프 목록, 퀘스트 변수 blob, 커스텀 컴포넌트)는 **`JsonArchive`로 직렬화한 JSONB 컬럼**(`data`)에 담는다.
- 이렇게 하면 게임 디자이너가 컴포넌트에 필드를 추가해도 **DB 마이그레이션 없이** JSONB에 자연 편입되고, 성능/제약이 필요할 때만 `promote` 마이그레이션으로 컬럼화한다.
- `IArchive`의 **버전 필드**(`__version`)·`RenamedFrom` 폴백([Archive.h](../../engine/reflect/include/mye/ser/Archive.h))이 JSONB 안의 구버전 데이터를 로드 시 승격한다 — 즉 **세이브 마이그레이션 인프라를 서버 영속성에 재활용**한다.

```cpp
// server/persist 의 매핑 규약 (개념)
// 정형 컬럼 = 인덱스/제약 필요, JSONB = 자유 게임플레이 필드.
struct CharacterRow {
    int64_t     characterId;   // PK (snowflake, 네트워크 안정 ID)
    int64_t     accountId;     // FK
    int32_t     shardId;       // 파티셔닝 키
    std::string name;          // UNIQUE(리전 내)
    int32_t     level;         // 정형(랭킹/필터)
    int64_t     experience;
    int16_t     mapId;         // 정형(존 라우팅)
    float       x, y, floor;   // 좌표(write-behind)
    // ── 나머지 전부 JSONB: 스탯 상세·버프·외형·퀘스트변수 ──
    std::string dataJsonb;     // JsonArchive::ToValue(CharacterGameplay).Dump()
    uint32_t    dataVersion;   // 컴포넌트 스키마 버전(마이그레이션 트리거)
    int64_t     updatedAtUnix;
    int64_t     rowVersion;    // 낙관적 락(optimistic concurrency)
};
```

### 2.4 네트워크 안정 ID (Snowflake) — Entity 핸들 한계 극복

MyEngine의 `ecs::Entity`는 `index:32 | generation:32`로 **프로세스 로컬**이다([Entity.h](../../engine/scene/include/mye/ecs/Entity.h) 주석: "핸들은 process-local, network-stable ID 아님"). MMO는 재부팅·존 이동·샤드 간에도 안정적인 64비트 ID가 필요하므로 **Snowflake ID**를 도입한다.

```
snowflake = [ 42bit: ms since epoch ] [ 6bit: shardId ] [ 6bit: nodeId ] [ 10bit: seq ]
```

- `account_id`, `character_id`, `item_instance_id`, `mail_id`, `auction_id`, `guild_id`, `trade_id` 전부 snowflake. 시간 정렬성(k-sortable)으로 인덱스 지역성 + 샤드/노드 내장으로 라우팅 힌트 획득.
- 서버 ECS에서 각 엔티티에 `NetId{ int64 snowflake }` 컴포넌트를 붙여 로컬 `Entity` ↔ 안정 ID를 매핑(`unordered_map<int64,Entity>`). 클라·DB·로그는 전부 snowflake로만 대상을 지칭한다.

---

## 3. 기능 목록

우선순위: **P0**=최소 서비스 필수(없으면 상용 불가) · **P1**=정식 오픈 필수 · **P2**=라이브 운영 안정화 · **P3**=성장/편의 · **P4**=장기.
상태: **있음**=엔진에 재사용 가능한 실동작 코드 · **부분**=일부 인프라만 존재 · **신규**=새로 만들어야 함.

### 3.1 계정 · 인증

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| 계정 가입(이메일/약관 동의/나이확인) | P0 | 신규 | `server/account` 신규. 비번 Argon2id 해시. Postgres `accounts` |
| 로그인(자격증명 → 세션토큰 발급) | P0 | 신규 | `server/account` + `server/gateway`(로그인 서버). Redis 세션 |
| 세션 토큰(단기 access + 장기 refresh, 서명) | P0 | 부분 | 서명키만 [01-core](../01-core-platform.md) `Config`에서 로드. JWT-유사 자체 토큰 |
| 재접속·토큰 갱신·다중 기기 정책 | P0 | 신규 | Redis `session:{accountId}` 단일화(중복 로그인 축출) |
| OAuth(구글/애플/스팀) 연동 | P1 | 신규 | `server/account/oauth` 어댑터. provider→account 링크 테이블 |
| 2FA(TOTP/이메일 OTP) | P1 | 신규 | `accounts.totp_secret`(암호화). 로그인 2단계 챌린지 |
| 비밀번호 재설정·이메일 검증 | P1 | 신규 | 만료 토큰 + 이메일 발송([09](09-liveops-infra.md) 연동) |
| 계정 차단/정지(ban·suspend·shadowban) | P0 | 신규 | `accounts.status` + 만료. 게이트웨이가 로그인 거부 |
| 로그인 속도제한·브루트포스 방어 | P0 | 부분 | Redis rate-limit. [08 보안](08-security-anticheat.md)과 공유 |
| 디바이스/IP 지문·이상 로그인 알림 | P2 | 신규 | 로그인 감사 + 이상 위치 탐지 |

### 3.2 캐릭터

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| 캐릭터 생성(외형·직업·이름 유니크) | P0 | 신규 | `server/character`. 이름 예약어·비속어 필터. 이름 UNIQUE(리전) |
| 캐릭터 슬롯(계정당 N개·확장 상품) | P0 | 신규 | `accounts.char_slots` + 생성 시 카운트 체크 |
| 캐릭터 삭제(소프트 삭제 + 유예기간) | P0 | 신규 | `characters.deleted_at`. 유예 내 복구 가능 |
| 캐릭터 로드(로그인 → World 스폰) | P0 | 부분 | 서버 헤드리스 ECS([01](01-server-architecture.md)) + `JsonArchive` 역직렬화 재사용 |
| 캐릭터 스탯·경험치·외형 영속 | P0 | 부분 | `Reflect<T>` 컴포넌트 → JSONB. write-behind |
| 캐릭터 저장(주기+이벤트 트리거) | P0 | 부분 | `SaveSystem`의 참여자 패턴을 서버 `IPersistParticipant`로 이식 |
| 캐릭터 이름 변경(상품·쿨다운) | P2 | 신규 | 이름 유니크 재검사 + 감사로그 |
| 캐릭터 외형 커스터마이즈 저장 | P1 | 신규 | 외형 JSONB. [06](06-gameplay-systems.md) 렌더 연동 |

### 3.3 인벤토리 · 장비 · 은행 · 창고 · 우편

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| 아이템 인스턴스 원장(고유 ID·소유자·위치) | P0 | 신규 | Postgres `items`(snowflake PK). **모든 아이템 이동의 진실** |
| 아이템 템플릿(정적 데이터=에셋) | P0 | 부분 | [04-asset-pipeline](../04-asset-pipeline.md) 에셋 + GUID. 서버는 template_id 참조 |
| 인벤토리 슬롯·스택·정렬 | P0 | 신규 | `items.container_type/slot/stack_count` |
| 장비 착용/해제(원자적) | P0 | 신규 | 슬롯 이동 트랜잭션 |
| 은행/공유 창고(계정 귀속) | P1 | 신규 | `container_type=bank`, account 소유. 동시성 락 |
| 우편(첨부 아이템·골드·만료·수령) | P1 | 신규 | `mail` + 첨부는 `items.container=mail:{id}`. 만료 큐(Redis) |
| 아이템 강화/개조/귀속(bind) | P2 | 신규 | `items.data` JSONB(강화수치)·`bound_to` |
| 아이템 이동 원자성(복사 방지) | P0 | 신규 | 단일 트랜잭션 + `rowVersion` 낙관락 + 멱등키 |
| 컨테이너 용량·무게 제약 | P1 | 신규 | 서버 검증(클라 신뢰 금지, [08](08-security-anticheat.md)) |

### 3.4 월드 상태

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| 퀘스트 진행·완료·반복 쿨다운 | P0 | 신규 | `world_state`(character 스코프) JSONB + 정형 인덱스 |
| 전역 플래그·이벤트 상태(월드 보스 처치 등) | P1 | 신규 | `world_flags`(shard 스코프). Redis 캐시 |
| 스폰 소유권·채집물·필드 오브젝트 상태 | P1 | 신규 | 대부분 휘발(RAM)·중요분만 영속. [06](06-gameplay-systems.md) |
| 하우징/필드 소유권·임대 만료 | P3 | 신규 | `ownership`(만료 큐). 오브젝트 스토리지에 레이아웃 |
| 존/맵 인스턴스 상태(던전 진행) | P2 | 부분 | 인스턴스는 휘발, 결과만 영속. [01](01-server-architecture.md) 인스턴싱 |
| 캐릭터 위치 복원(로그아웃 지점) | P0 | 부분 | `characters.mapId/x/y/floor` write-behind |

### 3.5 경제 (Economy)

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| 재화 원장(gold/캐시/토큰, 이중부기) | P0 | 신규 | `economy_ledger`(append-only, 차변/대변) — 진실 |
| 거래(플레이어 간, 2자 동시확정) | P0 | 신규 | 2-phase 확정 + 단일 트랜잭션. 스캠 방지 로그 |
| 거래/획득/소모 로그(전량 기록) | P0 | 신규 | `economy_ledger` = 감사 가능한 append-only |
| 경매장(등록·입찰·낙찰·수수료) | P1 | 신규 | `auctions` + 입찰 직렬화(Redis 락) + 만료 큐 |
| NPC 상점(구매/판매/재고·시세) | P1 | 신규 | 트랜잭션. 재고 shard 스코프 |
| 세금·수수료·재화 sink | P1 | 신규 | 인플레 제어. 원장에 sink 엔트리 |
| 재화/아이템 상한·오버플로우 방지 | P0 | 신규 | int64 + 서버 검증. 음수/오버플로 거부 |
| 경제 지표·인플레 모니터링 | P2 | 신규 | 원장 집계 → [09](09-liveops-infra.md) 대시보드 |

### 3.6 저장소 · 스키마 · 신뢰성

| 기능 | 우선 | 상태 | 엔진 매핑 |
|---|---|---|---|
| Postgres 스키마·마이그레이션 도구 | P0 | 신규 | `server/db/migrations`(순번 SQL). sqitch/자체 러너 |
| 리플렉션↔JSONB 직렬화 브리지 | P0 | **있음** | `JsonArchive`([JsonArchive.h](../../engine/reflect/include/mye/ser/JsonArchive.h)) 그대로 재사용 |
| 스키마 버전·데이터 마이그레이션 | P0 | 부분 | `ISaveMigration` 체인([SaveSystem.h](../../engine/runtime/include/mye/runtime/SaveSystem.h)) 서버 이식 |
| 트랜잭션·멱등성 프레임워크 | P0 | 신규 | `server/persist` 트랜잭션 헬퍼 + 멱등키 테이블 |
| write-behind 더티 큐·저널 | P0 | 부분 | `SaveSystem` 원자 쓰기 개념 확장. WAL 스타일 저널 |
| Redis 캐시·분산락·만료큐 | P0 | 신규 | `server/cache` hiredis 래퍼 |
| 백업·PITR·WAL 아카이브 | P0 | 신규 | pg_basebackup + WAL → S3. 복구 리허설 |
| 감사로그(economy/admin action) | P0 | 신규 | `audit_log` append-only + [09](09-liveops-infra.md) 수집 |
| 샤딩·파티셔닝·리전 | P2 | 신규 | shardId 라우팅. char/account 파티션 |
| 캐릭터 이전·서버 통합(merge) | P3 | 신규 | export→import 파이프라인. 이름 충돌 해소 |
| GDPR 삭제·데이터 추출 | P1 | 신규 | 삭제 요청 큐 + 추출 dump(S3). 익명화 |
| 청소년보호(플레이시간·셧다운) | P2 | 신규 | 세션 누적 시간 + 정책 게이트 |

---

## 4. 데이터 모델 · 스키마

### 4.1 계정 · 세션

```sql
-- accounts: 계정 = 인증 주체. 재화·아이템의 궁극 소유자는 캐릭터지만 은행/캐시는 계정 귀속.
CREATE TABLE accounts (
    account_id      BIGINT PRIMARY KEY,          -- snowflake
    email           CITEXT UNIQUE,               -- 대소문자 무시 유니크
    email_verified  BOOLEAN NOT NULL DEFAULT false,
    pw_hash         TEXT,                         -- Argon2id (NULL이면 OAuth 전용)
    totp_secret_enc BYTEA,                        -- 2FA(암호화 저장), NULL=미설정
    status          SMALLINT NOT NULL DEFAULT 0,  -- 0=active 1=suspended 2=banned 3=deletion_pending
    status_until    TIMESTAMPTZ,                  -- 정지 만료(NULL=영구)
    char_slots      SMALLINT NOT NULL DEFAULT 4,
    birth_date      DATE,                         -- 청소년보호/나이확인
    region          SMALLINT NOT NULL,            -- 리전(데이터 주권·레이턴시)
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    tos_version     INT NOT NULL,                 -- 동의한 약관 버전
    gdpr_delete_at  TIMESTAMPTZ                   -- 삭제 예약 시각
);

CREATE TABLE oauth_links (
    account_id  BIGINT REFERENCES accounts,
    provider    SMALLINT NOT NULL,               -- 0=google 1=apple 2=steam
    provider_uid TEXT NOT NULL,
    PRIMARY KEY (provider, provider_uid)          -- 한 소셜 계정 = 한 게임 계정
);

-- 세션은 Redis가 권위(휘발). Postgres에는 감사용 로그인 이력만 남긴다.
CREATE TABLE login_audit (
    id          BIGSERIAL PRIMARY KEY,
    account_id  BIGINT,
    at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    ip          INET, device_hash TEXT, result SMALLINT  -- 0=ok 1=badpw 2=locked 3=2fa_fail
);
```

Redis 세션 키(권위=ephemeral):
```
session:{accountId}      -> {token, characterId?, zoneNode, expiresAt}   TTL=access
refresh:{refreshId}      -> {accountId}                                   TTL=30d
presence:{accountId}     -> {status, zone, charId}                        TTL=heartbeat*3
lock:char:{characterId}  -> {ownerNode}  (SET NX PX)  단일 소유권
ratelimit:login:{ip}     -> counter                                       TTL=window
```

### 4.2 캐릭터 · 아이템 (원장 핵심)

```sql
CREATE TABLE characters (
    character_id  BIGINT PRIMARY KEY,             -- snowflake
    account_id    BIGINT NOT NULL REFERENCES accounts,
    shard_id      SMALLINT NOT NULL,
    name          CITEXT NOT NULL,
    level         INT NOT NULL DEFAULT 1,
    experience    BIGINT NOT NULL DEFAULT 0,
    map_id        SMALLINT NOT NULL,
    x REAL, y REAL, floor REAL,                    -- 로그아웃 지점(write-behind)
    data          JSONB NOT NULL,                  -- 스탯/외형/버프/퀘변수 (JsonArchive)
    data_version  INT NOT NULL,                    -- 컴포넌트 스키마 버전
    play_seconds  BIGINT NOT NULL DEFAULT 0,       -- 누적 플레이(청소년보호)
    row_version   BIGINT NOT NULL DEFAULT 0,       -- 낙관적 락
    deleted_at    TIMESTAMPTZ,                     -- 소프트 삭제
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (shard_id, name)                         -- 리전 내 이름 유니크
);
CREATE INDEX ON characters (account_id) WHERE deleted_at IS NULL;

-- items: 아이템 인스턴스 = 세상에 단 하나. 이동 = owner/container/slot UPDATE(복사 아님).
CREATE TABLE items (
    item_id        BIGINT PRIMARY KEY,             -- snowflake, 절대 재사용 안 함
    template_id    INT NOT NULL,                   -- 정적 아이템 정의(에셋 GUID 매핑)
    owner_char     BIGINT,                         -- 소유 캐릭터(NULL 가능: 우편/경매 중)
    owner_account  BIGINT,                         -- 계정 귀속(은행)
    container      SMALLINT NOT NULL,              -- 0=inv 1=equip 2=bank 3=mail 4=auction 5=trade_escrow
    container_ref  BIGINT,                         -- 우편/경매/거래 ID
    slot           SMALLINT NOT NULL,
    stack_count    INT NOT NULL DEFAULT 1,
    bound_to       BIGINT,                         -- 귀속 캐릭터(거래 불가)
    data           JSONB,                          -- 강화·소켓·내구도 등
    row_version    BIGINT NOT NULL DEFAULT 0,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX ON items (owner_char, container) WHERE owner_char IS NOT NULL;
CREATE INDEX ON items (container, container_ref);
```

### 4.3 경제 원장(이중부기 · append-only)

```sql
-- 재화의 진실. balance는 파생(집계)이며 절대 직접 UPDATE하지 않는다.
-- 모든 재화 이동은 여기에 +/- 쌍으로 append (double-entry). 총합=0이 항상 성립해야 함.
CREATE TABLE economy_ledger (
    entry_id     BIGINT PRIMARY KEY,               -- snowflake
    character_id BIGINT,                            -- 대상(NULL=시스템 sink/source)
    account_id   BIGINT,
    currency     SMALLINT NOT NULL,                 -- 0=gold 1=cash 2=eventtoken
    delta        BIGINT NOT NULL,                   -- +획득/-소모 (음수 허용)
    reason       SMALLINT NOT NULL,                 -- trade/quest/npc_buy/tax/mail/...
    ref_id       BIGINT,                            -- 관련 거래·경매·우편 ID
    idem_key     TEXT UNIQUE,                       -- 멱등키(중복 재시도 무효화)
    at           TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX ON economy_ledger (character_id, currency, at);

-- 잔액 캐시(파생, 재계산 가능). Redis에도 미러. 진실은 ledger 집계.
CREATE TABLE balances (
    character_id BIGINT, currency SMALLINT, amount BIGINT NOT NULL DEFAULT 0,
    row_version  BIGINT NOT NULL DEFAULT 0,
    PRIMARY KEY (character_id, currency),
    CHECK (amount >= 0)                              -- 음수 잔액 = 즉시 거부(오버드로우 방지)
);

CREATE TABLE trades (
    trade_id   BIGINT PRIMARY KEY,
    a_char BIGINT, b_char BIGINT,
    a_confirmed BOOLEAN DEFAULT false, b_confirmed BOOLEAN DEFAULT false,
    a_gold BIGINT DEFAULT 0, b_gold BIGINT DEFAULT 0,
    state SMALLINT NOT NULL,                         -- 0=open 1=locked 2=committed 3=cancelled
    created_at TIMESTAMPTZ DEFAULT now()
);

CREATE TABLE auctions (
    auction_id BIGINT PRIMARY KEY, seller_char BIGINT, item_id BIGINT,
    start_bid BIGINT, buyout BIGINT, cur_bid BIGINT, cur_bidder BIGINT,
    fee BIGINT, expires_at TIMESTAMPTZ, state SMALLINT
);

CREATE TABLE mail (
    mail_id BIGINT PRIMARY KEY, to_char BIGINT, from_char BIGINT,
    subject TEXT, body TEXT, attach_gold BIGINT DEFAULT 0,
    claimed BOOLEAN DEFAULT false, expires_at TIMESTAMPTZ, sent_at TIMESTAMPTZ DEFAULT now()
);

-- 감사로그(경제·관리자 행위 append-only). 롤백 조사·복구 근거.
CREATE TABLE audit_log (
    id BIGSERIAL PRIMARY KEY, at TIMESTAMPTZ DEFAULT now(),
    actor_type SMALLINT, actor_id BIGINT,           -- player/gm/system
    action SMALLINT, target_type SMALLINT, target_id BIGINT,
    detail JSONB
);

-- 멱등성 테이블: 네트워크 재시도로 같은 요청이 두 번 와도 한 번만 적용.
CREATE TABLE idempotency (
    idem_key TEXT PRIMARY KEY,
    result   JSONB,                                  -- 최초 실행 결과(재시도 시 그대로 반환)
    at       TIMESTAMPTZ DEFAULT now()
);
```

### 4.4 아이템 이동 = 단일 트랜잭션 (복사·유실 원천 차단)

```cpp
// server/persist/ItemOps.cpp (개념) — 모든 이동은 이 한 함수를 통과.
// 성공 = 커밋, 실패 = 전체 롤백. 부분 상태 없음.
Expected<void, Error> MoveItem(pg::Txn& tx, ItemMove m) {
    // 1) 멱등: 이미 처리된 요청이면 즉시 성공 반환(중복 클릭·재시도 흡수)
    if (tx.IdempotentAlready(m.idemKey)) return {};

    // 2) 소유·위치·row_version 검증 (낙관적 락 — 그 사이 바뀌었으면 실패)
    auto row = tx.SelectForUpdate("items", m.itemId);   // SELECT ... FOR UPDATE
    if (!row) return Error{"item_gone"};
    if (row->ownerChar != m.fromChar || row->rowVersion != m.expectRowVersion)
        return Error{"conflict"};                        // 다른 트랜잭션이 선점 → 재시도

    // 3) 대상 컨테이너 용량·귀속·거래가능 검증(서버 권위 — 클라 신뢰 안 함)
    if (m.toContainer == Container::Trade && row->boundTo)  return Error{"bound_untradable"};
    if (!tx.HasCapacity(m.toChar, m.toContainer, m.toSlot)) return Error{"no_space"};

    // 4) 이동 = UPDATE (INSERT 아님 → 복사 불가). row_version++ 로 후속 충돌 감지.
    tx.Exec("UPDATE items SET owner_char=$1, container=$2, slot=$3, row_version=row_version+1 "
            "WHERE item_id=$4 AND row_version=$5", m.toChar, m.toContainer, m.toSlot, m.itemId, row->rowVersion);

    // 5) 감사 + 멱등 기록 (같은 트랜잭션 안에서 — 원자성)
    tx.Audit(AuditAction::ItemMove, m);
    tx.RecordIdempotency(m.idemKey);
    return {};   // 커밋은 호출자(Txn 소멸자/Commit())가 수행
}
```

---

## 5. 경우의 수 · 엣지케이스 (exhaustive)

### 5.1 아이템 복사·유실 (제1 위협)

| 상황 | 위험 | 방어 |
|---|---|---|
| 거래 확정 중 한쪽 로그아웃/크래시 | 아이템 이중 지급 or 유실 | 단일 트랜잭션 확정(양쪽 확정 후 원자 커밋), 미확정 시 escrow에서 원위치 롤백 |
| 우편 첨부 수령 도중 서버 크래시 | 우편·인벤 양쪽 존재(복사) | 첨부는 `items` 이동 UPDATE 1건. `mail.claimed`도 같은 트랜잭션 |
| 같은 "장착" 요청 네트워크 재시도 2회 | 아이템 2개 생성 | **멱등키**(요청 UUID) — 두 번째는 무효 |
| 존 이동 중 저장 전 크래시 | 출발/도착 양쪽에 캐릭터·아이템 | 핸드오프는 [01](01-server-architecture.md) 프로토콜: DB 커밋 완료 후에만 도착 존이 소유권 획득 |
| 두 존 서버가 같은 캐릭터 동시 로드 | 병행 소유(복사 근원) | Redis `lock:char:{id}` SET NX — 단일 소유권. 락 없으면 로드 거부 |
| 낙관적 락 충돌(동시 두 이동) | 마지막 쓰기 승리로 한 이동 소실 | `row_version` 불일치 → 실패 반환 → 상위가 재시도 |
| 스택 분할/합치기 경합 | stack_count 계산 오류 | `SELECT FOR UPDATE` 행 잠금 + CHECK 제약 |
| 아이템 삭제 후 ID 재사용 | 오래된 참조가 새 아이템 지칭 | snowflake는 **절대 재사용 안 함**(monotonic). 삭제는 tombstone |
| DB 커밋 성공했으나 클라 응답 유실 | 클라가 재시도 → 중복 | 멱등키가 최초 결과를 그대로 반환 |
| 관리자 아이템 지급 중복 실행 | 인플레 | GM 액션도 멱등키 + 감사로그 필수 |

### 5.2 재화(골드/캐시)

- **음수 방지**: `balances.amount CHECK(>=0)` + 소모는 항상 잔액 확인 후. 오버드로우 시도는 트랜잭션 실패.
- **오버플로**: int64. 상한 도달 시 획득 거부(초과분 소각 로그).
- **이중 지급**: 원장 append + 멱등키. 같은 quest 보상은 `idem_key=quest:{qid}:{charId}`로 1회만.
- **거래 취소 롤백**: escrow 상태(`container=trade_escrow`)로 격리 → 취소 시 원소유자 복귀 UPDATE.
- **경매 동시 입찰**: Redis 락으로 입찰 직렬화 + Postgres `cur_bid` 낙관락. 진 입찰자 골드는 escrow에서 자동 환불.
- **인플레 모니터링**: 원장 집계로 faucet(생성)/sink(소각) 균형 추적 → [09](09-liveops-infra.md).

### 5.3 계정·세션·인증

| 상황 | 방어 |
|---|---|
| 중복 로그인(같은 계정 두 기기) | Redis `session:{acc}` 단일화 → 기존 세션 축출(kick) 또는 신규 거부(정책) |
| 세션 토큰 탈취 | 짧은 access TTL + refresh 회전(rotation) + 기기지문 불일치 시 재인증 |
| 로그인 브루트포스 | Redis rate-limit + 계정 잠금(status_until) + [08](08-security-anticheat.md) |
| 밴 우회(재가입) | 기기/결제 지문 + 이메일 도메인 규칙. 밴은 계정+지문 스코프 |
| 2FA 분실 | 백업 코드 + 이메일 검증 복구 경로 |
| OAuth provider 장애 | 자체 비번 폴백(링크된 경우). provider 링크는 다대일 금지 |
| 삭제 예약 계정 재로그인 | `gdpr_delete_at` 취소(유예 내) or 거부(경과) |
| 토큰 서명키 유출 | 키 회전(kid) 지원. Config에서 다중 키 로드([01-core](../01-core-platform.md)) |

### 5.4 크래시·저장·복구

| 상황 | 방어 |
|---|---|
| 존 서버 크래시(write-behind 미flush) | 손실 ≤ 저장주기(5s) + **저널**(WAL 스타일 append로 replay). 골드/아이템은 write-through라 무손실 |
| DB 프라이머리 다운 | 동기 복제 standby → 자동 페일오버. RPO≈0(동기), RTO 수십초 |
| DB 데이터 손상·잘못된 마이그레이션 | **PITR**(WAL 아카이브로 사고 직전 시점 복구) + 백업 복구 리허설 정례화 |
| 부분 저장(캐릭터는 저장, 아이템 실패) | 캐릭터 스냅샷과 아이템 이동은 **별 트랜잭션**이나 아이템은 항상 즉시 커밋(캐릭터보다 강한 보장) |
| 저장 폭주(동시 수천 캐릭터 flush) | 더티 큐 배치 + 코얼레싱(같은 캐릭터 중복 더티 병합) + 백프레셔 |
| 롤백 후 아이템 복원 | 감사로그+원장으로 정확히 재생(replay) 가능. 이것이 append-only의 이유 |
| Redis 캐시 유실(플러시) | 캐시는 재구축 가능(진실=Postgres). 락 유실 시 짧은 TTL로 자동 회복 |
| 마이그레이션 중 롤백 필요 | 각 마이그레이션은 up/down 쌍. 배포 전 shadow DB 리허설 |

### 5.5 스케일·샤딩·동시성

- **핫 캐릭터/핫 로우**: 인기 경매·이벤트 아이템 → 락 경합. Redis 락 + 큐로 직렬화, 필요 시 파티션.
- **커넥션 풀 고갈**: PgBouncer(트랜잭션 풀링). 서버 노드당 풀 제한.
- **샤드 간 캐릭터 이전**: export(락→dump→검증)→import(신규 ID 매핑·이름 충돌 해소)→원본 tombstone. 원자적 컷오버.
- **서버 통합(merge)**: 이름 충돌 = 접미사/강제 개명 상품 지급. 아이템 ID는 전역 유니크(snowflake)라 충돌 없음.
- **리전 데이터 주권(GDPR)**: EU 계정 데이터는 EU 리전 DB에 상주. `accounts.region` 라우팅.
- **분산 트랜잭션 회피**: 하나의 재화/아이템 이동은 **단일 샤드 내**로 설계(2PC 지양). 크로스샤드는 우편(비동기 saga)으로.

### 5.6 GDPR · 청소년보호 · 법규

| 상황 | 처리 |
|---|---|
| 데이터 추출 요청(right to access) | 계정 전체를 S3 dump(JSON)로 생성 → 다운로드 링크 |
| 삭제 요청(right to erasure) | 유예 후 PII 익명화(email/ip 삭제, 캐릭터명 난수화). 원장은 법정 보존기간 후 삭제 |
| 미성년 셧다운(리전별) | `birth_date` + 세션 시간 → 정책 게이트가 접속 차단/경고 |
| 약관 재동의(tos_version 상승) | 로그인 시 미동의면 게이트에서 동의 강제 |
| 결제 환불 시 재화 회수 | 원장에 음수 엔트리 + 잔액 부족 시 부채(negative) 플래그 |
| 로그 보존 vs 삭제 충돌 | 감사/원장은 법정 기간 보존, PII만 익명화(분리 설계) |

---

## 6. 신규 모듈 · 파일 제안

> 대부분 **신규 배포 단위** `server/`. 기존 엔진 재사용은 `engine/reflect`(직렬화)와 `engine/runtime/SaveSystem`(패턴).

```
server/
  db/
    migrations/               # 0001_init.sql, 0002_add_auctions.sql ... (순번 SQL)
    schema.md                 # 스키마 정본 문서
  persist/                    # ★ 핵심: 영속성 계층
    include/mye/server/persist/
      Snowflake.h             # 네트워크 안정 ID 생성기(시간·shard·node·seq)
      PgConnection.h          # libpq 얇은 래퍼(Expected<T,Error>, 풀)
      Transaction.h           # Txn RAII(BEGIN/COMMIT/ROLLBACK, SELECT FOR UPDATE)
      Idempotency.h           # 멱등키 기록/조회
      IPersistParticipant.h   # SaveSystem::ISaveParticipant의 서버판(섹션별 영속)
      JsonbBridge.h           # refl::TypeInfo + JsonArchive ↔ JSONB 컬럼(★ 엔진 재사용)
      Migrator.h              # 스키마 + 데이터(ISaveMigration 체인 이식) 마이그레이션
    src/
      ItemOps.cpp             # MoveItem/Split/Merge — 아이템 이동 단일 관문
      EconomyOps.cpp          # 이중부기 원장 적용·잔액 검증
      CharacterStore.cpp      # 캐릭터 로드/저장(write-behind 더티 큐)
      WorldStateStore.cpp     # 퀘스트·플래그·소유권 영속
  cache/
    include/mye/server/cache/
      RedisClient.h           # hiredis 래퍼
      DistributedLock.h       # SET NX PX 캐릭터 소유권·경매 락
      ExpiryQueue.h           # 우편/경매 만료 지연 큐(ZSET)
  account/
    src/
      AccountService.cpp      # 가입·인증·상태(ban/suspend)
      SessionService.cpp      # 토큰 발급·갱신·단일화(Redis)
      OAuthAdapter.cpp        # google/apple/steam
      TwoFactor.cpp           # TOTP/OTP
  gateway/                    # 로그인 서버(인증 게이트) — [01]과 경계
  economy/
    src/
      TradeService.cpp        # 2자 거래 2-phase 확정
      AuctionService.cpp      # 경매 등록·입찰·낙찰
      MailService.cpp         # 우편 발송·수령·만료
  backup/
    pitr.md                   # WAL 아카이브·PITR·복구 리허설 절차
    gdpr_export.cpp           # 데이터 추출·삭제 파이프라인
  tools/
    dbtool/                   # 마이그레이션 실행·shadow 리허설·시드 CLI(apps/paktool 스타일)

engine/reflect/  (기존 — 확장만)
  include/mye/ser/BinaryArchive.h   # 계약만 존재 → 서버 캐시/스냅샷용 구현 완료(현재 미구현)

engine/runtime/  (기존 — 오프라인/싱글 폴백)
  SaveSystem.h                # 오프라인 모드·에디터 테스트에서 로컬 폴백 유지(변경 없음)
```

핵심 재사용 지점:
- **`JsonbBridge`**: `ser::SerializeDynamic(ar, typeInfo, obj)`([Serialize.h](../../engine/reflect/include/mye/ser/Serialize.h))로 컴포넌트를 `json::Value`로 만들고 그 문자열을 JSONB로. 로드는 역방향. **엔진 컴포넌트 = DB 스키마**를 코드 중복 없이 연결.
- **`Migrator`**: `SaveSystem`의 `ISaveMigration` 체인·`__version` 필드 폴백을 그대로 이식 — 세이브 마이그레이션 = DB JSONB 마이그레이션.
- **`IPersistParticipant`**: `ISaveParticipant`([SaveSystem.h](../../engine/runtime/include/mye/runtime/SaveSystem.h))의 서버판. "엔진은 배관만, 게임이 무엇을 저장할지 결정" 철학을 서버에서도 유지.

---

## 7. 마일스톤 (작은 검증가능 단위)

| 단계 | 산출물 | 검증(수용 기준) |
|---|---|---|
| **P0-a 스키마·연결** | `PgConnection`·`migrations/0001` + `Snowflake` | 마이그레이션 up/down 왕복, snowflake 유일·k-sortable 단위테스트 |
| **P0-b JSONB 브리지** | `JsonbBridge`(refl↔JSONB) | 컴포넌트 라운드트립(write→JSONB→read) 필드 동일. `__version` 마이그레이션 통과 |
| **P0-c 트랜잭션·멱등** | `Transaction`·`Idempotency`·`MoveItem` | 동시 이동 100개 → 아이템 총량 보존, 재시도 중복 0(멱등) |
| **P0-d 계정·세션** | 가입·로그인·토큰·단일 세션 | 중복 로그인 축출, 브루트포스 차단, 밴 로그인 거부 |
| **P0-e 캐릭터 로드/저장** | `CharacterStore` write-behind + 저널 | 크래시 주입 후 손실 ≤5s, 부분저장 불일치 0 |
| **P0-f 경제 원장** | `EconomyOps` 이중부기 + `balances` CHECK | 원장 총합=0 불변, 음수/오버플로 거부 |
| **P1-a 거래·우편·경매** | Trade/Mail/Auction 서비스 | 확정 중 크래시→복사·유실 0, 만료 큐 동작 |
| **P1-b 은행·창고·GDPR** | 계정 귀속 컨테이너·데이터 추출/삭제 | 동시 접근 정합, GDPR dump/anonymize 검증 |
| **P0-g 백업·PITR** | WAL 아카이브·복구 리허설 자동화 | PITR로 임의 시점 복구 성공, RPO≈0 확인 |
| **P2 샤딩·감사·모니터링** | shard 라우팅·audit·경제 지표 | 샤드 간 이전 무손실, 감사 replay로 롤백 재구성 |
| **P3 서버통합·캐릭터이전** | merge/transfer 파이프라인 | 이름 충돌 해소, 아이템 ID 전역 유일 |

---

## 8. 의존성 · 타 도메인 참조

### 8.1 MMORPG 도메인 문서 (`docs/mmorpg/`)

- [01 서버 아키텍처](01-server-architecture.md) — 존/게이트웨이 토폴로지, 헤드리스 ECS, 존 핸드오프(캐릭터 소유권 이전 프로토콜의 상대편).
- [02 넷코드·복제](02-netcode-replication.md) — 이 문서가 저장하는 상태가 클라에 복제되는 경로. snapshot/delta의 source.
- [04 클라이언트 부트·데이터드리븐](04-client-bootstrap.md) — 클라 로그인 흐름·오프라인 폴백(SaveSystem)과의 경계.
- [06 게임플레이 시스템](06-gameplay-systems.md) — 스탯/인벤/퀘스트/경제의 **런타임 규칙**(이 문서는 그 데이터의 영속을 소유).
- [07 소셜·채팅·길드](07-social-chat.md) — 길드/친구 그래프 영속(이 문서 스키마 확장), 채팅 로그 보존.
- [08 보안·안티치트](08-security-anticheat.md) — 서버 권위 검증·rate-limit·이상거래 탐지(이 문서 트랜잭션과 공유).
- [09 운영·인프라](09-liveops-infra.md) — 배포·백업 운용·GM 툴·로그 수집·결제(PG)·경제 대시보드.

### 8.2 기존 엔진 설계 문서 (`docs/`)

- [00 개요](../00-overview.md) — 확장성 제1원칙·리플렉션 원-스톱 루프(서버 영속성으로 확장).
- [01 코어·플랫폼](../01-core-platform.md) — `Config`(토큰 서명키·DB 접속 정보), `Expected<T,Error>`, `JobSystem`(IO 큐로 DB 오프로드).
- [03 씬·월드](../03-scene-world.md) — 서버가 재사용하는 ECS·컴포넌트(`Entity` 핸들 한계 → snowflake 보완).
- [04 에셋 파이프라인](../04-asset-pipeline.md) — 리플렉션·`JsonArchive`·GUID(아이템 template_id ↔ 에셋 GUID 매핑).
- [06 런타임 시스템](../06-runtime-systems.md) — `SaveSystem`(참여자·마이그레이션 패턴을 서버로 이식)·네트워크 확장 훅.
- [08 MCP](../08-mcp.md) — 개발 도구(향후 DB 시드·마이그레이션 리허설 MCP 툴 확장 여지).

---

## 이 도메인 요약 3줄

1. **진실은 Postgres 한 곳**: 재화·아이템·캐릭터는 서버 권위 DB에만 존재하고, 재화/아이템은 write-through·좌표는 write-behind이며, 모든 아이템 이동은 `MoveItem` 단일 트랜잭션(낙관락+멱등키+감사)을 통과해 **복사도 유실도 원천 차단**한다.
2. **엔진 재사용의 핵심은 리플렉션↔JSONB 브리지**: `JsonArchive`·`SerializeDynamic`·`ISaveMigration`·`ISaveParticipant`(engine/reflect·runtime)를 서버 `JsonbBridge`/`Migrator`/`IPersistParticipant`로 이식해 컴포넌트 = DB 스키마를 코드 중복 없이 잇고, `Entity`의 process-local 한계는 snowflake ID로 보완한다.
3. **신규는 전부 `server/`**: 계정/세션·persist·cache·economy·backup을 새 배포 단위로 만들고, 크래시·중복로그인·동시소유·네트워크재시도·샤딩·GDPR까지 exhaustive하게 방어하며 PITR·이중부기 원장·append-only 감사로 라이브 운영(수백~수천 동접)의 롤백·정산·복구를 보장한다.
