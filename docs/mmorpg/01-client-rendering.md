# MMORPG 01 — 클라이언트 아키텍처 & 픽셀 2.5D 렌더링

> 도메인 소유 주제: **게임 클라이언트가 화면에 그리는 모든 것** — 대규모 월드 스트리밍, 수백~수천 엔티티의 배칭/컬링/LOD, 2D 라이팅, 파티클, 포스트프로세싱, 강화 카메라, 네트워크 엔티티 보간·스냅샷 렌더, 월드 앵커 오버레이(이름표·HP바·데미지 플로터), 미니맵/월드맵, 런타임 타일맵 렌더.
> 전제: 픽셀 2.5D MMORPG, 수백~수천 동접, RTT 30~300ms, 서버 권위 복제, 치트 방어, 라이브 운영.
> 이 문서는 기존 [docs/02-rendering.md](../02-rendering.md)가 확정한 좌표계·PPU·레이어 밴드·깊이 함수 규약을 **인용만** 하고 재정의하지 않는다. 렌더 데이터 계약은 02, 씬 추출은 [docs/03-scene-world.md](../03-scene-world.md)가 정본이다.

---

## 1. 목표·범위

### 1.1 목표

1. **한 화면에 수백~수천 스프라이트를 60fps(내부 960×540 기준)로** — 도시 광장·보스전·필드 몹 밀집에서도 프레임 예산을 지킨다.
2. **네트워크로 복제된 원격 엔티티를 지터 없이 렌더** — 서버 스냅샷(10~30Hz)을 60~300Hz 렌더 프레임으로 보간/외삽해 부드럽게 그린다. 로컬 고정스텝 보간(02 계약)과 원격 스냅샷 보간을 **분리된 두 경로**로 다룬다.
3. **대규모 월드를 메모리 안에 스트리밍** — 존/청크를 이동에 따라 로드·언로드·프리페치하고, 심리스 존 경계를 지원한다. 03의 `TilemapWorld`(무한 청크)에 스트리밍 경계와 다중 존 그래프를 얹는다.
4. **MMORPG 특유의 화면 정보 밀도** — 이름표·길드태그·HP/MP바·상태이상 아이콘·데미지 플로터·말풍선·타겟 마커·미니맵을 월드 앵커로 대량(수백 개) 그려낸다.
5. **02의 하이브리드 단일 뎁스버퍼를 그대로 재사용** — 밴드+Y-sort 깊이 인코딩(다리 위/아래, 큰 3D 뒤로 돌아 들어가기)을 MMO 스케일에서 깨지 않게 유지한다.
6. **저사양~고주사율 스펙트럼** — 통합 그래픽 노트북부터 300Hz 모니터까지. 품질 스케일러(파티클/라이트/오버레이 예산 동적 조절)를 1급으로 제공.

### 1.2 범위

**포함**: 클라이언트 렌더 파이프라인 확장(라이팅/파티클/포스트/카메라), 월드 스트리밍(청크·존·프리페치), 엔티티 배칭/컬링/LOD, 네트워크 엔티티 보간·외삽·스냅샷 렌더링(**렌더 소비 측**), 월드 앵커 오버레이 UI, 미니맵/월드맵, 런타임 타일맵 렌더+오토타일 런타임 반영, 성능 예산·프로파일링·품질 스케일러.

**비포함(타 도메인)**: 네트워크 트랜스포트·스냅샷 프로토콜·예측/보정 로직 자체([02-netcode](02-netcode-replication.md)), 서버 권위·관심관리(interest management) 서버측([03-server-world](03-server-world.md)), 게임플레이 데이터 모델([04-gameplay-framework](04-gameplay-framework.md)), 인게임 UI 위젯 프레임워크·채팅·인벤토리([05-ui-social](05-ui-social.md)), 에셋 스트리밍 전송·CDN·패치([06-asset-streaming](06-asset-streaming.md)). 본 문서는 이들의 **렌더 소비 측**만 다룬다.

> 상호참조 표기: `[NN-slug](NN-slug.md)`는 같은 `docs/mmorpg/` 폴더의 자매 도메인 문서, `[docs/NN](../NN-....md)`는 기존 엔진 설계 문서다. 자매 문서 슬러그는 이 도메인 시리즈의 제안 파일명이며, 실제 생성 시 조정될 수 있다(§8).

---

## 2. 핵심 개념·아키텍처

### 2.1 렌더 프레임의 두 시간 도메인

MMO 클라이언트는 **세 개의 서로 다른 클럭**을 하나의 렌더 프레임으로 합성한다. 이 분리가 본 도메인의 근간이다.

| 클럭 | 주기 | 소스 | 렌더 처리 |
|---|---|---|---|
| **로컬 시뮬레이션** (내 캐릭터·예측 이동·이펙트) | 고정 60Hz | 01 메인 루프 FixedUpdate | `lerp(prev, curr, alpha)` — 02 기존 보간 계약 그대로 |
| **원격 스냅샷** (타 플레이어·몹·투사체) | 서버 tick 10~30Hz + RTT | 넷코드([02-netcode](02-netcode-replication.md)) | 스냅샷 버퍼에서 **보간 지연(interpolation delay)** 후 `slerp/lerp`, 부족 시 **외삽(extrapolation)** |
| **렌더 프레임** | 가변 60~300Hz | 01 렌더 루프 | 위 둘을 각자의 alpha로 합성 → 픽셀 스냅 → 패스 체인 |

핵심 규칙(02의 "보간 → 픽셀 스냅" 순서를 확장):

```
final_pos = (엔티티가 로컬 예측 대상) ? lerp(prevFixed, currFixed, localAlpha)
                                     : SnapshotInterp(entity, renderTime - interpDelay)
render_pos = PixelSnap(final_pos)   // 02 규약: 스냅은 항상 마지막
```

- **로컬 플레이어**는 클라이언트 예측(client-side prediction)으로 즉시 반응하고 서버 보정(reconciliation) 시 부드럽게 스무딩. 렌더는 예측된 고정스텝 트랜스폼을 02 보간으로 그린다.
- **원격 엔티티**는 항상 `renderTime - interpDelay`(보통 100~200ms 과거) 시점을 스냅샷 두 개 사이에서 보간해 그린다. 스냅샷이 늦으면 마지막 속도로 짧게 외삽하고, 외삽 한계(예: 250ms)를 넘으면 홀드 후 페이드아웃 대상 후보.

### 2.2 렌더 파이프라인 확장 지도 (02 패스 체인 위에)

02가 정의한 `PassStage` 슬롯 체인을 그대로 쓰되, MMO가 요구하는 소비자를 각 슬롯에 얹는다. **새 스테이지를 만들지 않고 기존 슬롯의 밀도/예산만 키운다**(02 확장 원칙 존중).

```
Opaque3D → TerrainTiles(청크 스트리밍) → WorldSorted(수천 스프라이트 배칭·컬링·LOD)
  → SilhouetteFX(가려진 파티원/자신) → Transparent(파티클·이펙트) → Lighting2D(횃불·마법·데이나이트)
  → PostProcess(블룸·그레이딩·비네트·CRT 옵션) → Upscale(960×540→정수배)
  → UI(네이티브: 월드앵커 오버레이·미니맵·HUD) → Overlay(개발 전용)
```

- **WorldSorted가 최대 부하 지점**. 현재 `HybridRenderer::CollectAndDrawSpritesTiles`는 프레임마다 전 `RenderProxyList`를 순회해 텍스처런 분할 드로우콜을 발행한다(텍스처 종류 수 = 드로우콜). MMO 스케일에선 **아틀라스 통합 + 인스턴싱 + 프러스텀/뷰포트 컬링 + LOD**로 재작성이 필요하다(§3, §5).
- **오버레이는 월드 좌표를 추적하는 네이티브 해상도 UI**다. `Camera2D::WorldToScreen`(존재)이 정본. 수백 개 이름표를 매 프레임 변환·컬링·배칭해야 한다.

### 2.3 월드 스트리밍 모델

```
[존 그래프(zone graph)]  ──ptr──> [ActiveZoneSet]  ──청크 로드/언로드──> [TilemapWorld(03) + RenderProxyList]
     |                                   ^
     |                                   | 플레이어 위치·속도 → 프리페치 반경
     └── 존 경계(seam)·포탈·심리스 경계 ──┘
```

- **청크(chunk)**: 03의 `TilemapWorld`가 이미 32×32 셀 청크를 희소 보관. **로드 단위이자 컬링 단위**로 승격한다. `TilemapRenderer.runtime`(비소유 포인터)이 렌더 추출 진입점.
- **존(zone/map)**: 마을·던전·필드 등 논리 맵. 각 존 = 하나의 `TilemapWorld` + ECS 서브월드(또는 태그). 존 간 이동은 로딩 화면(포탈) 또는 심리스(경계 프리로드).
- **프리페치**: 플레이어 위치+속도 벡터로 진행 방향 청크를 미리 IO 큐(01 JobSystem IO 큐)로 로드. 히스테리시스로 경계 왕복 시 로드/언로드 채터링 방지.
- **서버 권위와 정합**: 클라이언트가 그리는 존/청크 범위는 서버의 관심관리(AOI) 범위 안에 있어야 한다. 서버가 보내지 않은 엔티티는 렌더 대상이 아니다([03-server-world](03-server-world.md)).

### 2.4 엔티티 렌더 스케일 파이프라인

수천 엔티티를 그리는 4단 파이프라인(모두 신규 또는 기존 확장):

1. **컬링(cull)**: 뷰 프러스텀(직교 = 뷰 렉트) + 존/청크 경계로 화면 밖 엔티티 제거. 03 RenderExtract가 이미 순회하므로, 여기에 뷰 렉트 컬링을 삽입.
2. **LOD 선택**: 거리/화면크기로 (a) 풀 애니메이션 스프라이트, (b) 정지 프레임, (c) 임포스터(단색/실루엣 점), (d) 이름표만/컬됨. 원격 엔티티 밀집 시 애니메이션 샘플링 비용 절감.
3. **배칭(batch)**: 아틀라스 단위로 인스턴스 스트림 구성. per-instance = {transform2x3, uvRect, tint, flash, palette, depthZ}. 드로우콜을 아틀라스 수로 수렴.
4. **정렬 인코딩**: 02 `DepthEncoder`가 그대로 depth 인코딩(밴드+sortKeyY). 인스턴싱에서도 per-instance depthZ는 CPU가 `EncodeDepth`로 계산해 넘긴다 — 뎁스버퍼가 최종 심판이므로 CPU 정렬은 배치 효율용일 뿐.

---

## 3. 기능 목록

우선순위: **P0**=수직 슬라이스 필수(첫 플레이 가능 빌드) · **P1**=MMO 체감 필수 · **P2**=완성도 · **P3**=라이브 운영/연출 · **P4**=선택·장기.
상태: **있음**=엔진에 구현 존재(재사용) · **부분**=일부만 존재(확장 필요) · **신규**=미존재.

| 기능 | P | 상태 | 엔진 매핑 (재사용/확장/신규) |
|---|---|---|---|
| 하이브리드 단일 뎁스버퍼 정렬(밴드+Y-sort) | P0 | 있음 | `render/HybridRenderer` + `render/DepthEncoder` 재사용. MMO는 인스턴싱 경로에서도 동일 `EncodeDepth` 호출 |
| 픽셀퍼펙트 내부 RT + 정수배 업스케일 | P0 | 있음 | `render/PixelPerfectTarget` 재사용. sharp-bilinear 비정수배 모드만 확장(현 정수배만) |
| Screen2D 카메라·픽셀스냅·Screen↔World | P0 | 있음 | `render/Camera2D` 재사용. Follow는 있음, 데드존·경계·셰이크·줌 트윈은 확장 |
| 런타임 타일맵 청크 렌더 | P0 | 부분 | `HybridRenderer::BuildTileChunkQuads`는 UV 미배선(전체 텍스처 placeholder). **타일 아틀라스 UV 매핑 배선 필요** |
| 오토타일 런타임 반영 | P1 | 부분 | 03 오토타일 룰 평가는 에디터/임포트 시점. 런타임 편집(건설·파괴) 반영은 청크 리빌드 훅 신규 |
| 뷰 컬링(프러스텀/뷰렉트) | P0 | 신규 | 03 `ExtractRenderItems`에 뷰 렉트 컬 삽입 또는 신규 `render/CullingSystem` |
| 스프라이트 인스턴싱 배칭 | P0 | 신규 | RHI `DrawIndexedInstanced` 계약 존재하나 DX11 미배선. **인스턴스 VB 슬롯·StructuredBuffer 배선 필요**(현 `SetVertexBuffer` 슬롯0만) |
| 엔티티 LOD(애니/정지/임포스터) | P1 | 신규 | 신규 `render/EntityLod`. 03 애니 샘플링 게이팅 + 렌더 임포스터 |
| 원격 엔티티 스냅샷 보간/외삽 | P0 | 신규 | 신규 `render/SnapshotInterpolator`(렌더 소비). 넷코드([02-netcode](02-netcode-replication.md))가 스냅샷 공급 |
| 로컬 예측 엔티티 렌더 스무딩 | P0 | 부분 | 02 고정스텝 보간 재사용 + 보정 스무딩(reconciliation smoothing) 신규 |
| 2D 라이팅(포인트·앰비언트·데이나이트) | P1 | 신규 | 02 `Lighting2D` 스테이지 설계는 있음, 구현 0. 신규 `render/Light2DPass` + 라이트버퍼 RGBA16F RT |
| 노멀맵 2D 라이팅 | P3 | 신규 | 02 `LIT2D` 퍼뮤테이션 설계만. MRT 필요 — 확장 |
| 2D 그림자(블롭·캐스트) | P2 | 신규 | 발밑 블롭 스프라이트(P2) + 스프라이트 기울임 캐스트(P3) |
| 파티클 시스템(이펙트·마법·날씨·타격) | P1 | 신규 | 렌더 코드 0건. 신규 `render/ParticleRenderer`(Transparent 패스). 시뮬은 06 소유(FxModule) 경계 |
| 포스트프로세싱(블룸·그레이딩·비네트·CRT) | P2 | 신규 | 02 `PostProcess` 스테이지 설계만. 신규 `render/PostChain` + `IPostEffect` |
| 강화 카메라(데드존·경계·셰이크·줌·존전환) | P1 | 부분 | `Camera2D::Follow` 있음. 신규 `render/CameraController`(데드존·룩어헤드·클램프·셰이크·줌 트윈) |
| 다중 뷰포트 카메라(미니맵 RT·분할) | P2 | 부분 | RHI 오프스크린 RT 있음, 카메라 스택 미구현. 신규 카메라 스택 |
| 월드 앵커 오버레이(이름표·HP바·데미지) | P0 | 신규 | `Camera2D::WorldToScreen` 재사용. 신규 `render/WorldOverlay`(대량 배칭·컬링·데미지 플로터 풀) |
| 상태이상 아이콘·버프바·캐스팅바 | P1 | 신규 | WorldOverlay 위. 04 게임플레이 데이터 소비 |
| 말풍선·채팅 버블 | P2 | 신규 | WorldOverlay + FreeType 텍스트(06). 겹침 회피 레이아웃 |
| 미니맵 렌더 | P1 | 신규 | 신규 `render/MinimapRenderer`(오프스크린 RT 또는 절차 드로우) + 블립 |
| 월드맵(전체 지도·마커) | P2 | 신규 | 존 그래프 데이터 + 정적 지도 텍스처 + 마커 오버레이 |
| 월드 스트리밍(청크 로드·언로드·프리페치) | P1 | 부분 | 03 `TilemapWorld` 청크 있음, 스트리밍 경계·프리페치 신규. 신규 `render/WorldStreamer` |
| 존 그래프·포탈·심리스 경계 | P2 | 신규 | 03 SceneManager 스텁 + 신규 존 그래프 |
| 히트 플래시·틴트·팔레트 스왑 | P1 | 부분 | 플래시·틴트는 셰이더에 있음. 팔레트 스왑(염색·상태색)은 02 `PALETTE_SWAP` 퍼뮤테이션 신규 |
| 아웃라인(타겟 강조·상호작용) | P1 | 신규 | 02 `OUTLINE` 퍼뮤테이션 신규. 아틀라스 패딩 필요(04) |
| 디더 페이드(가림 오브젝트) | P2 | 신규 | 02 설계(스크린도어 clip)만. 지붕·나무 캐노피 진입 시 |
| 실루엣/X-ray(가려진 자신·파티원) | P2 | 신규 | 02 `SilhouetteFX` 스테이지 설계만. 스텐실 + depth GREATER |
| 성능 예산·품질 스케일러 | P1 | 신규 | 신규 `render/QualityScaler`(파티클·라이트·오버레이·LOD 예산 동적) |
| GPU 프로파일링(타임스탬프) | P2 | 부분 | RHI `WriteTimestamp`/`ResolveTimestamps` 계약만, DX11 스텁. 배선 필요 |
| 엔티티 ID 버퍼 픽킹(마우스 타겟) | P1 | 부분 | RHI `CopyTextureToBuffer`/`EnqueueReadback` 계약만, DX11 스텁. **R32Uint ID 패스 + 리드백 배선 필요** |

---

## 4. 데이터 모델·스키마

### 4.1 렌더 프록시 확장 (03 `RenderItem` 위)

03 `RenderExtract.h::RenderItem`은 이미 `worldTransform`/`sortLayer`/`sortKeyY`/`tint`/`flashAmount`/`flipX`를 갖는다. MMO 렌더가 요구하는 필드를 **추가**한다(03 계약과 협의 — 03이 채우고 02가 소비).

```cpp
// 03 RenderItem 확장 제안 (기존 필드 유지, 아래 append)
struct RenderItem {
    // ... 기존: kind, texture, srcUV, pivotPx, tint, flashColor, flashAmount,
    //     flipX/Y, worldTransform, sortLayer, orderInLayer, sortKeyY, floorLevel ...

    // ---- MMO 렌더 확장 ----
    Mat4     prevWorldTransform = Mat4::Identity(); // 로컬 보간용(02 계약 배선). 원격은 SnapshotInterp가 대체
    uint8_t  interpMode = 0;      // 0=LocalFixed(02보간) 1=Snapshot(원격) 2=None(스냅)
    uint32_t netEntityId = 0;     // 네트워크 안정 ID(스냅샷 보간 키). 0=로컬 전용
    uint16_t paletteIndex = 0;    // 팔레트 스왑 LUT row(염색·팀색·상태색). 0=원본
    uint8_t  lodBias = 0;         // LOD 강제(디버그/품질스케일러). 0=자동
    uint8_t  overlayFlags = 0;    // 비트: 이름표·HP바·타겟마커·캐스팅바 표시 대상
    uint16_t factionColorId = 0;  // 이름표/아웃라인 진영색(아군/적/파티/길드)
    float    fadeAlpha = 1.0f;    // 디더 페이드 계수(가림 오브젝트, 원거리 페이드아웃)
};
```

### 4.2 인스턴스 스트림 (신규, 인스턴싱 배칭)

```cpp
// render/SpriteInstance.h (신규) — StructuredBuffer 또는 인스턴스 VB로 GPU 전송
struct SpriteInstanceGpu {           // 32B 정렬 목표(캐시·대역폭)
    float    m00, m01, m10, m11;     // 2x2 회전·스케일
    float    tx, ty;                 // 월드 위치(unit)
    float    z;                      // 02 EncodeDepth 결과(NDC 깊이)
    float    uvMinX, uvMinY, uvMaxX, uvMaxY; // 아틀라스 서브렉트
    uint32_t tintRGBA;               // packed 8888 straight
    uint32_t flashPalette;           // flash(8) | paletteIndex(16) | flags(8)
};
// 아틀라스별로 std::vector<SpriteInstanceGpu> 하나 → DrawIndexedInstanced 1콜(쿼드 IB 공유)
```

### 4.3 원격 엔티티 스냅샷 버퍼 (신규, 렌더 소비 측)

```cpp
// render/SnapshotInterpolator.h (신규)
struct EntitySnapshot {
    double   serverTime;   // 서버 tick 타임스탬프(넷코드 공급)
    Vec2     pos;          // 월드 위치(Screen2D XY)
    float    facingRad;    // 바라보는 방향(8방향 스냅 전 원값)
    uint16_t animState;    // 애니메이션 상태 id
    uint8_t  floorLevel;   // 층(다리 위/아래) — sortLayer 밴드 결정
    uint16_t flags;        // 사망·은신·투명 등 렌더 영향 플래그
};
struct RemoteEntityTrack {
    uint32_t netEntityId;
    RingBuffer<EntitySnapshot, 8> history; // 최근 N개(보간·외삽용)
    Vec2  lastRenderPos;   // 보정 스무딩용
    double lastSeenTime;   // 미수신 시 페이드아웃 판단
};
// renderTime - interpDelay 시점을 두 스냅샷 사이 lerp. 미래면 마지막 속도로 외삽(한계 clamp).
```

### 4.4 카메라 컨트롤러 (신규, `Camera2D` 위 래퍼)

```cpp
// render/CameraController.h (신규) — Camera2D를 소유·구동. Camera2D는 재작성 안 함.
struct CameraControllerDesc {
    Rect   deadzonePx{0,0,120,80};   // 타겟이 이 렉트 안이면 카메라 정지(내부 RT px)
    Vec2   lookaheadUnits{0,0};      // 이동 방향 선행
    Rect   worldBounds{};            // 카메라 중심 클램프 경계(존 크기). 빈 렉트=무제한
    float  followSmoothing = 0.12f;  // 프레임률 독립 지수 감쇠
    float  zoomMin = 0.5f, zoomMax = 2.0f;
};
struct CameraShake { float amplitudePx; float freqHz; float durationS; uint32_t seed; };
// 존 전환 시 bounds·zoom 트윈. 셰이크는 SubpixelResidual과 합성(픽셀스냅 유지).
```

### 4.5 라이트 프록시 (02 `Light2DProxy` 위, 03 컴포넌트 추출)

```cpp
// 02 Light2DProxy 인용 + MMO 확장(03 Light 컴포넌트가 공급)
struct Light2DProxy {
    Vec2   posWorld; float radius; Color color; float intensity;
    uint8_t kind;        // 0=Point 1=Spot 2=Ambient(전역)
    float   spotAngleRad, spotDirRad; // Spot
    uint16_t flags;      // 깜빡임(횃불)·펄스(마법)·그림자 캐스트 여부
};
// 데이나이트: 전역 ambient 컬러/강도를 시간 커브로. 서버가 월드 시간 권위(라이브 통일).
```

### 4.6 오버레이 데이터 (신규, 대량)

```cpp
// render/WorldOverlay.h (신규)
struct NameplateData {
    uint32_t netEntityId;
    Vec2     anchorWorld;   // 머리 위 월드 좌표
    uint16_t factionColorId;
    float    hpFrac, mpFrac; // 0..1
    uint8_t  bars;          // 비트: HP·MP·캐스팅·경험치
    uint8_t  statusIcons;   // 상태이상 아이콘 개수
    // 텍스트(이름·길드)는 문자열 풀 인덱스로(FreeType 셰이핑 06)
};
struct DamageFloater {     // 오브젝트 풀(초당 수백 생성 가능)
    Vec2 startWorld; float elapsed, lifetime;
    int32_t amount; uint8_t kind; // 물리/마법/힐/크리티컬/미스
};
```

---

## 5. 경우의 수·엣지케이스 (exhaustive)

### 5.1 스케일 — 수백~수천 엔티티

| 상황 | 문제 | 대응 |
|---|---|---|
| 도시 광장 500명 동시 표시 | 텍스처런 분할 드로우콜 폭발(현 SpriteBatch는 텍스처 종류당 드로우) | 아틀라스 통합 + 인스턴싱(§4.2). 캐릭터/장비 아틀라스 페이지 최소화 |
| 보스전 파티클 수만 개 | Transparent 패스 오버드로우·CPU 시뮬 병목 | GPU 파티클(인스턴싱) + 화면당 파티클 예산 캡 + 거리 컬 + 품질스케일러 하향 |
| 이름표 500개 매 프레임 WorldToScreen | CPU 변환·텍스트 셰이핑 비용 | 컬링(화면 밖 스킵) + 거리 LOD(멀면 이름표 숨김) + 글리프 아틀라스 캐시 + 배칭 |
| 데미지 플로터 초당 수백 | 힙 할당·정렬 폭주 | 오브젝트 풀 + 상한 캡(초과 시 합산 표시 "x12") + 페이드 후 반환 |
| 몹 500마리 애니메이션 샘플링 | 03 애니 시스템 O(n) | LOD: 원거리 몹은 정지 프레임/저프레임레이트 샘플. 화면 밖 완전 스킵 |
| 광역 스킬 이펙트 100개 동시 | 오버드로우 필레이트 한계(저사양) | 이펙트 인스턴스 상한 + 겹침 시 대표 1개 + 저사양 프로파일 강제 캡 |

### 5.2 네트워크 지연·복제

| 상황 | 문제 | 대응 |
|---|---|---|
| 스냅샷 지터(패킷 간격 불규칙) | 원격 캐릭터가 끊겨 보임 | interpDelay 버퍼(100~200ms)로 항상 과거 시점 보간. 버퍼는 지터 통계로 적응 |
| 패킷 로스(스냅샷 유실) | 보간 소스 부재 | 마지막 속도로 외삽(한계 250ms clamp). 초과 시 위치 홀드 + 페이드 후보 |
| 순간이동·넉백(불연속 이동) | 보간이 미끄러짐(순간이동이 슬라이드로) | 넷코드가 `teleport` 플래그 → 보간 스킵·즉시 스냅(02 `NoInterpolate` 확장) |
| 로컬 예측 오차 → 서버 보정 | 내 캐릭터가 튐(rubber-banding) | 보정 스무딩: 오차를 수 프레임에 걸쳐 흡수(위치 lerp). 큰 오차만 즉시 스냅 |
| 높은 RTT(300ms)에서 원격 캐릭터 | 과거 시점만 보임(전투 판정 괴리) | 렌더는 과거 보간 유지(부드러움 우선), 판정은 서버 권위([03-server-world](03-server-world.md)). 스킬 이펙트는 로컬 즉시 재생 |
| 엔티티가 AOI 밖으로(서버가 제거) | 갑자기 사라짐 | 페이드아웃(디더/알파) 후 제거. 재진입 시 페이드인 |
| 층 전환(다리 위→아래)이 스냅샷에 지연 반영 | 잘못된 밴드로 그려짐(가림 오류) | floorLevel을 스냅샷에 포함(§4.3) → sortLayer 밴드 즉시 반영. 전환 프레임 1~2개 tolerance |
| 시계 드리프트(서버-클라 시간차) | 보간 시점 오프셋 누적 | 넷코드가 RTT/오프셋 추정 → renderTime 보정. 렌더는 보정된 serverTime 소비 |
| 대량 동시 스폰(레이드 소환) | 프레임 스파이크(트랙 생성·아틀라스 로드) | 스폰 예산(프레임당 N개) + 프리페치된 아틀라스 + 임포스터로 첫 프레임 대체 후 업그레이드 |

### 5.3 월드 스트리밍

| 상황 | 문제 | 대응 |
|---|---|---|
| 존 경계 왕복 이동 | 청크 로드/언로드 채터링 | 히스테리시스(로드 반경 > 언로드 반경). 최근 청크 LRU 유지 |
| 고속 이동(탈것·순간이동) | 프리페치 못 따라감 → 빈 화면 | 속도 기반 프리페치 반경 확대 + 순간이동은 목적지 청크 우선 동기 로드 + 로딩 페이드 |
| 청크 로드 중 IO 지연 | 스톨·프레임 드랍 | 01 JobSystem IO 큐 비동기 + 청크 미도착 시 저해상 프록시/빈 타일 임시 + 완료 시 스왑 |
| 심리스 존 경계 넘어감 | 두 존 동시 렌더 필요 | 경계 존은 미리 additive 로드(03 SceneManager). 좌표계 오프셋 정합 |
| 메모리 예산 초과(오픈월드) | OOM·페이지 스와핑 | LRU 언로드 + 청크 메모리 예산 + 저사양은 스트리밍 반경 축소 |
| 런타임 타일 편집(건설·파괴) | 오토타일·청크 메시 무효화 | 편집 셀 주변 청크 리빌드 훅(신규). 오토타일 룰 런타임 재평가 |
| 존 언로드 중 그 존 엔티티 참조 | 대시보드·미니맵 dangling | 존 핸들 세대 검증. 언로드는 프레임 경계에서 지연 파괴 |

### 5.4 하이브리드 깊이·정렬 (02 핵심 불변식 유지)

| 상황 | 문제 | 대응 |
|---|---|---|
| 수천 엔티티 같은 밴드 sortKeyY 동률 | orderInLayer 타이브레이커 부족 → z-fight 깜빡임 | 02 `kOrderEpsilon` + netEntityId를 안정 2차 키로. 인스턴싱에서도 CPU가 depth 유일화 |
| 큰 3D 석상 뒤 여러 캐릭터 겹침 | AnchorBiased ε 근사 한계 | 02 설계 유지. 밀집 시 실루엣 패스로 가려진 아군 표시 |
| 다리 위 100명·아래 100명 | 밴드 분리는 되나 대량 정렬 비용 | 밴드별 사전 분할 후 밴드 내만 정렬. 밴드 경계 가드(`kBandGuard`) 유지 |
| 반투명 이펙트가 다리 아래 통과 | flat depth 정밀도 한계(02 오픈이슈 D7) | Transparent는 뎁스 테스트만·기록 안 함. 허용 오차 감수 |
| 원격 엔티티 floorLevel 미도착 | 잘못된 밴드 | §5.2 tolerance. 기본 World 0 폴백 |

### 5.5 카메라

| 상황 | 문제 | 대응 |
|---|---|---|
| 존 경계에서 카메라가 존 밖 노출 | 빈 영역·미로드 청크 노출 | worldBounds 클램프(§4.4). 경계 존 프리로드로 노출 대비 |
| 화면 흔들림(폭발·피격)과 픽셀스냅 | 셰이크가 지터로 보임 | 셰이크 오프셋을 `SubpixelResidual`과 합성 → 업스케일 UV로 환원(스냅 유지) |
| 줌 인/아웃 시 픽셀 무결성 | 비정수 줌에서 텍셀 번짐 | 정수 줌 우선. 연속 줌은 sharp-bilinear 옵션(순수 도트는 정수 스텝 스냅) |
| 순간이동·존전환 카메라 점프 | 급격한 화면 이동으로 멀미 | 짧은 트윈 또는 페이드 컷(로딩과 동기) |
| 데드존 안 미세 이동 | 카메라 정지로 캐릭터가 렉트 벗어남(고속) | 데드존 + 룩어헤드 병행. 고속 시 데드존 축소 |
| 다중 뷰포트(미니맵) 좌표 혼선 | Screen↔World가 어느 카메라 기준인지 | 뷰포트별 카메라 스택. WorldToScreen은 뷰포트 컨텍스트 인자 |

### 5.6 오버레이·UI 밀도

| 상황 | 문제 | 대응 |
|---|---|---|
| 이름표 수백 개 겹침 | 가독성 붕괴 | 겹침 회피(밀면), 거리 페이드, 중요도 우선(자신>파티>적>일반), 개수 캡 |
| 데미지 숫자 폭주(광역딜) | 화면 뒤덮음 | 합산 표시("x12 3450"), 상한 캡, 페이드 가속 |
| 말풍선 동시 다수 | 화면 점유 | 최근 N개만, 시간 페이드, 화면 밖 앵커 클램프 |
| 월드앵커가 화면 밖 | 잘못된 위치 그리기 | 화면 밖 컬(오프스크린 마커는 미니맵/화살표로) |
| 초고해상도(4K)에서 오버레이 크기 | 너무 작음/큼 | 오버레이는 네이티브 해상도 렌더 + DPI/UI 스케일 반영(정수배 세계와 분리) |
| 로컬라이즈 텍스트 폭(한글·CJK·라틴) | 이름표 길이 편차 | 폭 측정 후 배경 9-slice 리사이즈. 06 FreeType 셰이핑 |

### 5.7 성능·저사양·고주사율

| 상황 | 문제 | 대응 |
|---|---|---|
| 통합 그래픽·저 VRAM | 필레이트·대역폭 한계 | 저사양 프로파일: 파티클/라이트/오버레이 예산 하향, 포스트 스킵, 스트리밍 반경 축소 |
| 300Hz 모니터 | 렌더 비용 5배 | 렌더는 보간만(시뮬 60Hz 유지). 오버레이/파티클은 프레임률 독립 갱신. 프레임 캡 옵션 |
| 프레임 스파이크(청크·아틀라스 로드) | 히칭 | IO 비동기 + 업로드 예산 분할(프레임당 N) + 임포스터 우선 |
| 알트탭·최소화·해상도 변경 | 스왑체인 리사이즈·컨텍스트 로스 | 01 WindowResized 대응(있음). RT 재생성. 최소화 시 렌더 스킵 |
| VRAM 부족(아틀라스 다수) | 텍스처 생성 실패 | 아틀라스 LRU 언로드 + 폴백 핑크 텍스처(04 갭) + 품질 하향 |
| GPU 타임스탬프로 병목 진단 | DX11 스텁(측정 불가) | RHI 타임스탬프 배선(P2). 라이브 운영 텔레메트리로 프레임 히스토그램 |

### 5.8 치트·악용 (렌더 관점)

| 상황 | 문제 | 대응 |
|---|---|---|
| 벽 투시(월핵) 메모리 스캔 | 안 보여야 할 적 위치 노출 | **서버가 AOI 밖 엔티티를 아예 안 보냄**([03-server-world](03-server-world.md)). 클라는 받은 것만 렌더 — 렌더 차원 방어는 보조 |
| 텍스처/셰이더 개조(투명벽 제거) | 지형 투시 | 근본 방어는 서버 권위. 렌더는 클라 신뢰 안 함 |
| 렌더 오버레이 자동조준(ESP) | 이름표 좌표 훅 | 클라 방어 한계 인정. 서버 이동/판정 검증이 주 방어 |
| 카메라 줌아웃 핵(맵 전체 관찰) | AOI 밖 정보 | 서버 AOI로 애초에 데이터 없음. 클라 줌 상한(zoomMax)은 UX용 |

> 원칙: **렌더는 서버가 보낸 것만 그린다. 클라이언트 렌더 차원의 치트 방어는 보조이며, 근본 방어는 서버 권위·AOI다.** 상세는 [02-netcode](02-netcode-replication.md)·[03-server-world](03-server-world.md).

### 5.9 동시성·스레딩

| 상황 | 문제 | 대응 |
|---|---|---|
| 넷코드 스레드가 스냅샷 push | 렌더 스레드와 데이터 경쟁 | 01 JobSystem·EventBus Enqueue(스레드세이프) 경유. 스냅샷 버퍼는 더블버퍼/락프리 링 |
| 청크 로드 워커 완료 콜백 | GPU 업로드는 메인 스레드만 | 01 `RunOnMainThread` 마샬링(04 Finalize 패턴 재사용) |
| 파티클 시뮬 병렬화 | 결정성·순서 | ParallelFor(01)로 파티클 배치 시뮬. 렌더 제출은 메인 |
| RenderProxyList 이중 버퍼 | 추출/렌더 스레드 분리(02 D9) | 02 RenderWorld 더블버퍼 구조 준비됨 — 본 도메인이 첫 실수요 후보 |

---

## 6. 신규 모듈·파일 제안

기존 `engine/render`·`engine/scene`를 확장하고, MMO 전용 렌더 확장은 `engine/render` 하위에 배치. 게임 런타임 앱은 별도(현재 부재 — [04-gameplay-framework](04-gameplay-framework.md) 및 apps와 협의).

```
engine/render/
  include/mye/render/
    CullingSystem.h        # (신규 P0) 뷰 렉트/청크 프러스텀 컬. RenderExtract 후단 삽입
    SpriteInstance.h       # (신규 P0) SpriteInstanceGpu, 아틀라스별 인스턴스 배칭
    InstancedSpritePass.h  # (신규 P0) DrawIndexedInstanced 경로(하이브리드 뎁스 유지)
    EntityLod.h            # (신규 P1) 거리/화면크기 LOD 선택·애니 샘플 게이팅
    SnapshotInterpolator.h # (신규 P0) 원격 엔티티 스냅샷 보간/외삽(렌더 소비)
    CameraController.h      # (신규 P1) Camera2D 위 데드존·경계·룩어헤드·셰이크·줌
    CameraStack.h          # (신규 P2) 다중 뷰포트(미니맵·분할) 카메라 스택
    Light2DPass.h          # (신규 P1) 라이트버퍼 RGBA16F + 포인트/스팟/앰비언트 합성
    ParticleRenderer.h     # (신규 P1) Transparent 인스턴싱 파티클 렌더(시뮬은 06)
    PostChain.h            # (신규 P2) IPostEffect 체인(블룸·그레이딩 LUT·비네트·CRT)
    WorldOverlay.h         # (신규 P0) 이름표·HP바·데미지플로터·상태아이콘 대량 배칭
    MinimapRenderer.h      # (신규 P1) 미니맵 오프스크린 RT·블립·월드맵
    WorldStreamer.h        # (신규 P1) 청크·존 로드/언로드/프리페치·존 그래프
    QualityScaler.h        # (신규 P1) 파티클·라이트·오버레이·LOD·스트리밍 예산 스케일러
  src/
    ... 각 대응 .cpp ...

engine/render/ (기존 확장)
  HybridRenderer.*   # 인스턴싱 경로 추가, 타일 아틀라스 UV 배선, ID 버퍼 패스
  Camera2D.*         # (변경 최소) 데드존/셰이크는 CameraController가 소유, Camera2D는 그대로
  PixelPerfectTarget.* # sharp-bilinear 비정수배 업스케일 옵션 추가

engine/rhi/src/dx11/
  Dx11Device.cpp     # DrawIndexedInstanced·StructuredBuffer·인스턴스 VB 슬롯 배선(현 스텁)
                     # CopyTextureToBuffer/EnqueueReadback/TryGetReadback 구현(ID픽킹)
                     # WriteTimestamp/ResolveTimestamps 구현(프로파일링)

engine/scene/ (03 협의)
  RenderExtract.h    # RenderItem 확장(§4.1), 뷰 컬링 훅, 원격 엔티티 추출
  tilemap/Tilemap.h  # 청크 스트리밍 경계·런타임 편집 리빌드 훅
```

> RHI 확장(인스턴싱·StructuredBuffer·리드백·타임스탬프)은 02가 이미 계약(인터페이스)을 확정했고 DX11 구현만 비어 있다. **인터페이스 재설계 없이 백엔드 구현만 채우면 된다** — 02 확장 원칙과 정합.

---

## 7. 마일스톤 단계 (작은 검증 가능 단위)

전역 로드맵([docs/00 §7](../00-overview.md))의 M2(하이브리드 핵심)·M6(수직 슬라이스) 이후를 잇는 MMO 렌더 단계. 각 단계는 **눈으로 확인 가능한 데모**로 게이트한다.

| 단계 | 범위 | 완료 데모(검증) |
|---|---|---|
| **CR-M0 배칭·컬링** | RHI 인스턴싱·StructuredBuffer 배선(DX11), 아틀라스 통합, 뷰 렉트 컬링, `InstancedSpritePass`(하이브리드 뎁스 유지) | 한 화면 1,000 스프라이트가 60fps(내부 960×540), 드로우콜 < 아틀라스 수. 다리 위/아래 정렬 불변 |
| **CR-M1 원격 엔티티 보간** | `SnapshotInterpolator`, interpDelay 버퍼, 외삽·teleport 스냅, 보정 스무딩, netEntityId 추출 | 모의 스냅샷(10Hz+지터+로스)으로 원격 캐릭터 100명이 지터 없이 이동. 순간이동은 스냅 |
| **CR-M2 월드 앵커 오버레이** | `WorldOverlay`(이름표·HP바·데미지플로터 풀), 컬링·거리 LOD·겹침 회피, WorldToScreen 배칭 | 500 엔티티에 이름표·HP바 + 초당 수백 데미지 숫자, 프레임 안정. 4K에서 크기 정상 |
| **CR-M3 강화 카메라** | `CameraController`(데드존·룩어헤드·경계 클램프·셰이크·줌 트윈), 셰이크×픽셀스냅 합성 | 존 경계 클램프·피격 셰이크·이동 룩어헤드가 픽셀 무결성 유지하며 동작 |
| **CR-M4 스트리밍** | `WorldStreamer`(청크 로드/언로드/프리페치·히스테리시스), 존 그래프·포탈, 런타임 타일 편집 리빌드 | 넓은 맵을 이동하며 청크가 비동기 스트리밍, 스톨 없음. 존 포탈 이동. 고속 이동 대응 |
| **CR-M5 라이팅·파티클** | `Light2DPass`(포인트/스팟/앰비언트·데이나이트), `ParticleRenderer`(타격·마법·날씨), 블롭 그림자 | 밤 마을에 횃불 조명, 스킬 파티클, 비/눈 날씨. 저사양 프로파일에서 예산 하향 동작 |
| **CR-M6 포스트·픽킹·연출** | `PostChain`(블룸·그레이딩·비네트·CRT), ID 버퍼 픽킹, 팔레트 스왑·아웃라인·디더 페이드·실루엣 | 마우스로 몹 클릭 타겟팅, 염색 장비 색, 타겟 아웃라인, 지붕 진입 디더 페이드 |
| **CR-M7 미니맵·품질·프로파일** | `MinimapRenderer`·월드맵·블립, `QualityScaler`, GPU 타임스탬프 프로파일 | 미니맵에 블립·지형, 저/고사양 자동 스케일, 프레임 히스토그램 텔레메트리 |
| **CR-M8 스케일 강화(백로그)** | 노멀맵 2D 라이팅(MRT), 캐스트 그림자, 다중 뷰포트 분할, 렌더 스레드 분리(02 D9) | 노멀맵 도트 조명, 파티 분할 화면, 렌더 스레드 프레임타임 개선 |

**리스크 노트**: 가장 무거운 단계는 **CR-M0(인스턴싱 배칭 — 현 텍스처런 드로우 구조를 대체)**과 **CR-M1(스냅샷 보간 — 넷코드 계약 의존)**이다. CR-M0가 02의 하이브리드 깊이 불변식을 인스턴싱에서 유지하지 못하면 이후 대량 렌더가 흔들리므로, 다리 위/아래·석상 뒤 정렬 회귀 테스트를 CR-M0 게이트에 포함한다.

---

## 8. 의존성·타 도메인 문서 참조

### 8.1 기존 엔진 설계 문서 (정본 인용)

| 문서 | 본 도메인이 인용/의존하는 것 |
|---|---|
| [docs/00-overview.md](../00-overview.md) | 전역 아키텍처·레이어·마일스톤·확장 원칙 |
| [docs/01-core-platform.md](../01-core-platform.md) | 메인 루프(고정스텝+보간 alpha), JobSystem(IO 큐·ParallelFor·RunOnMainThread), EventBus, FrameAllocator, WindowResized |
| [docs/02-rendering.md](../02-rendering.md) | **좌표계·PPU·레이어 밴드·깊이 함수·PassStage·프록시 계약·픽셀퍼펙트·라이팅/파티클/포스트 설계 스켈레톤 — 전부 정본. 본 문서는 소비·확장만** |
| [docs/03-scene-world.md](../03-scene-world.md) | ECS·RenderExtract·Renderable·Tilemap 청크·FloorLevel·애니메이션 샘플링·SceneManager 스텁 |
| [docs/04-asset-pipeline.md](../04-asset-pipeline.md) | 아틀라스 패킹·텍스처 임포트(포인트·premultiplied·패딩)·GUID·핫리로드 |
| [docs/06-runtime-systems.md](../06-runtime-systems.md) | 파티클 시뮬(FxModule 경계)·FreeType 텍스트 셰이핑(오버레이 텍스트)·INetworkProvider 훅 |
| [docs/07-editor-ui.md](../07-editor-ui.md) | 오프스크린 뷰포트 RT·ImGui 표출·ID 버퍼 픽킹 공유 |

### 8.2 자매 MMORPG 도메인 문서 (상호참조)

| 문서(제안 슬러그) | 경계 관계 |
|---|---|
| [02-netcode-replication.md](02-netcode-replication.md) | **스냅샷 공급자.** 본 문서는 렌더 소비(보간·외삽) 측. 트랜스포트·프로토콜·예측/보정 로직은 그쪽 |
| [03-server-world.md](03-server-world.md) | **AOI/관심관리·서버 권위.** "무엇을 렌더할지"는 서버가 결정. 치트 방어 근본은 그쪽 |
| [04-gameplay-framework.md](04-gameplay-framework.md) | 스탯·전투·상태이상 데이터를 오버레이(HP/버프/캐스팅바)가 소비. 게임 런타임 앱 부트스트랩 |
| [05-ui-social.md](05-ui-social.md) | 인게임 UI 위젯(HUD·채팅·인벤). 본 문서 오버레이(월드앵커)와 HUD(스크린공간)의 경계 |
| [06-asset-streaming.md](06-asset-streaming.md) | 원격 에셋 다운로드·패치·버전드 pak. 청크 스트리밍이 소비하는 에셋 공급 |

### 8.3 의존 방향 요약

```
[06 asset-streaming] ─에셋─> [본 도메인 렌더] <─스냅샷─ [02 netcode] <─권위/AOI─ [03 server-world]
                                    │
                         [04 gameplay] ─데이터─> 오버레이/HUD ─> [05 ui-social]
                                    │
                         [docs/02 렌더 규약·03 씬 추출·01 루프] (엔진 기반, 정본)
```

---

## 이 도메인 요약 3줄

1. **엔진의 하이브리드 단일 뎁스버퍼·픽셀퍼펙트·Camera2D·DepthEncoder는 이미 견고하므로 재작성 없이 재사용하고, MMO 스케일이 요구하는 인스턴싱 배칭·뷰 컬링·LOD·원격 스냅샷 보간·월드 앵커 오버레이·월드 스트리밍·2D 라이팅·파티클·포스트·강화 카메라를 그 위에 얹는다.**
2. **최대 리스크는 현재의 "텍스처런 분할 드로우콜" 구조를 인스턴싱으로 대체하면서 02의 밴드+Y-sort 깊이 불변식(다리 위/아래·석상 뒤)을 수천 엔티티에서 유지하는 것이며, 두 번째는 서버 스냅샷(10~30Hz+지터+로스)을 60~300Hz 렌더로 지터 없이 보간·외삽하는 것이다.**
3. **렌더는 서버가 AOI로 보낸 것만 그린다는 원칙 아래, 치트 방어의 근본은 서버 권위이고 렌더 차원 방어는 보조이며, 저사양~고주사율 스펙트럼은 품질 스케일러(파티클·라이트·오버레이·스트리밍 예산 동적 조절)로 흡수한다.**
