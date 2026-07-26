# 04. 게임플레이 시스템 (데이터드리븐 + Lua)

> 소유 범위: 픽셀 2.5D MMORPG의 "콘텐츠 루프" 전체 — 스탯·레벨·경험치, 스킬·쿨다운·자원, 전투(명중/회피/크리/속성/데미지/헤이트), 몬스터 AI(스폰·순찰·추격·귀환·군집), 인벤토리·장비·강화·제작·수리, 루트·드랍·희귀도·바인딩, 경제(상점·거래·경매장·화폐싱크), 퀘스트(수주·진행·보상·체인·일일/주간), 파티·레이드·전리품 분배, 길드·영지·길드전, 채팅(채널·필터·명령어), PvP/PvE·결투·전장·공성·카르마, 인스턴스 던전·매칭, 버프/디버프/상태이상/CC, 사망/부활/페널티, 탈것·펫·페이퍼돌, 상호작용(NPC/오브젝트/채집/포탈), 그리고 이 모두를 잇는 **ECS 컴포넌트 + Lua 훅 매핑**.
>
> 이 문서는 "게임을 게임답게 만드는 규칙 계층"을 정의한다. 렌더·입력·UI·오디오·씬 로딩은 다른 도메인이 소유하고, 여기서는 **서버 권위 시뮬레이션 상태와 그 위의 Lua 콘텐츠**만 다룬다. 핵심 원칙: **모든 게임플레이 상태 변경은 서버 권위 고정틱 위에서만 일어난다. 클라이언트는 표현·예측만 한다.**

---

## 1. 목표·범위

### 1.1 목표

- **데이터 + Lua 철학의 극한 적용**: 스탯 공식, 스킬 정의, 아이템, 루트 테이블, 퀘스트, 몬스터 행동은 전부 **에셋(JSON/`.lua`)**이고, 엔진 C++는 컴포넌트 저장소·틱 시스템·이벤트 배관·복제 훅만 제공한다. 밸런스 패치가 코드 리빌드 없이 데이터 교체로 끝나야 한다.
- **서버 권위(server-authoritative)**: 데미지·드랍·경제·경험치 같은 "돈이 되는" 상태는 절대 클라이언트를 신뢰하지 않는다. 클라이언트는 의도(Command)를 보내고, 서버가 시뮬레이션해 결과(GameEvent + 스냅샷 델타)를 브로드캐스트한다.
- **스케일**: 존(zone)당 수백~수천 엔티티(플레이어 + 몹 + 투사체 + 채집물), 서버 프로세스당 다수 존. 관심 영역(AoI) 필터링으로 브로드캐스트 범위를 좁힌다.
- **결정론적 시뮬레이션 코어**: 전투·스탯·이동은 고정틱(fixed dt)·결정론 RNG로 돌아 리플레이·검증·안티치트가 가능해야 한다.
- **1인 개발 확장성**: 새 스킬/아이템/몹/퀘스트 추가가 "에셋 하나 + Lua 훅 하나"로 끝나는 콘텐츠 파이프라인.

### 1.2 범위 밖 (타 도메인 참조)

| 관심사 | 소유 도메인 |
|---|---|
| 넷코드·복제·예측·스냅샷·RPC 프로토콜 | [05-netcode-replication](05-netcode-replication.md) |
| 서버 프로세스·존 샤딩·매치메이킹 인프라 | [06-server-architecture](06-server-architecture.md) |
| 계정·인증·DB 영속화·트랜잭션 | [07-persistence-accounts](07-persistence-accounts.md) |
| 인게임 UI 위젯(인벤토리 창·채팅창·스킬바) 렌더 | [09-ui-mmo-widgets](09-ui-mmo-widgets.md), 기존 [../06-runtime-systems.md](../06-runtime-systems.md) |
| 데이터드리븐 씬/존 로딩·스트리밍 | [02-world-streaming](02-world-streaming.md), 기존 [../03-scene-world.md](../03-scene-world.md) |
| 게임 런타임 앱(클라 exe·서버 exe 부트) | [01-runtime-app](01-runtime-app.md) |
| AI 콘텐츠 생성(스프라이트·밸런스·대사) | [08-ai-content-pipeline](08-ai-content-pipeline.md) |

본 문서는 이 도메인들과 **경계 계약**으로 맞물린다. 특히: 모든 게임플레이 컴포넌트는 05의 복제 대상 후보이고, 모든 영속 상태는 07의 저장 대상이며, 모든 상태 변경은 06의 존 틱 위에서 일어난다.

---

## 2. 핵심 개념·아키텍처

### 2.1 계층 그림

```
┌─────────────────────────────────────────────────────────────┐
│ 콘텐츠 (에셋 + Lua)                                            │
│  items.json · skills.json · loot_tables.json · quests/*.lua   │
│  mob_ai/*.lua · npc_shops.json · stat_formulas.lua            │
├─────────────────────────────────────────────────────────────┤
│ engine/gameplay (신규 C++ 모듈) — 시뮬레이션 상태·틱·이벤트     │
│  StatSystem · SkillSystem · CombatSystem · InventorySystem     │
│  LootSystem · QuestSystem · AiSystem · BuffSystem · EconomySys │
│  PartySystem · GuildSystem · ChatSystem · InstanceSystem       │
├─────────────────────────────────────────────────────────────┤
│ engine/scene ECS (재사용)  engine/script Lua (확장)            │
│  World·CommandBuffer·5페이즈 스케줄러·월드버스   ScriptSystem   │
├─────────────────────────────────────────────────────────────┤
│ engine/core (재사용): Expected·EventBus·Time·JobSystem·Config  │
└─────────────────────────────────────────────────────────────┘
```

`engine/gameplay`는 **신규 서브시스템**이다. 기존 엔진에는 스탯/전투/인벤/퀘스트 코드가 0줄이다(grep 확인 — `engine/`의 해당 키워드는 전부 UI/에디터/오디오 주석). 유일한 선례는 `engine/runtime`의 `NpcSystem`(배회·상호작용 상태기계)뿐이며, 이는 몬스터 AI의 축소판으로 확장·일반화한다.

### 2.2 서버 권위 시뮬 / 표현 분리

기존 엔진의 5페이즈 스케줄러([`SystemScheduler.h`](../../engine/scene/include/mye/scene/SystemScheduler.h))가 이 분리를 이미 강제한다. 게임플레이 시스템은 페이즈에 정확히 매핑된다:

| 페이즈 | 게임플레이 시스템(서버) | 클라이언트에서 |
|---|---|---|
| `Input` | 클라 Command 수신·검증·큐잉 | 로컬 입력→Command, 예측 적용 |
| `FixedUpdate`(고정틱) | **StatSystem→SkillSystem→CombatSystem→BuffSystem→AiSystem→MovementAuth→LootSystem→QuestSystem** | 예측 재시뮬(reconcile) |
| `Update` | 인스턴스/파티/경제 만료 타이머 | 원격 엔티티 보간 |
| `PostUpdate` | 스냅샷 델타 산출·AoI 브로드캐스트 큐 | Transform 전파·애니 샘플 |
| `RenderExtract` | (서버 없음) | 렌더 프록시 추출 |

**규칙**: 게임플레이 상태(스탯·HP·인벤·쿨다운)는 오직 `FixedUpdate`에서만 변한다. 표현(파티클·사운드·데미지 숫자)은 `GameEvent`(월드 버스)를 구독해 가변 프레임에서 재생한다. 이 파이프 중간에 05 넷코드가 끼어든다.

### 2.3 시스템 실행 순서 계약 (전투 프레임 한 틱)

전투는 순서 의존이 크므로 `runAfter`/`runBefore`로 아래를 고정한다:

```
StatSystem      (파생 스탯 재계산: 장비/버프 dirty 시)
  → SkillSystem   (시전 진행·채널링·쿨다운 감소·시전 완료→CombatIntent 방출)
  → CombatSystem  (CombatIntent 소비: 명중/회피/크리/속성/데미지→HP 적용·헤이트 누적)
  → BuffSystem    (틱 데미지/힐·지속시간 감소·CC 만료·스탯 dirty 마킹)
  → AiSystem      (몹 행동트리: 위협 대상 선정·스킬 시전 결정→Command 생성)
  → DeathSystem   (HP<=0 감지→사망 이벤트·드랍 트리거·부활 타이머)
  → LootSystem    (드랍 롤·소유권 배정·바닥 아이템 생성)
  → RegenSystem   (자원 리젠: HP/MP/기력 고정 회복)
```

각 시스템은 `SystemDesc{ phase=FixedUpdate, reads/writes=<컴포넌트 id 집합>, runAfter=<앞 시스템 이름> }`로 등록한다. 향후 05/06이 `reads/writes` 접근 집합을 써서 JobSystem 병렬 디스패치를 켠다(현재는 선언만 저장·단일스레드, [`SystemScheduler.h:41`](../../engine/scene/include/mye/scene/SystemScheduler.h) 확인).

### 2.4 결정론 RNG

명중/크리/드랍/AI 선택은 **존별 결정론 RNG 스트림**을 쓴다. `xorshift128+`/`pcg32` 하나를 존 틱 시드로 시딩하고, 각 판정은 `(entityId, tick, saltEnum)`을 섞은 하위 스트림에서 뽑는다. 이유: (1) 서버 리플레이·검증, (2) 안티치트(클라 예측과 서버 결과 대조), (3) 버그 재현. `std::rand`·전역 RNG 금지(기존 `NpcSystem`이 이미 결정론 RNG 사용 — 그 패턴을 승격).

### 2.5 ID·핸들 규약

- **엔티티 핸들**은 프로세스 로컬(`index:32|gen:32`, [`Entity.h`](../../engine/scene/include/mye/ecs/Entity.h)). 네트워크·DB에는 그대로 못 쓴다.
- **네트워크 안정 ID**: 각 복제 대상 엔티티에 `NetId`(64bit, 서버 발급 단조증가) 컴포넌트를 부여 → 05가 소유. 게임플레이는 `NetId ↔ Entity` 매핑을 존 레벨 인덱스로 유지.
- **DB 영속 ID**: 플레이어·아이템 인스턴스·길드는 `PersistentId`(128bit GUID 또는 DB serial) — 07 소유. 아이템 인스턴스는 스택 불가 장비의 강화수치·소울바운드 추적을 위해 **인스턴스 단위 고유 ID**를 갖는다.

---

## 3. 기능 목록

우선순위: **P0**=MVP 필수(전투 루프 성립), **P1**=MMO 코어(파티·상점·퀘스트), **P2**=콘텐츠 확장(길드·경매장·인스턴스), **P3**=엔드게임(공성·영지·레이드), **P4**=편의/후반(펫·탈것·페이퍼돌).
상태: **있음**=엔진에 재사용 가능한 실동작 코드, **부분**=일부 프리미티브만 존재, **신규**=완전 신설.

| 기능 | 우선순위 | 상태 | 엔진 매핑 (재사용 / 확장 / 신규) |
|---|---|---|---|
| 스탯·레벨·경험치·성장·파생공식 | P0 | 신규 | **신규** `engine/gameplay` `StatComponent`+`StatSystem`. 파생공식은 `stat_formulas.lua`(05 확장). 리플렉션([`refl`](../../engine/reflect/include/mye/refl/TypeInfo.h))으로 인스펙터·직렬화 자동. |
| 자원(HP/MP/기력)·리젠 | P0 | 신규 | **신규** `ResourceComponent`+`RegenSystem`(FixedUpdate). |
| 스킬·쿨다운·시전·채널링 | P0 | 신규 | **신규** `SkillBookComponent`+`CastStateComponent`+`SkillSystem`. 스킬 정의=`skills.json`, 효과 로직=Lua 훅. 코루틴 시전은 [`CoroutineScheduler`](../../engine/script/include/mye/script/CoroutineScheduler.h) 재사용. |
| 전투: 명중/회피/크리/속성상성/데미지 | P0 | 신규 | **신규** `CombatSystem`. 데미지 공식=`combat_formulas.lua`. `KinematicBody2D`([`Collision.h`](../../engine/scene/include/mye/phys/Collision.h)) 재사용해 근접 판정, 투사체는 신규 `ProjectileComponent`. |
| 투사체·근접·범위(AoE) 판정 | P0 | 부분 | **확장** 기존 `SpatialHash`([`SpatialHash.h`](../../engine/scene/include/mye/phys/SpatialHash.h))·`OverlapArea`/`Raycast`([`PhysicsWorld2D.h`](../../engine/scene/include/mye/phys/PhysicsWorld2D.h)) 재사용해 히트박스 질의. AoE=원/부채꼴/직사각 오버랩. |
| 헤이트(위협)·타겟팅 | P0 | 신규 | **신규** `ThreatTableComponent`. AiSystem이 소비. |
| 몬스터 AI(상태기계/행동트리·순찰·추격·귀환) | P0 | 부분 | **확장** `NpcSystem`([`NpcSystem.h`](../../engine/runtime/include/mye/runtime/NpcSystem.h))의 상태기계를 `AiSystem`으로 일반화. 경로=기존 A*([`Pathfinding.h`](../../engine/scene/include/mye/nav/Pathfinding.h)) 재사용. 행동=`mob_ai/*.lua`. |
| 스폰·리스폰·개체수 관리 | P0 | 신규 | **신규** `SpawnerComponent`+`SpawnSystem`. 존 로딩(02)이 스포너 배치. |
| 버프/디버프/상태이상/CC | P0 | 신규 | **신규** `StatusEffectComponent`(스택 배열)+`BuffSystem`. 효과=`status_effects.json`+Lua 훅. |
| 사망/부활/페널티/회생 | P0 | 신규 | **신규** `DeathSystem`+`ResurrectComponent`. 경험치·내구도 페널티는 데이터. |
| 인벤토리·장비 슬롯·페이퍼돌 | P0 | 신규 | **신규** `InventoryComponent`(그리드/슬롯)+`EquipmentComponent`. 아이템 인스턴스=`ItemInstance`. 장비 외형은 09/애니([`SpriteAnimator`](../../engine/scene/include/mye/anim/SpriteAnimator.h)) 연동. |
| 아이템 정의·스택·바인딩 | P0 | 신규 | **신규** `items.json` + `ItemDatabase`. GUID([`AssetGuid.h`](../../engine/asset/include/mye/asset/AssetGuid.h)) 스타일 안정 참조. |
| 루트테이블·드랍·희귀도 | P0 | 신규 | **신규** `LootSystem`+`loot_tables.json`. 결정론 RNG(§2.4). |
| 상호작용(NPC/오브젝트/채집/포탈) | P0 | 부분 | **확장** `NpcSystem::TryInteract`([`NpcSystem.h:116`](../../engine/runtime/include/mye/runtime/NpcSystem.h))·상호작용 반경 재사용. 채집=신규 `GatherableComponent`, 포탈=신규 `PortalComponent`(02 씬전환 트리거). |
| 퀘스트(수주·조건·진행·보상·체인) | P1 | 신규 | **신규** `QuestLogComponent`+`QuestSystem`. 정의=`quests/*.lua`(이벤트 구독형 목표). 대화 연동=기존 [`DialogueSystem`](../../engine/runtime/include/mye/runtime/DialogueSystem.h). |
| 일일/주간/반복 퀘스트·타이머 | P1 | 신규 | **신규** 서버 시각 기반 리셋. 07 영속. |
| 제작(크래프팅)·분해·강화·수리 | P1 | 신규 | **신규** `CraftingSystem`+`recipes.json`. 강화=`EnhancementSystem`(성공/실패/파괴 확률, 결정론 RNG). |
| 경제: NPC 상점·구매/판매 | P1 | 신규 | **신규** `ShopComponent`+`shops.json`. 화폐=인벤토리 특수 아이템. |
| 파티·공유경험·전리품 분배 | P1 | 신규 | **신규** `PartySystem`(세션 서비스, 존 초월). 전리품=`LootDistributionPolicy`(자유/순번/주사위/기여도). |
| 채팅(지역/귓속말/파티/길드/거래·필터·명령어) | P1 | 부분 | **확장** [`EventBus`](../../engine/core/include/mye/core/Events.h) 채널 모델 재사용, UI는 09. **신규** `ChatSystem`(채널 라우팅·욕설필터·명령어 파서·스팸 억제). |
| PvP·결투·카르마 | P2 | 신규 | **신규** `PvpComponent`+`FlagSystem`(진영/카르마/PK 상태). |
| 경매장(auction house) | P2 | 신규 | **신규** `AuctionSystem`(07 DB 트랜잭션 + 06 크로스존 서비스). |
| 개인거래(P2P trade) | P2 | 신규 | **신규** `TradeSession`(2인 락스텝 확정·원자적 교환·사기 방지). |
| 길드·길드 은행·길드전 | P2 | 신규 | **신규** `GuildSystem`(07 영속 조직). |
| 인스턴스 던전·매칭 | P2 | 신규 | **신규** `InstanceSystem`(06 존 인스턴싱). 매칭=`MatchmakingQueue`. |
| 레이드·공유 진행·위상(phasing) | P3 | 신규 | **신규** 파티/인스턴스 확장. |
| 전장·공성·영지 | P3 | 신규 | **신규** `BattlegroundSystem`·`SiegeSystem`. 대규모 동시성·거점 상태. |
| 탈것·펫·소환수 | P4 | 부분 | **확장** ECS 엔티티 + `KinematicBody2D`·`SpriteAnimator` 재사용. **신규** `MountComponent`·`PetComponent`(펫 AI=AiSystem 재사용). |
| ECS 컴포넌트 ↔ Lua 훅 매핑 | P0 | 부분 | **확장** [`EcsBindings.cpp`](../../engine/script/src/bindings/EcsBindings.cpp)의 `LuaEntity` usertype에 게임플레이 컴포넌트 접근자 추가 + 리플렉션 기반 범용 필드 접근(현재 미구현 갭). |

---

## 4. 데이터 모델·스키마

### 4.1 스탯·자원 (컴포넌트)

파생 스탯은 **1차 스탯(할당) → 2차 스탯(파생 공식)** 두 단계. 공식은 데이터(Lua)라 밸런스 패치가 코드 리빌드 없이 가능.

```cpp
// engine/gameplay/include/mye/gameplay/StatComponent.h
namespace mye::gameplay {

enum class StatId : uint16_t {   // 1차 스탯(레벨업·장비로 증가)
    Str, Dex, Int, Vit, Wis, Luk, Count
};

struct StatComponent {
    MYE_COMPONENT(StatComponent);          // 기존 ECS 매크로(ComponentType.h)
    uint32_t level = 1;
    uint64_t exp   = 0;                    // 현재 레벨 내 경험치
    std::array<int32_t, (size_t)StatId::Count> base{};   // 할당된 1차 스탯
    std::array<int32_t, (size_t)StatId::Count> bonus{};  // 장비/버프 합산(캐시)
    bool     derivedDirty = true;          // 장비/버프 변경 시 마킹 → StatSystem 재계산
};

struct ResourceComponent {
    MYE_COMPONENT(ResourceComponent);
    int32_t hp = 100,  hpMax = 100;
    int32_t mp = 50,   mpMax = 50;
    int32_t stamina = 100, staminaMax = 100;   // 기력(대시·회피)
    float   hpRegenPerSec = 1.0f, mpRegenPerSec = 2.0f;
    double  regenAccum = 0.0;              // 고정틱 누적(정수 회복)
};

// 2차(파생) 스탯 — StatSystem이 공식으로 계산, 컴포넌트로 캐시.
struct DerivedStats {
    MYE_COMPONENT(DerivedStats);
    int32_t attackMin=0, attackMax=0, defense=0;
    int32_t accuracy=0, evasion=0;
    float   critChance=0.0f, critMult=1.5f, attackSpeed=1.0f, moveSpeed=3.0f;
    std::array<int16_t, 6> elemResist{};   // 화/수/풍/지/광/암 저항(%)
};

} // namespace mye::gameplay
```

파생 공식은 Lua로:

```lua
-- assets/data/stat_formulas.lua  (05 스크립트 확장, StatSystem이 호출)
function derive_stats(s)   -- s = {level, str, dex, int, vit, wis, luk, ...(bonus 합산됨)}
  return {
    attack_min = s.str * 2 + s.level,
    attack_max = s.str * 2 + s.level + math.floor(s.dex * 0.5),
    defense    = s.vit * 3,
    accuracy   = s.dex * 4 + s.level * 2,
    evasion    = s.dex * 2 + s.luk,
    crit_chance= math.min(0.5, s.luk * 0.003),
    move_speed = 3.0,   -- 픽셀/틱 기반은 CombatSystem이 PPU 환산
  }
end
```

### 4.2 스킬·시전

```cpp
struct SkillDef {                          // skills.json 로드 → ItemDatabase류 SkillDatabase 상주
    uint32_t id;
    std::string name;                      // LocText 키(로컬라이제이션)
    enum class Cast { Instant, Cast, Channel } castType;
    float   castTimeSec, channelSec, cooldownSec, gcdSec;
    int32_t mpCost, staminaCost;
    float   range, radius;                 // 사거리·효과 반경(월드)
    enum class Shape { Single, Circle, Cone, Line, Self } shape;
    std::string effectHook;                // Lua 함수 이름: on_skill_apply
};

struct SkillBookComponent { MYE_COMPONENT(SkillBookComponent);
    std::vector<uint32_t> learned;
    std::unordered_map<uint32_t, float> cooldownRemain;  // skillId → 남은 쿨(틱 감소)
    float gcdRemain = 0.0f;
};

struct CastStateComponent { MYE_COMPONENT(CastStateComponent);  // 시전 진행 중일 때만 존재
    uint32_t skillId; float remain; Entity target; Vec2 groundTarget;
    bool interruptible = true;             // 피격/이동 시 취소 규칙
};
```

효과는 Lua 훅(서버 측 실행):

```lua
-- assets/skills/fireball.lua
function on_skill_apply(caster, targets, ctx)  -- targets = 판정 통과한 엔티티 배열
  for _, t in ipairs(targets) do
    ctx.deal_damage(caster, t, { element="fire", base=ctx.spell_power(caster)*1.4 })
    ctx.apply_status(t, "burn", { duration=4.0, tick=1.0, dmg=ctx.spell_power(caster)*0.2 })
  end
  ctx.emit_fx(ctx.ground_target, "fx_fireball_impact")   -- 표현은 GameEvent로 클라 브로드캐스트
end
```

### 4.3 아이템·인벤토리·장비

```cpp
struct ItemDef {                           // items.json (정적 정의, 불변)
    uint32_t id; std::string name, icon;
    enum class Kind { Equip, Consumable, Material, Quest, Currency } kind;
    uint16_t maxStack;                     // 1=장비(스택불가)
    enum class Bind { None, OnPickup, OnEquip } bind;
    uint8_t  rarity;                       // 0 일반 ~ 5 신화
    std::array<int32_t,(size_t)StatId::Count> statMods{};   // 장비 스탯 보너스
    uint16_t equipSlot;                    // 페이퍼돌 슬롯(장비만)
    std::string useHook;                   // 소비 아이템 Lua: on_item_use
};

// 아이템 "인스턴스" — 스택불가 장비는 개별 고유 ID(강화·소울바운드 추적).
struct ItemInstance {
    uint64_t instId;                       // DB 영속 고유(07). 스택 아이템은 0 허용.
    uint32_t defId; uint16_t count;
    uint8_t  enhanceLevel;                 // 강화 수치(+0..+N)
    bool     bound;                        // 귀속 여부
    std::vector<uint32_t> sockets;         // 소켓/젬(옵션)
};

struct InventoryComponent { MYE_COMPONENT(InventoryComponent);
    std::vector<ItemInstance> slots;       // 그리드=인덱스, 빈칸=defId 0
    uint32_t capacity;
    uint64_t currency[3];                  // 골드·명예·이벤트화폐 등
};

struct EquipmentComponent { MYE_COMPONENT(EquipmentComponent);
    std::array<ItemInstance, 16> equipped; // 페이퍼돌 슬롯 고정 배열
    // 변경 시 StatComponent.derivedDirty = true (장비 스탯 반영)
};
```

### 4.4 루트 테이블

```json
// assets/data/loot_tables.json
{
  "goblin_common": {
    "rolls": 2,
    "guaranteed": [{ "item": 1001, "min": 1, "max": 3 }],
    "entries": [
      { "item": 2010, "weight": 60, "min": 1, "max": 1 },
      { "item": 2011, "weight": 30 },
      { "item": 3050, "weight": 9, "rarity_boost_by": "luk" },
      { "item": 9001, "weight": 1, "announce": true }
    ],
    "currency": { "gold_min": 5, "gold_max": 20 }
  }
}
```

### 4.5 퀘스트 (이벤트 구독형)

퀘스트는 **월드 버스 이벤트를 구독하는 Lua 상태**로 표현한다(폴링 아님). 기존 [`EventBus`](../../engine/core/include/mye/core/Events.h)와 `DialogueSystem`을 재사용.

```lua
-- assets/quests/q_slay_goblins.lua
return {
  id = "q_slay_goblins", title_key = "quest.goblins.title",
  prerequisites = { level = 5 },
  objectives = {
    { id="kill", type="kill", target="mob_goblin", count=10 },
    { id="talk", type="talk", target="npc_elder", after="kill" },
  },
  rewards = { exp=500, gold=100, items={ {2010,1} }, choose={ {5001,1},{5002,1} } },
  on_event = function(q, ev)     -- q=진행상태, ev=게임이벤트
    if ev.type=="mob_killed" and ev.mob_type=="mob_goblin" then
      q:advance("kill", 1)
    end
  end,
}
```

### 4.6 몬스터 AI (행동트리 데이터)

```lua
-- assets/mob_ai/goblin.lua  (AiSystem이 고정틱에 tick, NpcSystem 상태기계 일반화)
return {
  aggro_radius = 6.0, leash_radius = 15.0,   -- 귀환 거리
  behavior = {
    { state="idle",   on_player_in_aggro="chase", wander=true },
    { state="chase",  chase_target="highest_threat",
      on_in_range=2.0, action="cast:skill_bite",
      on_leash_exceeded="return_home" },
    { state="return_home", heal_full=true, on_arrive="idle" },
    { state="flee",   when="hp_pct<0.15 and is_coward", speed_mult=1.5 },
  },
}
```

---

## 5. 경우의 수·엣지케이스 (exhaustive)

MMO 스케일(수백~수천 동접·지연·복제·치트·라이브운영) 기준으로 도메인별 실패/악용/스케일/동시성/네트워크지연 케이스를 망라한다.

### 5.1 스탯·경험치·레벨

- **오버플로우/언더플로우**: 경험치 `uint64`, 골드 `uint64` — 획득/차감 시 포화(saturating) 연산. 음수 스탯 방지(장비 탈착으로 bonus < 0).
- **경험치 손실 페널티 하한**: 사망 시 경험치 차감이 레벨 다운을 일으키면 규칙 확정(레벨 유지 하한 or 다운 허용). 레벨 경계에서 반복 사망 시 무한 루프 방지.
- **레벨업 즉시 회복 악용**: 전투 중 레벨업으로 풀피 회복 → 밸런스 결정(회복 여부 데이터화).
- **파생 재계산 폭주**: 장비 1개 교체가 매 틱 `derive_stats` Lua 호출 → `derivedDirty` 플래그로 틱당 1회 배치, 변경 없으면 스킵.
- **레벨 싱크(인스턴스 스케일링)**: 고레벨이 저레벨 존/파티 진입 시 스탯 하향 — 원본 스탯 보존 + 오버레이.
- **동시 경험치 분배**: 파티/헤이트 여러 기여자에게 킬 경험치 분배 시 부동소수 반올림 누락/중복 방지(정수 배분 + 나머지 처리).

### 5.2 스킬·쿨다운·시전

- **시전 중 이동/피격/사망**: 취소 규칙(interruptible). 채널링 도중 대상 사망/시야이탈/사거리이탈 → 조기 종료.
- **GCD·개별쿨 이중 게이트**: 글로벌 쿨(GCD)과 스킬 쿨 동시 검사. 클라 예측 시전과 서버 쿨 불일치 → 서버 권위로 롤백(예측 실패 애니 복구).
- **쿨다운 리셋 악용**: 로그아웃/존이동/사망으로 쿨 초기화 치트 → 서버가 쿨 상태 영속·복원.
- **자원 부족 레이스**: 두 스킬이 같은 틱에 MP 소모 → 고정틱 순차 처리로 원자성 보장. 클라가 MP 충분하다 믿고 시전했으나 서버는 부족 → 거부 + 예측 롤백.
- **시전 큐잉 스팸**: 클라가 초당 수백 시전 Command → 서버 rate-limit + GCD 게이트.
- **대상 지정 실패**: 대상이 시전 완료 시점에 무효(파괴/디스폰) → 안전 취소(크래시 금지, `LuaEntity::IsValid` 패턴 재사용).
- **이동 예측 vs 스킬 확정 위치**: 지연 하에 클라가 본 대상 위치와 서버 위치 차이 → 서버 시점 판정(클라는 관대한 히트 피드백만).

### 5.3 전투·판정

- **투사체 관통·다단히트**: 같은 대상 중복 히트 방지(히트 리스트), 관통 횟수 상한.
- **AoE 대량 히트**: 부채꼴에 100+ 대상 → SpatialHash 질의 상한·틱당 처리 예산. 프레임 스파이크 방지.
- **동시 사망 처리**: 같은 틱 상호 치명타 → 사망 순서 결정론(엔티티 ID 타이브레이커). 드랍 소유권 배정 일관성.
- **데미지 반사·흡혈 순환**: 반사→반사 무한 루프 방지(반사는 반사 안 됨 규칙, 재귀 깊이 가드 — [`EventBus`](../../engine/core/include/mye/core/Events.h) 재귀 깊이 가드 패턴 재사용).
- **음수 데미지/힐**: 방어 > 공격 시 최소 1 데미지 하한, 힐 오버플로우 클램프.
- **속성 상성 미정의**: 매트릭스 누락 셀 기본값(1.0배) 폴백.
- **위치 조작 치트(speedhack/teleport)**: 서버가 이동 델타를 최대 속도·틱 간격으로 검증, 초과 시 위치 롤백. 근접 사거리 판정도 서버 위치 기준.
- **헤이트 조작**: 클라가 헤이트 값을 못 씀(서버 전용 `ThreatTable`). 도발/감소 스킬만 규칙대로.

### 5.4 몬스터 AI·스폰

- **리쉬(leash) 무한 추격**: `leash_radius` 초과 시 귀환·풀피·무적. 플레이어가 리쉬 밖에서 원거리 폭딜(kiting) → 귀환 중 데미지 무시 규칙.
- **스폰 캠핑/독점**: 리스폰 즉시 특정 플레이어가 선점 → 스폰 위치 랜덤화·경합 태그.
- **군집(swarm) 폭주**: 스포너가 상한 없이 리스폰 → `maxAlive` 캡. 대량 몹 A* 비용 폭발 → 경로 캐시·flow-field(기존 A*는 요청당 full-cost, [`Pathfinding.h`](../../engine/scene/include/mye/nav/Pathfinding.h) 갭) 또는 스티어링 폴백.
- **AI Lua 무한루프/예외**: 한 몹 행동 Lua가 던지거나 무한루프 → 에러 격리(기존 `ScriptSystem` hasError 패턴), 명령 예산 상한. 나머지 몹 정상 동작.
- **네비 없는 위치로 스폰/추격**: 도달 불가 목표 → A* 실패 시 직선 폴백 또는 포기(기존 A* 실패 폴백 재사용).
- **디스폰 타이밍**: 전투 중 서버 재시작/존 언로드 → 진행 중 전투 정리, 드랍/퀘스트 진행 원자적 커밋 또는 롤백.

### 5.5 인벤토리·아이템·경제 (치트·복제 핵심)

- **아이템 복제(dupe) 버그**: 거래·창고·존이동·크래시 타이밍의 이중 참조 → **모든 아이템 이동은 서버 원자적 트랜잭션**(07 DB), 인스턴스 고유 ID로 존재 검증. "삭제 후 추가"가 아니라 이동은 단일 트랜잭션.
- **인벤 꽉참**: 루트/보상/구매 시 공간 부족 → 우편함 폴백 or 바닥 드랍 or 거부(규칙 데이터화).
- **스택 경계**: `maxStack` 초과 분할, 0개 스택 정리, 음수 count 방지.
- **바인딩 우회**: 귀속 아이템 거래/판매 차단, 귀속 시점(획득/장착) 서버 확정.
- **강화 확률 조작**: 강화 성공/실패/파괴 판정 서버 결정론 RNG, 클라는 결과만 수신. 파괴 시 원자적 소멸.
- **동시 장착 레이스**: 두 아이템을 같은 슬롯에 동시 장착 Command → 고정틱 순차. 장착 실패 시 인벤 복구.
- **화폐 인플레이션**: 몹 골드 드랍·퀘스트 보상 발행 vs 상점 수리비·거래세 소각 → 화폐 싱크(sink) 설계, 발행/소각 로그(07 감사).
- **상점 가격 조작**: 구매/판매가 서버 확정, 클라 표시가 무시. 판매 후 즉시 재구매 차익(buyback) 규칙.
- **경매장 동시 입찰/구매**: 마지막 순간 동시 낙찰 → DB 트랜잭션 직렬화. 만료·환불·수수료 원자성.
- **개인거래 사기**: 확정 직전 아이템 바꿔치기 → 양측 "확정" 후 변경 시 확정 리셋(락스텝). 교환은 원자적(둘 다 성공 or 둘 다 실패).
- **우편 첨부 유실**: 첨부 아이템/화폐 회수 전 만료 → 반송. 우편 트랜잭션 원자성.

### 5.6 퀘스트

- **이벤트 유실**: 몹 킬 이벤트가 퀘스트 수주 전 발생 → 소급 인정 안 함(수주 후 카운트). 다만 "이미 보유 아이템 N개" 류는 수주 시 스냅샷.
- **중복 완료/보상 재수령**: 완료 상태 서버 영속, 재수령 차단. 일일/주간 리셋은 서버 시각 기준(클라 시각 신뢰 금지).
- **공유 목표 카운트**: 파티 동시 킬 시 각자 카운트(개인) vs 공유(파티) 규칙 명시. 기여자 판정.
- **체인 퀘스트 선행 붕괴**: 선행 삭제/롤백 시 후행 잠금. 순환 의존 방지(등록 시 검증).
- **아이템 소모형 목표에서 아이템 손실**: 제출 직전 아이템 파기/거래 → 제출 시점 재검증.
- **퀘스트 로그 상한**: 동시 수주 한도, 초과 시 거부.

### 5.7 파티·전리품 분배

- **리더 이탈/오프라인**: 리더 자동 위임. 전원 오프라인 시 파티 해산 타이머.
- **분배 방식 악용**: 자유획득에서 선점 독식 → 순번/기여도/주사위 모드. 주사위 동점 재굴림.
- **경험치 공유 범위**: 근접(존·거리) 기여자만. 원거리 파티원 무기여 시 페널티. 레벨 격차 페널티.
- **파티 초월 존**: 파티원이 서로 다른 존/샤드 → 파티는 존 초월 세션 서비스(06). 인스턴스 진입은 동일 인스턴스 배정.
- **정원 초과·중복 초대**: 초대 레이스로 정원 초과 방지(원자적 수락).

### 5.8 채팅

- **스팸/도배**: rate-limit(채널별 초당 상한), 동일 메시지 반복 억제.
- **욕설/광고 필터**: 금칙어 테이블(로케일별), 우회(공백·특수문자 삽입) 정규화 후 매칭.
- **명령어 주입**: `/명령어` 파싱 — 권한 검사(GM 명령 일반유저 차단). 인자 검증.
- **귓속말 대상 부재/차단**: 오프라인·차단 목록 대상 → 실패 통지. 블랙리스트.
- **아이템 링크 위조**: `{link=item:id}` 클릭 시 서버가 실제 인스턴스 검증(위조 툴팁 방지).
- **채널 라우팅**: 지역 채팅은 AoI 범위 브로드캐스트(05), 길드/파티는 세션 멤버만. 크로스존은 서버 릴레이.

### 5.9 PvP·인스턴스·대규모

- **결투 중 제3자 개입/도주**: 결투 구역 이탈 무효, 외부 데미지 차단.
- **카르마/PK 악용**: 무고한 유저 공격 페널티, 정당방위 예외. 진영 강제(같은 진영 데미지 0).
- **인스턴스 인원 이탈**: 진행 중 전원 이탈 → 인스턴스 리셋 타이머·진행도 저장 규칙.
- **매칭 큐 이탈/노쇼**: 매칭 수락 후 미접속 → 페널티·재큐. 큐 시간 폭발 시 조건 완화.
- **위상(phasing) 불일치**: 같은 좌표 다른 위상 유저 상호작용 차단.
- **공성/전장 동시성**: 거점 상태 서버 권위, 수백 명 동시 판정 → AoI·틱 예산·배치 처리. 서버 부하 시 우아한 저하(tick rate 하향).

### 5.10 사망·부활·상태이상

- **CC 무한 체인(스턴락)**: 감쇠(diminishing returns) 규칙 — 동일 CC 반복 시 지속 감소·면역 부여.
- **버프 스택 폭주**: 같은 버프 스택 상한, 갱신 vs 중첩 규칙. 상충 버프(공증/공감) 처리.
- **사망 중 상태 적용**: 시체에 데미지/버프 금지. 부활 무적 시간.
- **부활 위치 악용**: 던전 심층 사망 후 즉시 부활로 진행 스킵 → 부활 지점 규칙(마을/체크포인트).
- **DoT로 인한 사후 사망**: 버프 틱이 이미 죽은 대상 처리 → 사망 후 상태 정리 순서(DeathSystem이 StatusEffect 클리어).

### 5.11 네트워크·복제·라이브운영 (전 도메인 공통)

- **지연·패킷 손실**: 클라 예측 + 서버 화해(reconcile). 스냅샷 보간 버퍼. 이 도메인은 05에 "복제 대상 컴포넌트"와 "GameEvent"를 계약으로 제공.
- **서버 재시작·크래시 복구**: 진행 중 전투·거래·인스턴스의 원자적 커밋/롤백. 마지막 세이브 이후 손실 최소화(주기적 스냅샷).
- **핫 밸런스 패치**: 스탯 공식·아이템·드랍률을 라이브 리로드(기존 [핫리로드](../../engine/asset/src/AssetDatabase.cpp) 파이프라인 확장) — 진행 중 세션에 무중단 반영.
- **시계 신뢰 금지**: 쿨다운·일일 리셋·버프 만료는 서버 틱 기준. 클라 시각·틱 조작 무효.
- **결정론 깨짐 감지**: 클라 예측 결과 ≠ 서버 결과가 임계 초과 → 안티치트 플래그·로그.

---

## 6. 신규 모듈·파일 제안

### 6.1 신규 엔진 모듈: `engine/gameplay`

기존 모듈 규약(01 `IModule`·`MYE_SERVICE`·토폴로지 의존)을 따르는 신규 서브시스템. `engine/scene`(ECS)·`engine/script`(Lua)·`engine/runtime`(대화/세이브)에 의존.

```
engine/gameplay/
  CMakeLists.txt
  include/mye/gameplay/
    GameplayModule.h        # IModule — 시스템 등록·서비스·페이즈 틱 배선
    GameplayTypes.h         # StatId·ElementType·DamageType·ID 규약(sol 미포함 순수 계약)
    StatComponent.h         # StatComponent·ResourceComponent·DerivedStats
    StatSystem.h            # 파생 재계산·경험치·레벨업 (FixedUpdate)
    SkillDatabase.h         # skills.json 상주 로드
    SkillComponent.h        # SkillBookComponent·CastStateComponent
    SkillSystem.h           # 시전·채널링·쿨다운·GCD → CombatIntent 방출
    CombatSystem.h          # 명중/회피/크리/속성/데미지/헤이트 (권위 판정)
    ProjectileComponent.h   # 투사체 상태 + ProjectileSystem
    ThreatTable.h           # ThreatTableComponent
    BuffSystem.h            # StatusEffectComponent·틱·CC·감쇠
    DeathSystem.h           # 사망·부활·페널티
    ItemDatabase.h          # items.json·ItemDef·ItemInstance
    InventorySystem.h       # InventoryComponent·EquipmentComponent·원자적 이동
    LootSystem.h            # loot_tables.json·드랍 롤·소유권
    CraftingSystem.h        # recipes.json·강화·분해·수리
    AiSystem.h              # 몹 행동트리(NpcSystem 일반화)·스폰
    SpawnerComponent.h      # SpawnerComponent·SpawnSystem
    QuestSystem.h           # QuestLogComponent·이벤트 구독형 목표
    EconomySystem.h         # ShopComponent·거래세·화폐 싱크
    ChatSystem.h            # 채널 라우팅·필터·명령어 파서
    GameEvents.h            # 월드 버스 게임플레이 이벤트 타입(MYE_EVENT)
  src/
    ...대응 .cpp...
  bindings/
    GameplayBindings.cpp    # 05 확장: mye.stats/skill/inventory/quest/combat Lua 모듈
```

**세션 초월 서비스(존을 넘나드는 조직·거래)** — 존 World가 아니라 서버 프로세스/클러스터에 붙는다. 06 서버 아키텍처 아래 배치:

```
server/gameplay/            # (06 소유 인프라 위, 이 도메인이 로직 제공)
  PartySystem.h/.cpp        # 파티 세션(존 초월)
  GuildSystem.h/.cpp        # 길드·은행·길드전(07 영속)
  AuctionSystem.h/.cpp      # 경매장(07 DB 트랜잭션)
  TradeSession.h/.cpp       # 개인거래 락스텝
  InstanceSystem.h/.cpp     # 인스턴스 던전·매칭(06 존 인스턴싱)
  MatchmakingQueue.h/.cpp
```

### 6.2 Lua 바인딩 확장 (기존 `engine/script`)

[`EcsBindings.cpp`](../../engine/script/src/bindings/EcsBindings.cpp)의 `LuaEntity` usertype에 게임플레이 접근자를 추가하고, 신규 `GameplayBindings.cpp`로 시스템 API를 노출. **현재 갭**: `LuaEntity`는 Transform/Body/Animator만 노출하고 임의 컴포넌트 필드 접근이 없다(리플렉션 usertype 미구현, `ScriptClass.cpp` "엔티티 usertype 후속"). 게임플레이는 리플렉션 기반 범용 컴포넌트 접근이 필요 → [`refl`](../../engine/reflect/include/mye/refl/TypeInfo.h) 연동으로 `entity:get("StatComponent").hp` 형태 제공.

```lua
-- 노출 예정 Lua API (서버 로직에서 사용)
local dmg = mye.combat.roll_damage(caster, target, { element="fire", base=40 })
mye.inventory.add(player, item_id, count)     -- 서버 원자적, 실패 시 false
mye.quest.advance(player, "q_slay_goblins", "kill", 1)
mye.stats.grant_exp(player, 500)
mye.buff.apply(target, "stun", { duration=2.0 })
```

### 6.3 콘텐츠 에셋 레이아웃

```
assets/data/     items.json · skills.json · loot_tables.json · recipes.json
                 stat_formulas.lua · combat_formulas.lua · status_effects.json
                 shops.json · elements.json (상성 매트릭스)
assets/quests/   *.lua (이벤트 구독형)
assets/mob_ai/   *.lua (행동트리)
assets/skills/   *.lua (효과 훅)
```

### 6.4 MCP·에디터 도구 (기존 확장, [08-mcp](../08-mcp.md))

- **MCP 툴 신규**: `gameplay_validate`(밸런스 데이터 정합성 검사 — 존재하지 않는 아이템/스킬 참조, 순환 퀘스트, 드랍 확률 합), `balance_sim`(전투 공식 몬테카를로 시뮬 — DPS·TTK 산출).
- **AI 콘텐츠 생성**([08-ai-content-pipeline](08-ai-content-pipeline.md)): 아이템/스킬/드랍 데이터의 AI 생성 초안 + 밸런스 검증 루프.

---

## 7. 마일스톤 단계 (작은 검증가능 단위)

각 단계는 **헤드리스 단위테스트 + 결정론 검증**을 동반한다(기존 327개 MYE_TEST 러너 재사용).

| 단계 | 산출물 | 검증(테스트) |
|---|---|---|
| **G0** 기반 | `engine/gameplay` 모듈 스켈레톤·`GameplayModule`·`StatComponent`/`ResourceComponent` 등록·리플렉션 배선 | 컴포넌트 add/get/직렬화 왕복, 인스펙터 표시 |
| **G1** 스탯·리젠 | `StatSystem`(파생 공식 Lua)·`RegenSystem` | `derive_stats` 결정론 계산, 레벨업 경계, 리젠 정수 회복, 오버플로우 포화 |
| **G2** 전투 코어 | `SkillSystem`(Instant)·`CombatSystem`(명중/크리/데미지)·`ThreatTable` | 결정론 데미지 롤(시드 고정), 명중/회피 확률, 최소 데미지 하한, 동시 사망 순서 |
| **G3** 버프·사망 | `BuffSystem`(DoT·CC·감쇠)·`DeathSystem`(부활 타이머) | 스택 상한, CC 감쇠, 사후 상태 정리, 부활 무적 |
| **G4** 인벤·아이템 | `ItemDatabase`·`InventorySystem`·`EquipmentComponent`(장비→스탯) | 원자적 이동(dupe 불가), 스택 경계, 장착→derivedDirty, 바인딩 |
| **G5** 루트·드랍 | `LootSystem`·`SpawnSystem`·`AiSystem`(NpcSystem 일반화) | 결정론 드랍 롤, maxAlive 캡, 리쉬 귀환, AI Lua 에러 격리 |
| **G6** 스킬 확장 | 시전/채널링·투사체·AoE·속성 상성 | 시전 취소, 관통 중복 방지, AoE 대량 히트 예산, 상성 폴백 |
| **G7** 퀘스트 | `QuestSystem`(이벤트 구독)·대화 연동·보상 | 이벤트 소급 규칙, 중복 완료 차단, 체인 선행, 일일 리셋(서버 시각) |
| **G8** 경제 로컬 | `EconomySystem`(NPC 상점)·화폐 싱크·수리 | 가격 서버 확정, buyback, 화폐 발행/소각 로그 |
| **G9** 파티·채팅 | `PartySystem`(로컬)·전리품 분배·`ChatSystem`(채널·필터·명령어) | 분배 모드, 경험치 공유 범위, rate-limit, 명령어 권한 |
| **G10** 세션 서비스 | 05/06/07 배선 후: 개인거래·경매장·길드·인스턴스·매칭 | 거래 원자성·사기 방지, 경매 트랜잭션 직렬화, 인스턴스 인원 관리 |
| **G11** PvP·엔드게임 | 결투·카르마·전장·공성·레이드 | 진영 데미지, 카르마 페널티, 대규모 동시성 저하 처리 |
| **G12** 라이브운영 | 핫 밸런스 리로드·안티치트 훅·감사 로그 | 무중단 데이터 리로드, 결정론 불일치 감지, 복제 검증 |

G0~G9는 **단일 프로세스(존 로컬)에서 완결 검증** 가능 — 넷코드(05) 없이 서버 권위 시뮬 로직을 먼저 완성한다. G10부터 05/06/07과 맞물린다.

---

## 8. 의존성·타 도메인 문서 참조

### 8.1 이 도메인이 소비하는 기존 엔진 (재사용)

| 재사용 | 위치 | 용도 |
|---|---|---|
| ECS World·CommandBuffer | [`engine/scene/.../World.h`](../../engine/scene/include/mye/ecs/World.h) | 게임플레이 컴포넌트 저장·지연 구조변경 |
| 5페이즈 스케줄러 | [`SystemScheduler.h`](../../engine/scene/include/mye/scene/SystemScheduler.h) | 시뮬(FixedUpdate)/표현 분리·시스템 순서 |
| 월드 EventBus | [`Events.h`](../../engine/core/include/mye/core/Events.h) | GameEvent 발행·퀘스트/채팅 구독·재귀 가드 |
| 리플렉션·직렬화 | [`refl`](../../engine/reflect/include/mye/refl/TypeInfo.h)·[`JsonArchive.h`](../../engine/reflect/include/mye/ser/JsonArchive.h) | 컴포넌트 인스펙터·세이브·데이터 로드 |
| SaveSystem 참여자 | [`SaveSystem.h`](../../engine/runtime/include/mye/runtime/SaveSystem.h) | 인벤/퀘스트/스탯 로컬 영속(싱글·오프라인) |
| Lua 런타임·코루틴·에러격리 | [`ScriptSystem.h`](../../engine/script/include/mye/script/ScriptSystem.h)·[`CoroutineScheduler.h`](../../engine/script/include/mye/script/CoroutineScheduler.h) | 스킬/퀘스트/AI 훅·시전 코루틴 |
| NpcSystem 상태기계 | [`NpcSystem.h`](../../engine/runtime/include/mye/runtime/NpcSystem.h) | AiSystem 일반화의 출발점 |
| A* 길찾기 | [`Pathfinding.h`](../../engine/scene/include/mye/nav/Pathfinding.h) | 몹 추격·순찰 경로(캐시/flow-field는 확장) |
| 물리 쿼리·SpatialHash | [`PhysicsWorld2D.h`](../../engine/scene/include/mye/phys/PhysicsWorld2D.h)·[`SpatialHash.h`](../../engine/scene/include/mye/phys/SpatialHash.h) | 히트박스·AoE 오버랩·투사체 판정 |
| 애니 상태머신 | [`SpriteAnimator.h`](../../engine/scene/include/mye/anim/SpriteAnimator.h) | 공격/피격/사망 애니 트리거(표현) |
| 대화·세이브·로컬라이제이션 | [기존 06](../06-runtime-systems.md) | 퀘스트 대화·NPC 상호작용·문자열 테이블 |

### 8.2 이 도메인이 계약으로 넘기는 것 (타 도메인 참조)

- **[05-netcode-replication](05-netcode-replication.md)**: 모든 게임플레이 컴포넌트는 "복제 대상 후보 + 델타 인코딩 스키마". GameEvent는 클라 브로드캐스트 대상. 클라 예측/서버 화해 대상 정의(이동·시전). `NetId` 소유.
- **[06-server-architecture](06-server-architecture.md)**: 존 틱 위에서 게임플레이 시스템 실행. 파티/길드/경매/인스턴스 세션 서비스 호스팅. AoI 필터링. 존 초월 라우팅.
- **[07-persistence-accounts](07-persistence-accounts.md)**: 인벤/장비/스탯/퀘스트/화폐/우편/경매의 DB 영속·**원자적 트랜잭션**(dupe 방지 핵심). 아이템 인스턴스 고유 ID·감사 로그.
- **[02-world-streaming](02-world-streaming.md)** / 기존 [03](../03-scene-world.md): 존 로딩 시 스포너·NPC·채집물·포탈 배치. 타일 충돌(현 미구현 갭)로 이동 판정.
- **[09-ui-mmo-widgets](09-ui-mmo-widgets.md)** / 기존 [06](../06-runtime-systems.md): 인벤토리 창·스킬바·퀘스트 로그·채팅창·상점창 UI. 이 도메인은 데이터·이벤트만 제공.
- **[01-runtime-app](01-runtime-app.md)**: 게임 클라/서버 exe가 GameplayModule을 부트 배선. (현재 게임 런타임 앱 부재 갭.)
- **[08-ai-content-pipeline](08-ai-content-pipeline.md)** / 기존 [08-mcp](../08-mcp.md): 아이템/스킬/드랍/밸런스 데이터 AI 생성 + 검증 루프.
- **[00-overview](../00-overview.md)**: 전체 아키텍처·레이어 위치.

---

## 이 도메인 요약 3줄

1. **게임플레이 시스템은 100% 신규 `engine/gameplay` 모듈**이다 — 기존 엔진엔 스탯/전투/인벤/퀘스트 코드가 0줄이고, 유일한 선례인 `NpcSystem`(배회 상태기계)을 `AiSystem`으로 일반화한다. 반면 ECS·5페이즈 스케줄러·월드버스·리플렉션·Lua 런타임·A*·물리쿼리·세이브 참여자·코루틴은 그대로 재사용하는 견고한 토대가 이미 있다.
2. **모든 것은 데이터(JSON) + Lua 훅**이다: 스탯 공식·스킬 효과·아이템·루트·퀘스트·몹 AI가 에셋이고, C++는 서버 권위 고정틱 시스템·컴포넌트 저장·이벤트 배관만 소유해 코드 리빌드 없는 밸런스 패치를 보장한다.
3. **서버 권위 + 결정론 + 원자적 트랜잭션**이 3대 불변식이다 — 데미지·드랍·경제는 클라를 신뢰하지 않고, 판정은 결정론 RNG로 리플레이·검증 가능하며, 아이템 이동은 dupe를 막는 단일 트랜잭션이다. 05(넷코드)·06(서버)·07(영속)과의 경계 계약이 이 도메인의 생사를 가른다.
