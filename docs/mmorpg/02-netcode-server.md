# 02. 넷코드 & 권위 서버 아키텍처 (Netcode & Authoritative Server)

> 픽셀 2.5D MMORPG의 **서버 권위(server-authoritative) 시뮬레이션**과 **클라이언트 넷코드**를 정의한다.
> 목표 스케일: **존(zone)당 수백~수천 동접**, 왕복 지연(RTT) 30~250ms, 패킷 손실 0~5%, 라이브 운영(24/7).
> 설계 제1원칙(확장성)에 따라, 서버는 **헤드리스 `mye_scene` ECS를 그대로 공유**하고, 넷코드는 새 `engine/net`
> 서브시스템 + `server/` 실행 스택으로 붙인다. 클라이언트 예측/서버 재조정/지연보상/AoI/복제/전송/치트방지의
> 모든 경우의 수를 다룬다.
> 관련: [00-overview](../00-overview.md) · [01-core-platform](../01-core-platform.md) · [03-scene-world](../03-scene-world.md) ·
> [mmorpg/01-server-topology](01-server-topology.md) · [mmorpg/03-replication-state](03-replication-state.md) ·
> [mmorpg/04-gameplay-combat](04-gameplay-combat.md) · [mmorpg/09-liveops-anticheat](09-liveops-anticheat.md)

---

## 1. 목표 · 범위

### 1.1 이 문서가 다루는 것

| 축 | 범위 |
|---|---|
| **권위 서버 루프** | 헤드리스 고정 60Hz(설정) 틱, 존별 `World`, 결정론 시뮬, 입력 커맨드 소비, 스냅샷 생성 |
| **클라 예측 + 재조정** | client-side prediction(CSP), server reconciliation, input command frames, replay-on-correction |
| **지연보상(lag compensation)** | 서버 히스토리 버퍼 rewind, 히트스캔/투사체 판정 되감기, 이동 보간 |
| **관심영역관리(AoI)** | 그리드/셀 기반 interest management, relevance 필터, 구독/구독해제, 하이스테리시스 |
| **존·샤드·채널·인스턴스** | 필드 존, 던전 인스턴스, 채널(혼잡 분산), 크로스존 핸드오프 |
| **전송계층** | 신뢰성 UDP(자체 채널) 1차, WebSocket(웹클라)·KCP·ENet 백엔드 추상화, TCP 폴백 |
| **직렬화·프로토콜** | 비트패킹, 스냅샷+델타 압축, 양자화(quantization), 프로토콜 버전 협상 |
| **엔티티 복제** | 서버→클라 상태 복제, 권한(authority), 관련성(relevance), 네트워크 안정 ID |
| **시간·틱 동기** | 서버 권위 시계, RTT 추정, clock offset 보정, 틱 정렬, 롤백(선택) |
| **재접속·세션** | 세션 복구, 이중 로그인 처리, 유예(grace) 재접속, 상태 재동기화(full resync) |
| **치트방지(서버측)** | 입력 검증, 속도/좌표 sanity, 쿨다운/자원 서버 소유, 스냅샷 신뢰 경계 |
| **부하/봇 테스트** | 헤드리스 봇 클라이언트, 부하 프로파일, 틱 예산 회귀 |

### 1.2 이 문서가 다루지 **않는** 것 (타 도메인 위임)

- 서버 프로세스 토폴로지(게이트웨이/로그인/월드/채팅 분리·오케스트레이션) → [01-server-topology](01-server-topology.md) (본 문서는 넷코드 관점만)
- 복제 스키마의 **게임플레이 필드 정의**(어떤 컴포넌트를 복제하는가) → [03-replication-state](03-replication-state.md)
- 전투/스탯/스킬 **규칙** → [04-gameplay-combat](04-gameplay-combat.md) (본 문서는 판정 타이밍/rewind만)
- 계정 DB·영속화 → [05-persistence-db](05-persistence-db.md)
- 채팅/소셜 서버 → [06-social-chat](06-social-chat.md)
- 라이브 운영·안티치트 정책·밴 → [09-liveops-anticheat](09-liveops-anticheat.md) (본 문서는 서버측 판정 훅만)

### 1.3 설계 원칙 (MyEngine 규약 계승)

1. **서버는 클라이언트가 아니다, 그러나 코드는 공유한다.** 서버는 `engine/scene`(ECS·물리·타일맵·A*)을 **헤드리스로 링크**한다. `engine/render`·`engine/rhi`·`engine/audio`·`engine/ui`·`engine/imgui`는 **링크하지 않는다**. 이로써 "서버 물리 = 클라 물리" 결정론이 공짜로 성립한다.
2. **예외 불사용.** 넷코드도 `Expected<T, Error>` + 로그. 소켓 오류·역직렬화 실패는 값으로 전파.
3. **UTF-8 전면.** 문자열 페이로드는 UTF-8 바이트. 채팅/이름은 길이 프리픽스 + 검증.
4. **모듈·서비스 게이트웨이 재사용.** 넷코드는 `IModule` 라이프사이클을 따르고, `NetClient`/`NetServer`는 `MYE_SERVICE`로 `EngineContext`에 등록된다.
5. **이벤트 버스가 공용 언어.** 서버 수신 입력·복제 도착·연결 상태 변화는 `EventBus`로 발행(스레드 세이프 `Enqueue`, 메인/틱 스레드에서 `Flush`).
6. **결정론 = 안티치트·재조정의 전제.** 고정 스텝(`TimeStep::fixedStepIndex`), 정수 시드 RNG, 부동소수 순서 고정. 하이브리드 깊이 정렬은 렌더 전용이라 서버에 무관.

---

## 2. 핵심 개념 · 아키텍처

### 2.1 권위 모델 — 서버 권위 + 클라 예측 (AAA 표준)

```
클라이언트                                        서버(존 월드)
────────────                                     ──────────────
[t] 입력 샘플(01 InputState)                       매 fixed step S:
  └ InputCommand{seq, dt, move, buttons} 기록        1. 수신 InputCommand 큐 드레인(seq 순, 검증)
  └ 로컬 예측: Play World에 즉시 적용(03 이동)         2. mye_scene 시스템 실행(move&slide·전투·AI)
  └ 링버퍼에 (seq, 입력, 예측결과) 보관                3. AoI 갱신 → 관련 관찰자별 스냅샷 델타 생성
  └ 서버로 전송(신뢰성 없는 채널, 최근 N개 재전송)         4. lastProcessedSeq(플레이어별) 스냅샷에 포함
                                                     5. 브로드캐스트(관련 클라에게만)
[t+RTT] 서버 스냅샷 도착
  └ authoritative 상태 + ackedSeq 추출
  └ 재조정: ackedSeq 이후 로컬 입력을 재적용(replay)
  └ 원격 엔티티: 보간 버퍼(interpolation buffer)에 push
```

- **로컬 플레이어**: 예측(prediction) — 입력 즉시 반영, 서버 스냅샷과 불일치 시 재조정(reconcile)해 replay.
- **원격 플레이어/몹**: 보간(interpolation) — 100~200ms 지연 버퍼로 부드럽게, 예측 안 함(또는 등속 외삽 extrapolation은 짧게만).
- **서버**: 유일한 진실(single source of truth). 클라 입력을 **명령으로만** 신뢰하고, **상태는 절대 신뢰하지 않는다**.

### 2.2 계층 다이어그램 (클라 · 서버)

```
클라이언트 프로세스 (apps/game — 06-game-runtime)          서버 프로세스 (server/world_server)
┌───────────────────────────────────────────┐          ┌────────────────────────────────────────┐
│ L5 GameApp (Application, 01)                │          │ L5 WorldServerApp (Application --headless)│
│ ├ PredictionSystem (engine/net client)      │          │ ├ ZoneManager (server/) : World* per zone │
│ ├ ReconcileSystem                           │          │ ├ AoiSystem (engine/net)                  │
│ ├ InterpolationSystem (원격 엔티티)          │          │ ├ ReplicationSystem (스냅샷/델타)          │
│ ├ NetClient (MYE_SERVICE)                   │          │ ├ NetServer (MYE_SERVICE)                 │
│ ├ SnapshotDecoder / DeltaApply              │◄───UDP──►│ ├ SnapshotEncoder / DeltaGen              │
│ └ ClientClock (RTT/offset)                  │  (신뢰성  │ └ ServerClock (권위 tick)                  │
│                                             │   채널)   │                                          │
│ engine/scene (Play World: 예측 반영)         │          │ engine/scene (권위 World: 헤드리스)        │
│ engine/render·ui·audio (표현)                │          │ (render/ui/audio 미링크)                   │
│ engine/core (loop·jobs·events·config)        │          │ engine/core (loop·jobs·events·config)      │
└───────────────────────────────────────────┘          └────────────────────────────────────────┘
                    engine/net (공유): Transport · Protocol · BitStream · Channel · ConnectionState
```

`engine/net`은 **클라/서버 공용 코드**(전송·직렬화·프로토콜·연결 상태기계)를 담고, 클라 전용(예측·재조정·보간)과 서버 전용(AoI·복제·존)은 각각 소비 측(`apps/game`, `server/`)이 시스템으로 배선한다.

### 2.3 네트워크 안정 엔티티 ID (NetId)

문제: `ecs::Entity`는 `index:32 | generation:32`인데 **프로세스 로컬**이다. 서버의 `index`와 클라의 `index`는 무관하며, 클라가 서버 엔티티를 참조할 안정 키가 없다.

해결: **NetId** — 서버가 부여하는 전역(존 내) 안정 64비트 ID. 서버는 `NetId → 서버 Entity` 맵을, 클라는 `NetId → 로컬 Entity` 맵을 유지한다.

```cpp
// engine/net/include/mye/net/NetId.h
namespace mye::net {
// 상위 16비트 = zoneId(핸드오프 시 재발급 회피), 하위 48비트 = 존 내 단조증가 시퀀스.
struct NetId {
    uint64_t value = 0;
    static constexpr NetId Null() { return {0}; }
    bool IsNull() const { return value == 0; }
    uint16_t Zone() const { return static_cast<uint16_t>(value >> 48); }
    auto operator<=>(const NetId&) const = default;
};
// 서버측 발급기: 존별 시퀀스. 재사용 금지(48비트면 존당 실질 무한).
class NetIdAllocator { public: NetId Next(uint16_t zoneId); /* ... */ };
}
```

`engine/scene`은 손대지 않는다. 대신 서버·클라 둘 다 `NetIdComponent { NetId id; }`(신규, 복제 대상 엔티티에만 부착)를 `mye_scene` 런타임 컴포넌트로 등록한다. 매핑은 넷 계층이 `unordered_map<NetId, Entity>`로 소유(`std::hash<Entity>` 이미 존재).

### 2.4 틱 · 시간 모델

- 서버 존 루프는 **01 메인 루프의 고정 스텝**을 그대로 쓴다(`TimeSystem`, `AdvanceFixedStep`, `TimeStep::fixedStepIndex`). 렌더 프레임 개념 없음 — `--headless`로 창/렌더 없이 fixed step만 회전.
- **서버 tick = `fixedStepIndex`.** 스냅샷·입력·rewind가 이 정수 tick을 기준으로 정렬된다(부동소수 시간 금지).
- 클라는 **ServerClock offset**을 추정해 "서버가 지금 몇 tick인지"를 예측하고, 입력 커맨드에 예상 도착 tick을 붙일 수 있다(선택; 기본은 seq 기반 ack).
- 스냅샷 전송률(snapshot rate)은 tick률과 분리: 예) 서버 60Hz 시뮬 + 20Hz 스냅샷 송신(대역 절약). 클라 보간이 이 간격을 메운다.

---

## 3. 기능 목록

우선순위: **P0**=수직슬라이스 최소(한 존·소수 인원 실동작) / **P1**=베타(수백 동접·재접속) / **P2**=라이브(수천·채널·핸드오프) / **P3**=스케일·최적화 / **P4**=선택·연구.
상태: **있음**=기존 엔진 자산 재사용 / **부분**=일부 프리미티브 존재, 넷 배선 필요 / **신규**=새 코드.

| 기능 | 우선 | 상태 | 엔진 매핑 (추가/재사용 위치) |
|---|---|---|---|
| 헤드리스 존 서버 루프(고정 tick) | P0 | 부분 | 재사용: `engine/core` 메인 루프·`TimeSystem`·`--headless`(App.cpp). 신규: `server/world_server` Application |
| ECS/물리/타일맵 서버 공유(헤드리스) | P0 | 있음 | 재사용: `engine/scene`(World·SystemScheduler·PhysicsWorld2D·Tilemap·NavSystem) 링크. render/rhi/ui/audio 미링크 |
| 전송 추상화 `ITransport`(UDP 1차) | P0 | 신규 | 신규 `engine/net/Transport.*`. 재사용: `JobSystem` IO 큐(소켓 IO 스레드), `Expected` 오류 |
| 신뢰성 채널(reliable/unreliable/ordered) | P0 | 신규 | 신규 `engine/net/Channel.*`(ack/재전송/시퀀스). ENet 대체 가능 |
| BitStream 직렬화·양자화·비트패킹 | P0 | 신규 | 신규 `engine/net/BitStream.*`. 재사용: `Vec2`·`Math`(양자화 헬퍼) |
| 프로토콜 버전 협상 + 메시지 프레이밍 | P0 | 신규 | 신규 `engine/net/Protocol.*`. 재사용: `HashFnv1a64`(메시지 타입 ID) |
| NetId 안정 엔티티 ID + 매핑 | P0 | 신규 | 신규 `engine/net/NetId.*` + `NetIdComponent`(mye_scene 런타임 등록) |
| 입력 커맨드 프레임 캡처·전송 | P0 | 부분 | 재사용: `01 InputState` 에지 쿼리. 신규: `net/InputCommand`, 링버퍼 |
| 클라 예측(CSP) | P0 | 부분 | 재사용: `03 KinematicBody2D` move&slide를 Play World에 적용. 신규: `PredictionSystem`(client) |
| 서버 재조정(reconciliation·replay) | P0 | 신규 | 신규 client `ReconcileSystem`. 재사용: 03 이동 시스템 재실행, ECS 스냅샷 복원 |
| 스냅샷 생성(권위 상태 직렬화) | P0 | 신규 | 신규 server `SnapshotEncoder`. 재사용: `reflect`(복제 필드 순회 후보) |
| 원격 엔티티 보간 버퍼 | P0 | 신규 | 신규 client `InterpolationSystem`. 재사용: `Math` lerp, `Transform` |
| 연결 상태기계(handshake/timeout/close) | P0 | 신규 | 신규 `engine/net/Connection.*`. 재사용: `EventBus`(상태 이벤트) |
| AoI/관심영역(그리드 셀) | P1 | 부분 | 재사용: `phys::SpatialHash`(그리드 브로드페이즈 → AoI 셀로 재사용). 신규 `AoiSystem` |
| 관련성(relevance) 필터·구독 관리 | P1 | 신규 | 신규 server `ReplicationSystem`(관찰자별 대상집합·enter/leave) |
| 델타 압축(baseline 대비 변경분) | P1 | 신규 | 신규 `net/DeltaCodec`. 재사용: BitStream, 컴포넌트 dirty 추적(03 갭 보완) |
| ack/재전송/혼잡·흐름 제어 | P1 | 신규 | 신규 `net/Channel`·`CongestionControl`(RTT·손실 기반 송신율) |
| RTT/clock offset 추정·시간동기 | P1 | 부분 | 재사용: `Clock`(QPC ns). 신규 `net/ClientClock`·`ServerClock` |
| 재접속·세션 복구(grace·full resync) | P1 | 신규 | 신규 `net/Session`·서버 세션 테이블. 재사용: `Config`(타임아웃) |
| 이중 로그인/세션 선점 처리 | P1 | 신규 | 신규 세션 정책(신규 접속이 기존 킥 or 거부). [05-persistence](05-persistence-db.md) 연계 |
| 서버측 이동 검증(속도/좌표 sanity) | P1 | 신규 | 신규 server `MovementValidation`. 재사용: 03 타일 walkability, move&slide 재판정 |
| 지연보상(rewind/히트 판정 되감기) | P2 | 신규 | 신규 server `LagCompensation`(엔티티 위치 히스토리 링버퍼). 재사용: `SpatialHash` 재구축 |
| 채널(혼잡 분산)·인스턴스(던전) | P2 | 신규 | 신규 `server/ZoneManager`가 동일 존을 다중 World 인스턴스로 |
| 크로스존 핸드오프(월드 이동) | P2 | 신규 | 신규 `server/Handoff`(상태 직렬화→대상 존 전달). 재사용: 씬 직렬화 개념(07) |
| 스냅샷 우선순위·대역 예산(priority) | P2 | 신규 | 신규 `net/PriorityAccumulator`(거리·중요도 가중, 프레임 바이트 예산) |
| 부하/봇 테스트 하니스 | P2 | 신규 | 신규 `tools/loadtest`(헤드리스 봇 클라, N개 커넥션). 재사용: `net`, `--headless` |
| WebSocket/KCP/ENet 백엔드 | P3 | 신규 | `ITransport` 구현 추가(웹클라·모바일). 백엔드 인터페이스화(00 원칙) |
| 존 간 로드밸런싱·수평확장 | P3 | 신규 | [01-server-topology](01-server-topology.md) 소유. 본 문서는 핸드오프 프로토콜만 |
| 롤백(rollback) 결정론 재시뮬 | P4 | 신규 | 선택(격투/PvP 인스턴스 한정). `fixedStepIndex` 기반 상태 스냅샷 저장 |
| 스냅샷 압축(zstd/lz4) 스트림 | P4 | 신규 | 델타 후단 엔트로피 압축(대형 존 초기 스냅샷). `third_party` 추가 |

---

## 4. 데이터 모델 · 스키마

### 4.1 입력 커맨드 프레임 (클라 → 서버)

클라는 매 fixed step 입력을 **결정론적 명령**으로 캡처한다(`01 InputState` 스냅샷을 넷 커맨드로 압축).

```cpp
// engine/net/include/mye/net/InputCommand.h
namespace mye::net {
struct InputCommand {
    uint32_t seq;          // 클라 단조증가 시퀀스(ack 기준)
    uint32_t clientTick;   // 클라가 이 명령을 적용한 fixedStepIndex
    float    dtMs;         // 이 명령이 대표하는 시간(가변 프레임 병합 시). 서버는 클램프.
    int8_t   moveX, moveY; // 정규화 이동 입력 -127..127 (스틱/키). 서버가 다시 정규화.
    uint16_t buttons;      // 비트마스크: attack/jump/interact/skill1..N (서버 정의)
    uint16_t aimAngleQ;    // 조준각 양자화(0..65535 = 0..2π). 픽셀 2.5D 8방향+정밀조준
    // 확장: targetNetId(스킬 타깃), sequence of pending seqs (재전송 묶음)
};
// 한 패킷에 최근 미ack 명령 여러 개를 묶어 보냄(손실 복원용, unreliable 채널).
struct InputPacket { uint32_t baseSeq; uint8_t count; InputCommand cmds[]; };
}
```

**규약**: 서버는 `seq`가 이미 처리됐거나(중복) 미래 tick으로 과도하게 앞선 명령을 폐기한다. `dtMs`는 `[0, maxFrameMs]`로 클램프(속도 핵 방지). `moveX/Y`는 서버에서 재정규화(magnitude>1 방지).

### 4.2 엔티티 스냅샷 / 델타 (서버 → 클라)

```cpp
// engine/net/include/mye/net/Snapshot.h
namespace mye::net {
struct SnapshotHeader {
    uint32_t serverTick;      // 이 스냅샷의 fixedStepIndex
    uint32_t baselineTick;    // 델타 기준(0 = full snapshot)
    uint32_t ackedInputSeq;   // 수신처 플레이어의 마지막 처리 입력 seq (재조정 기준)
    uint16_t entityCount;     // 이 스냅샷에 포함된 엔티티 수
    uint8_t  flags;           // bit0=full, bit1=compressed, bit2=hasEvents
};
// 엔티티당 델타: 변경된 필드만 비트마스크로.
struct EntitySnapshot {
    NetId    id;
    uint8_t  changeMask;      // bit0=pos bit1=vel bit2=anim bit3=health bit4=facing ...
    // 이하 changeMask에 따라 가변: 위치(양자화 Vec2), 속도, animState(u8), hp(u16) 등
};
}
```

**양자화(quantization)** — 픽셀 2.5D는 PPU 48. 위치는 서브픽셀 정밀이면 충분:

| 필드 | 표현 | 비트 |
|---|---|---|
| 위치 X/Y | 존 로컬 고정소수(1/16 px 단위, ±월드범위) | 축당 ~20~24bit |
| 속도 | 옵션(예측용, 저정밀) | 축당 ~10bit |
| 조준각 | 0..2π → u16 | 16bit |
| animState | 상태머신 상태 인덱스(03 SpriteAnimator) | 6~8bit |
| facing(Dir8) | 03 `Dir8` enum | 3bit |
| health/mana | 게임 정의(04) 스탯 스냅 | 게임 정의 |

### 4.3 패킷 프레임 · 채널

```cpp
// engine/net/include/mye/net/Protocol.h
namespace mye::net {
enum class ChannelId : uint8_t {
    Control    = 0,  // handshake/ping/disconnect (신뢰성·순서보장)
    Input      = 1,  // 클라 입력 (unreliable, 최근값 재전송으로 복원)
    Snapshot   = 2,  // 서버 스냅샷 (unreliable-sequenced, 최신만 유효)
    Reliable   = 3,  // 채팅·인벤·거래·씬전환 (신뢰성·순서보장)
    Bulk       = 4,  // 대형(초기 존 스냅샷·에셋 매니페스트) 조각화 전송
};
enum class MessageType : uint16_t {  // FNV 대신 컴팩트 열거(대역)
    ConnectRequest, ConnectAccept, ConnectDeny, Disconnect,
    Ping, Pong, InputBatch, Snapshot, ReliableEvent, Ack, ResyncRequest, ...
};
struct PacketHeader {
    uint16_t protocolId;   // 프로토콜 시그니처(버전+게임). 불일치 → 즉시 폐기(포트스캔·구버전 차단)
    uint16_t sequence;     // 패킷 시퀀스(채널별)
    uint16_t ack;          // 마지막 수신 시퀀스
    uint32_t ackBits;      // 이전 32개 수신 비트필드(선택적 ack)
    // 이하 채널 프레임들(가변). MTU(1200B 안전값) 초과 시 Bulk 채널에서 조각화.
};
}
```

### 4.4 연결 상태기계

```cpp
// engine/net/include/mye/net/Connection.h
enum class ConnState : uint8_t {
    Disconnected, Connecting, Handshaking, Connected, Reconnecting, Disconnecting, Timeout
};
struct ConnectionStateEvent { MYE_EVENT(ConnectionStateEvent); ConnState prev, next; uint64_t connId; };
```

**타임아웃 규약**: `Connected`에서 마지막 수신 후 `netTimeoutMs`(기본 10s, `Config` `net.timeoutMs`) 무패킷 → `Timeout`. Keep-alive는 `Ping/Pong`(1s 간격)으로 유지. `Reconnecting`은 유예 창(grace window) 동안 세션 상태 보존.

### 4.5 서버 세션 테이블

```cpp
// server/include/mye/server/Session.h
struct Session {
    uint64_t   connId;         // 전송 연결 ID (재접속 시 갱신)
    uint64_t   accountId;      // 05-persistence 계정
    NetId      avatar;         // 이 세션이 조종하는 아바타 엔티티
    uint16_t   zoneId, channel;
    uint32_t   lastInputSeq;   // 마지막 처리 입력(재조정 ack)
    uint32_t   lastAckTick;    // 클라가 확인한 스냅샷 tick(델타 baseline)
    double     graceUntil;     // 재접속 유예 만료(끊겼을 때 realTime)
    // AoI: 이 세션이 현재 구독 중인 NetId 집합(enter/leave diff용)
    std::unordered_set<NetId> subscribed;
};
```

---

## 5. 경우의 수 · 엣지케이스 (exhaustive)

> "실패·악용·스케일·동시성·네트워크지연" 관점에서 발생 가능한 모든 상황과 처리 규약. 넷코드의 본질은 여기에 있다.

### 5.1 네트워크 지연 · 손실 · 순서 뒤바뀜

| 상황 | 처리 |
|---|---|
| **입력 패킷 손실** | unreliable 채널이지만 각 패킷에 최근 N개(예 6) 명령을 중복 포함 → 다음 패킷이 도착하면 gap 복원. 완전 손실 창은 서버가 마지막 입력을 등속 반복(coast) 또는 정지(설계 선택; 이동은 반복, 공격은 무시). |
| **스냅샷 패킷 손실** | Snapshot 채널은 unreliable-sequenced: 오래된 스냅샷은 폐기, 최신만 적용. 델타 baseline이 손실되면 클라가 `ResyncRequest`로 full snapshot 요청. |
| **패킷 순서 뒤바뀜(reorder)** | 헤더 `sequence`로 오래된 것 폐기. 입력은 `seq`로 정렬 후 처리(과거 seq는 이미 처리됨 → 무시). |
| **중복 패킷(duplicate)** | ack 비트필드/처리된 seq 집합으로 idempotent 처리. 입력 재적용 금지. |
| **높은 RTT(250ms+)** | 예측 창이 커짐 → 재조정 replay 비용↑. 보간 지연을 RTT 적응(interpolation delay = f(RTT jitter)). UI에 "높은 지연" 표시. |
| **RTT 지터(jitter) 급변** | 보간 버퍼 크기를 적응적으로(지터 EWMA). 버퍼 언더런 시 짧은 외삽(extrapolation), 초과 시 소폭 타임스케일 조정(스냅 방지). |
| **비대칭 지연(up≫down 등)** | RTT 추정은 왕복이지만, 입력 지연과 스냅샷 지연을 분리 추정(원웨이 추정은 clock offset 필요, §5.7). |
| **MTU 초과 페이로드** | 1200B 안전 MTU. 초과 스냅샷은 우선순위로 잘라 다음 tick으로 이월(§5.4) 또는 Bulk 채널 조각화. IP 단편화 회피. |
| **패킷 폭주(버스트 손실)** | 혼잡 제어가 송신율 하향(스냅샷 rate·priority 예산 축소). 손실률 임계 초과 시 저품질 모드(20Hz→10Hz 스냅샷). |

### 5.2 예측 · 재조정 오류

| 상황 | 처리 |
|---|---|
| **예측 불일치(misprediction)** | 서버 스냅샷 위치와 클라 예측 위치 차 > 임계 → 재조정: authoritative로 스냅한 뒤 ackedSeq 이후 입력을 replay. 시각적 스냅을 줄이려 **에러 스무딩**(수 프레임에 걸쳐 보정 오프셋 감쇠). |
| **재조정 replay 중 결정론 깨짐** | 서버·클라 이동 시스템이 동일 코드(03) + 동일 고정 dt여야 함. 부동소수 순서·컴파일러 플래그 고정. 불일치 지속 시 강제 full resync. |
| **로컬 충돌 vs 서버 충돌 차이** | 클라는 타일 collision(03 `ITileCollision` 구현 필요, 03 갭)을 서버와 동일하게. 서버 판정이 최종. 예측이 벽을 통과했으면 재조정이 되돌림. |
| **예측 불가 행동(투사체 발사·스킬)** | 발사 자체는 예측(즉시 이펙트), 명중/데미지는 **서버 확정 대기**(예측 명중은 "임시" 표시 후 서버 결과로 확정/취소). |
| **입력 seq 랩어라운드(u32)** | 실사용 오래 안 걸리나, 비교는 순환 비교(`(int32_t)(a-b) > 0`)로 안전. |
| **클라가 예측 상태를 조작(치트)** | 무의미 — 서버는 클라 상태를 신뢰 안 함. 클라 예측이 어긋나면 재조정이 강제로 서버 값으로. |

### 5.3 관심영역(AoI) · 관련성

| 상황 | 처리 |
|---|---|
| **엔티티 시야 진입(enter)** | 관찰자 AoI 셀 집합에 새 NetId 등장 → 서버가 **spawn 메시지**(full 초기 상태) 전송, 세션 `subscribed`에 추가. |
| **엔티티 시야 이탈(leave)** | AoI에서 벗어남 → **despawn 메시지**, `subscribed`에서 제거. 클라는 로컬 엔티티 파괴. |
| **경계 왕복(border flicker)** | enter/leave가 매 tick 토글되는 것 방지 → **하이스테리시스**: enter 반경 < leave 반경(예 enter 12타일, leave 15타일). |
| **밀집 군집(수백 엔티티가 한 셀)** | 우선순위 예산(§5.4)으로 가까운/중요한 것 우선, 나머지는 저빈도 갱신. 하드 상한(관찰자당 최대 N 복제). |
| **순간이동/스킬 블링크** | 큰 위치 점프 → enter/leave 즉시 재평가(다음 AoI tick 대기 금지). 위치 스냅은 보간 안 함(텔레포트 플래그). |
| **투명/은신 유닛** | AoI relevance에 게임 규칙(가시성) 훅. 서버가 관찰자별로 relevance=false 처리(스텔스는 복제 자체 안 함 → 월핵 방지). |
| **큰 오브젝트(보스·탈것)** | AABB가 여러 셀에 걸침 → 다중 셀 삽입(`SpatialHash`가 이미 지원: multi-cell insert). |
| **AoI 셀 크기 튜닝** | 타일맵 청크(32×32) 정렬. 셀 너무 작으면 경계 처리 폭증, 크면 복제 과다. `Config` `net.aoiCellTiles`. |

### 5.4 스케일 · 대역 예산

| 상황 | 처리 |
|---|---|
| **관찰자당 복제 대상 폭증** | `PriorityAccumulator`: 대상별 우선도(거리·최근 갱신 경과·중요도) 누적, 프레임 바이트 예산 내에서 상위만 송신. 나머지는 다음 tick 누적↑으로 결국 송신(기아 방지). |
| **존당 엔티티 수천** | 스냅샷 생성이 O(관찰자×대상)이 되지 않게: AoI로 대상 사전 축소, `JobSystem::ParallelFor`로 관찰자별 병렬 인코딩(01 잡 시스템). |
| **틱 예산 초과(over-budget tick)** | 서버 tick이 16.6ms 초과 → spiral-of-death 방지(01 메인 루프 clamp 재사용). 초과 지속 시 채널 분리·인스턴스 분할·존 이전 권고. |
| **대량 동시 스폰(레이드·이벤트)** | 스폰 스냅샷을 우선순위로 스로틀, Bulk 채널로 초기 상태 스트리밍. |
| **광역 스킬로 다수 동시 데미지** | 데미지 이벤트는 Reliable 채널로 배치(묶음) 전송, 개별 스냅샷 남발 회피. |
| **대역 상한 초과 클라(모바일·저속)** | 클라가 협상 시 대역 등급 신고 → 서버가 저빈도 스냅샷·저정밀 양자화 적용(적응형). |

### 5.5 존 · 인스턴스 · 핸드오프

| 상황 | 처리 |
|---|---|
| **필드↔던전 진입** | 던전 = 존의 새 World **인스턴스**(`ZoneManager`가 생성). 진입 = 현재 존에서 despawn → 대상 인스턴스 스폰(§5.9 세션 이전). |
| **채널 이동(혼잡 분산)** | 같은 존의 다른 World 인스턴스(채널). 상태 이전은 캐릭터 영속 데이터(05)만, 존 로컬 상태는 버림. |
| **크로스존 이동(월드 경계)** | `Handoff`: 출발 존이 아바타 상태(위치·스탯·인벤 스냅) 직렬화 → 대상 존으로 전달(또는 05 DB 경유) → 대상 존이 스폰 → 클라에 새 존 접속 지시. 원자성: 대상 스폰 성공 확인 후 출발 despawn(양쪽 존재/유령 방지). |
| **핸드오프 중 연결 끊김** | 세션이 "in-transit" 상태 → 재접속 시 마지막 확정 존으로 복구(§5.9). 이중 스폰 방지 토큰. |
| **인스턴스 정리(빈 던전)** | 마지막 세션 leave 후 유예(reset 창) → World 파괴, 리소스 회수. 진행 중 재접속 유예와 조율. |
| **존 서버 크래시** | 세션은 게이트웨이가 재라우팅([01-server-topology](01-server-topology.md)). 마지막 DB 체크포인트 이후 상태 손실 최소화(주기 저장, 05). |

### 5.6 재접속 · 세션 · 이중 로그인

| 상황 | 처리 |
|---|---|
| **일시 끊김 후 재접속(grace 내)** | 세션 유예 창 동안 아바타·상태 보존. 재접속 시 새 `connId` 바인딩 + **full resync**(현재 존 스냅샷 전량) + 마지막 lastAckTick 리셋. |
| **grace 만료 후 접속** | 정상 신규 로그인 경로(로그인 서버·존 배정). 캐릭터는 마지막 저장 상태(05). |
| **이중 로그인(같은 계정 두 번)** | 정책: 신규 접속이 기존 세션 강제 종료("다른 위치 로그인") — 기본. 또는 신규 거부. `Config` `net.duplicateLogin=kick|reject`. 원자적 세션 교체(레이스 방지 락). |
| **좀비 세션(끊겼는데 서버가 모름)** | keep-alive 타임아웃으로 정리. 그 전에 재접속 오면 좀비 대체. |
| **재접속 폭풍(서버 재시작 후)** | 로그인 서버 레이트리밋·큐잉([01](01-server-topology.md)). 존 서버는 스폰을 스로틀. |
| **클라 상태 불일치(구버전)** | `ResyncRequest`/버전 불일치 → full resync 또는 강제 재접속·클라 업데이트 안내. |
| **입력 ack 유실로 무한 재조정** | ackedInputSeq는 스냅샷마다 포함 → 스냅샷만 오면 자동 진전. 스냅샷도 끊기면 keep-alive→타임아웃. |

### 5.7 시간 · 틱 · 동기

| 상황 | 처리 |
|---|---|
| **클라-서버 clock offset** | 클라가 `Ping`에 로컬 send time 포함, `Pong`에 서버 tick·시각 반영 → RTT/2로 offset 추정(EWMA). `net/ClientClock`이 "서버 현재 tick" 예측. |
| **서버 tick 드리프트** | 서버는 QPC 고정 스텝(01)이라 드리프트 없음. accumulator가 프레임 지연을 흡수(spiral clamp). |
| **클라 프레임레이트 가변(144/60/30)** | 입력은 고정 스텝으로 캡처(01 FixedUpdate)해 프레임레이트 독립. 렌더는 보간(01 alpha). 저프레임 클라도 동일 tick 수 입력 생성. |
| **일시정지·백그라운드(창 최소화)** | 클라 일시정지 시 입력 없음 → 서버가 coast/정지. 복귀 시 큰 offset 재추정 + resync. |
| **롤백 필요(PvP 인스턴스, 선택)** | `fixedStepIndex`별 월드 상태 스냅 저장 → 늦게 도착한 입력에 대해 재시뮬(결정론 전제). 비용 큼 → PvP 소인원 인스턴스 한정(P4). |

### 5.8 치트 · 악용 (서버측 방어)

| 상황 | 처리 |
|---|---|
| **속도 핵(speed hack)** | 서버가 dt 클램프 + 입력 tick 간격 검증. 이동은 서버 move&slide로 재계산(클라 위치 무시). 누적 이동 거리/시간 비율 sanity. |
| **텔레포트/좌표 조작** | 클라는 **위치를 보내지 않는다**(입력만). 서버가 위치 소유. 순간이동은 서버 스킬로만. |
| **월핵(벽 통과)** | 서버 타일 collision(03) 최종 판정. 통과 감지 시 스냅백. |
| **월핵(정보 노출, 시야 밖 적)** | AoI relevance로 애초에 복제 안 함(스텔스·시야 밖 = 클라에 데이터 없음). |
| **공격 속도·쿨다운 핵** | 쿨다운·자원(마나·화살) **서버 소유**. 클라 요청은 검증 후만. |
| **패킷 리플레이/위조** | protocolId·시퀀스·연결 토큰(핸드셰이크 시 발급 nonce) 검증. HMAC 서명(선택, [09](09-liveops-anticheat.md)). |
| **입력 스팸(DoS)** | 연결당 입력 레이트리밋. 과도 시 폐기·경고·차단. |
| **미검증 필드 오버플로** | 모든 역직렬화는 길이·범위 검증(BitStream 경계 체크). 실패 → 연결 종료(신뢰 경계). |
| **에임봇·매크로** | 서버측 통계 이상 탐지 훅([09](09-liveops-anticheat.md))으로 위임. 넷 계층은 원시 입력 로깅 제공. |

### 5.9 동시성 · 서버 내부

| 상황 | 처리 |
|---|---|
| **소켓 IO 스레드 vs 틱 스레드** | 수신 패킷은 IO 스레드가 파싱해 `EventBus::Enqueue`(스레드 세이프, 01)로 넣고, 틱 스레드가 `Flush`로 소비. 또는 lock-free MPSC 큐. World는 틱 스레드 단독 소유(구조 변경은 CommandBuffer, 03). |
| **다중 존 병렬 틱** | 존 = 독립 World → 존별 잡으로 병렬(`JobSystem`). 존 간 상호작용은 메시지(핸드오프)로만, 공유 메모리 금지. |
| **크로스존 원자성** | 핸드오프는 2단계(prepare→commit). 실패 시 롤백(출발 존 유지). |
| **입력 큐 경합** | 세션별 입력 큐, 틱 시작에 드레인. 락 최소화(SPSC per 세션). |
| **스냅샷 인코딩 병렬화** | 관찰자별 독립 → `ParallelFor`. 단, 읽기 스냅샷은 tick 시작에 고정(불변 뷰)해야 데이터 레이스 없음. |
| **DB 접근(영속화)** | 틱 스레드가 블로킹 금지 → `JobSystem` IO 큐/비동기(05). 결과는 메인 스레드 마샬(`RunOnMainThread`). |

### 5.10 프로토콜 · 버전 · 운영

| 상황 | 처리 |
|---|---|
| **클라/서버 프로토콜 불일치** | 핸드셰이크에서 `protocolId`(버전 해시) 검증 → 불일치 시 `ConnectDeny(reason=version)` + 최소 클라 버전 안내. |
| **롤링 배포(서버만 갱신)** | 하위호환 프로토콜(필드 추가는 changeMask 확장, 제거는 예약). 메이저 변경은 강제 클라 업데이트. |
| **메시지 타입 미지원** | 알 수 없는 MessageType → 무시(로그) 또는 연결 종료(엄격 모드). 관대/엄격 `Config`. |
| **부하 테스트(봇)** | `tools/loadtest` 헤드리스 봇이 실제 프로토콜로 접속·랜덤 입력. 틱 예산·대역·메모리 회귀 측정. |
| **메트릭·관측성** | 연결 수·RTT 분포·손실률·tick time·대역/세션을 이벤트/카운터로. [09](09-liveops-anticheat.md)·[08-mcp](../08-mcp.md) 대시보드 연계. |

---

## 6. 신규 모듈 · 파일 제안

### 6.1 `engine/net` — 클라/서버 공용 넷 코어 (신규 서브시스템)

```
engine/net/
  CMakeLists.txt                      # mye_net static lib. 의존: mye_core (scene 미의존 — 순수 전송/직렬화)
  include/mye/net/
    NetId.h                           # 안정 엔티티 ID + Allocator
    BitStream.h                       # 비트 단위 read/write, 양자화 헬퍼(Vec2/각도/정수 압축)
    Protocol.h                        # PacketHeader, MessageType, ChannelId, protocolId
    Transport.h                       # ITransport 인터페이스(추상) + Endpoint
    UdpTransport.h                    # win32 UDP 소켓 백엔드(1차)
    Channel.h                         # 신뢰성/순서/시퀀스 채널(ack·재전송·중복제거)
    Connection.h                      # 연결 상태기계 + keep-alive + 타임아웃
    CongestionControl.h               # RTT/손실 기반 송신율(간이 AIMD)
    InputCommand.h                    # 입력 커맨드 프레임 + 링버퍼
    Snapshot.h                        # SnapshotHeader/EntitySnapshot + changeMask
    DeltaCodec.h                      # baseline 대비 델타 인/디코드
    NetClock.h                        # ClientClock/ServerClock(RTT·offset 추정)
    NetModule.h                       # IModule(01) — NetClient/NetServer 서비스 등록
  src/  ...                           # 위 각 .cpp
  tests/                              # BitStream 라운드트립·채널 ack/재전송·델타·양자화 오차·seq 랩 (자작 MYE_TEST)
```

- `ITransport`는 **백엔드 인터페이스화**(00 원칙): `UdpTransport`(1차), 이후 `WebSocketTransport`(웹클라)·`KcpTransport`·`EnetTransport` 추가 가능.
- `mye_net`은 **scene에 의존하지 않는다**(순수 바이트·전송). NetId↔Entity 매핑은 소비 측이 소유.
- 소켓 IO는 `JobSystem` IO 큐 또는 전용 스레드; 데이터는 `EventBus::Enqueue`로 틱 스레드에 전달.

### 6.2 `apps/game` — 데이터드리븐 게임 클라이언트 (신규, [06-game-runtime](06-game-runtime.md) 소유·본 문서 소비)

```
apps/game/                            # 프로젝트를 로드해 실행하는 exe (현재 부재 — 핵심 갭)
  src/net/
    PredictionSystem.cpp              # 로컬 입력 예측(Play World 즉시 적용, 링버퍼 보관)
    ReconcileSystem.cpp               # 서버 스냅샷 도착 시 재조정·replay·에러 스무딩
    InterpolationSystem.cpp           # 원격 엔티티 보간 버퍼 → Transform 갱신
    SnapshotClient.cpp                # 스냅샷 디코드·NetId→로컬 Entity 스폰/파괴
    ClientNet.cpp                     # NetClient 배선(연결·입력 송신·상태 이벤트)
```

> 클라 앱 자체의 부트스트랩·씬 로딩은 [06-game-runtime](06-game-runtime.md) 소유. 본 문서는 넷 시스템만 규정.

### 6.3 `server/` — 헤드리스 존 서버 스택 (신규 최상위 디렉터리)

```
server/                               # CMake add_subdirectory(server) 추가
  world_server/                       # 존 시뮬 서버 실행 파일(--headless Application)
    src/
      WorldServerApp.cpp              # Application(01) 진입, engine/scene 헤드리스 링크
      ZoneManager.cpp                 # 존별 World·인스턴스·채널 수명
      AoiSystem.cpp                   # SpatialHash 재사용 → 관찰자별 관련성
      ReplicationSystem.cpp           # 스냅샷/델타 생성·우선순위·전송
      MovementValidation.cpp          # 서버 이동 재판정(치트방지)
      LagCompensation.cpp             # 위치 히스토리 rewind(히트 판정)
      Handoff.cpp                     # 크로스존 상태 이전 프로토콜
      SessionManager.cpp              # 세션 테이블·재접속·이중 로그인
    include/mye/server/ ...
  CMakeLists.txt                      # mye_core + mye_scene(+ mye_net) 링크. render/rhi/ui/audio 미링크
```

- 게이트웨이/로그인/채팅 서버 프로세스 분리는 [01-server-topology](01-server-topology.md)에서 정의(본 문서는 `world_server`의 넷/시뮬만).
- 서버는 **동일 `mye_scene`** 링크로 클라와 물리·이동·A* 결정론 공유(코드 공유 트레이드오프의 승리 지점).

### 6.4 `tools/loadtest` — 부하/봇 테스트 (신규)

```
tools/loadtest/                       # 헤드리스 봇 클라이언트 N개, 실제 프로토콜로 접속
  src/main.cpp                        # mye_net 링크, 랜덤/스크립트 입력, RTT·tick·대역 측정
```

### 6.5 `engine/scene` 최소 확장 (신규 컴포넌트만, 기존 미변경)

- `NetIdComponent { NetId id; }` — 복제 대상 엔티티에 부착(런타임 컴포넌트 등록, `World::RegisterComponent`).
- (03 갭 보완 연계) **컴포넌트 dirty 추적** — 복제 델타의 전제. 03 문서의 "변경 추적/dirty-events" 갭을 델타 코덱이 요구하므로, 03에 `ChangeTracker`(선택) 훅 제안. 본 문서는 소비자.
- (03 갭 보완 연계) **`ITileCollision` 구현** — 서버 이동 검증·클라 예측 일치의 전제(03 갭). 서버·클라 공유.

---

## 7. 마일스톤 (작은 검증 단위)

| 단계 | 산출물 | 검증(테스트/데모) |
|---|---|---|
| **N0 — 전송·직렬화 기반** | `engine/net`: `BitStream`, `UdpTransport`, `Protocol`, `Connection`(handshake/timeout) | `mye_net` 유닛테스트: BitStream 라운드트립·양자화 오차 한계·seq 랩·채널 ack/재전송/중복제거. 로컬 루프백 ping/pong RTT. |
| **N1 — 헤드리스 존 루프** | `server/world_server`: `Application --headless` + `mye_scene` 링크 + fixed tick 회전 | 봇 1개 접속 → 서버가 tick 진행·keep-alive 유지. `--frames`로 결정론 덤프 회귀(에디터 CI 패턴 재사용). |
| **N2 — 입력→권위 이동→스냅샷** | 클라 입력 커맨드 송신, 서버 move&slide 적용, 단일 엔티티 스냅샷 브로드캐스트 | 봇 클라가 입력 → 서버 위치 갱신 → 스냅샷 수신. 위치 일치(양자화 오차 내). |
| **N3 — 예측 + 재조정** | 클라 `PredictionSystem`·`ReconcileSystem`, 로컬 즉시 이동 + 서버 ack replay | 인위 지연(200ms)·손실(3%) 주입 시 스냅 없이 부드러운 이동. 벽 예측 오류 → 재조정 스냅백 확인. |
| **N4 — 원격 보간 + 다중 클라** | `InterpolationSystem`, 여러 봇 상호 관찰 | 2~10 봇이 서로 보간으로 부드럽게 보임. 지터 적응 버퍼 검증. |
| **N5 — AoI + 관련성** | `AoiSystem`(SpatialHash 재사용), enter/leave·하이스테리시스, 세션 `subscribed` | 넓은 존에서 시야 밖 엔티티 미복제. 경계 왕복 시 flicker 없음. |
| **N6 — 델타 압축 + 우선순위 예산** | `DeltaCodec`, baseline ack, `PriorityAccumulator`, 대역 상한 | 델타 vs full 대역 절감 측정. 예산 초과 시 기아 없이 이월 확인. |
| **N7 — 재접속·세션·이중 로그인** | `SessionManager`, grace 창, full resync, duplicate 정책 | 끊었다 재접속 시 상태 복구. 이중 로그인 킥 동작. |
| **N8 — 지연보상 + 서버 검증** | `LagCompensation` rewind, `MovementValidation` sanity | 되감기 히트 판정 정확도(고RTT에서 "맞았는데 안 맞음" 해소). 속도핵 봇 차단. |
| **N9 — 존/인스턴스/핸드오프** | `ZoneManager` 다중 인스턴스·채널, `Handoff` 크로스존 원자 이전 | 필드→던전 진입, 존 경계 이동 무유령. 핸드오프 중 끊김 복구. |
| **N10 — 부하/봇 테스트 + 스케일** | `tools/loadtest` N=수백~수천, 병렬 존 틱(JobSystem) | 존당 목표 동접에서 tick<16.6ms·대역 예산 준수 회귀. |

각 단계는 앞 단계 위에서 **독립 검증 가능**하며, N0~N2가 수직 슬라이스(한 존·소수 인원 실동작)의 최소 골격이다.

---

## 8. 의존성 · 타 도메인 참조

### 8.1 기존 엔진 문서(정본) 참조

- [00-overview](../00-overview.md) — 확장성 제1원칙·백엔드 인터페이스화·"내장=1급 플러그인". `ITransport`가 이 원칙을 따른다.
- [01-core-platform](../01-core-platform.md) — 메인 루프(고정 스텝·spiral clamp), `TimeSystem`/`Clock`(QPC ns), `JobSystem`(Compute/IO 큐·`RunOnMainThread`), `EventBus`(스레드 세이프 `Enqueue`/`Flush`), `Config`(layered), `Module`/`EngineContext`(서비스 게이트웨이), `--headless`. **넷코드의 모든 기반.**
- [03-scene-world](../03-scene-world.md) — ECS(index:32|gen:32, sparse-set, CommandBuffer 지연), `SystemScheduler`(5페이즈), `PhysicsWorld2D`(move&slide), `SpatialHash`(AoI 재사용), `Tilemap`(walkability), `NavSystem`(A*). **서버가 헤드리스 공유. 갭 연계: 컴포넌트 dirty 추적·`ITileCollision` 구현.**
- [02-rendering](../02-rendering.md) — 서버 **미링크**. 클라만 스냅샷→보간→Transform→렌더.
- [06-runtime-systems](../06-runtime-systems.md) — `SceneTransition`/`SceneLoaderFn`(존 로딩 스텁)을 존 스트리밍·핸드오프에 연계.
- [08-mcp](../08-mcp.md) — 서버 빌드/실행/로그·메트릭 관측(개발 도구).

### 8.2 MMORPG 도메인 문서(형제) 상호참조

- [01-server-topology](01-server-topology.md) — 게이트웨이/로그인/월드/채팅 프로세스 분리·오케스트레이션·수평확장. 본 문서는 `world_server`의 넷/시뮬만 담당하고, 프로세스 토폴로지·로드밸런싱을 위임.
- [03-replication-state](03-replication-state.md) — **어떤 컴포넌트를 복제하는가**(복제 스키마·필드 정의·권한 규칙). 본 문서의 스냅샷/델타 코덱이 이를 소비.
- [04-gameplay-combat](04-gameplay-combat.md) — 전투/스킬 규칙·데미지. 본 문서는 판정 타이밍(예측·서버 확정·lag comp rewind)만.
- [05-persistence-db](05-persistence-db.md) — 계정/세션/캐릭터 영속화. 재접속·핸드오프·이중 로그인이 계정 ID·저장 상태를 소비.
- [06-game-runtime](06-game-runtime.md) — 데이터드리븐 클라 부트스트랩·씬 로딩. 넷 클라 시스템이 여기에 배선.
- [09-liveops-anticheat](09-liveops-anticheat.md) — 안티치트 정책·이상탐지·밴·메트릭. 본 문서는 서버측 검증 훅·원시 입력 로깅 제공.

### 8.3 의존 방향 요약

```
mye_core (01) ◄── mye_net (신규, scene 미의존)
mye_core + mye_scene (03) ◄── server/world_server ──► mye_net
mye_core + mye_scene + render/ui/audio ◄── apps/game ──► mye_net
```

`mye_net`은 순수 전송/직렬화라 `mye_core`에만 의존한다(재사용·테스트 용이). 서버는 `mye_scene`을 헤드리스로 링크해 클라와 시뮬 결정론을 공유한다. 이 코드 공유가 "C++ 서버가 엔진 ECS/물리/타일맵을 공유 vs 별도 스택"의 트레이드오프에서 **공유를 채택**한 이유다: 이동·충돌·A*를 두 번 구현하지 않으므로 예측/재조정의 결정론이 구조적으로 보장된다.

---

## 이 도메인 요약 3줄

1. **서버 권위 + 클라 예측/재조정**을 표준으로, 서버는 `engine/scene`을 **헤드리스로 공유**(이동·물리·타일맵·A* 결정론 공짜)하고 넷코드는 순수 전송/직렬화 `engine/net` + `server/world_server` 스택으로 붙인다 — 클라 상태는 절대 신뢰하지 않고 **입력만** 신뢰한다.
2. 핵심 신규는 **NetId(안정 엔티티 ID)·BitStream/델타 스냅샷·신뢰성 UDP 채널·연결 상태기계·AoI(SpatialHash 재사용)·지연보상 rewind·세션/재접속**이며, 01의 고정 스텝 루프·JobSystem·EventBus·Config·모듈 게이트웨이를 그대로 기반으로 삼는다.
3. 경우의 수는 **지연/손실/순서·예측오류·AoI 경계·대역예산·존 핸드오프·재접속/이중로그인·시간동기·서버측 치트방지**를 exhaustive하게 규정했고, N0(전송·직렬화)→N10(부하·스케일)까지 작은 검증 단위로 마일스톤을 쪼갰다.
