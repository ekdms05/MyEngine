// mye/render/DepthEncoder.h — 하이브리드 깊이 인코딩 (M2-C 구현) (docs/02 §하이브리드 깊이 정렬)
//
// 02가 소유하는 "단일 깊이 함수". RenderExtract(RenderItem)의 (sortLayer 밴드, sortKeyY,
// orderInLayer)를 NDC 깊이[0,1]로 인코딩한다. 스프라이트/타일 VS가 이 값을 SV_Position.z로 출력하고,
// 3D 메시 패스는 AnchorBiased 모드에서 앵커 지면Y를 이 함수에 태워 스프라이트와 자연 정렬한다.
//
// 깊이 규약(02): NDC 0(near, 화면 앞) ~ 1(far, 화면 뒤). "앞"은 작은 값.
//   depth = LayerBand(sortLayer).base + t × LayerBand(sortLayer).width
//   t = saturate((viewTopY − sortKeyY) / viewRangeY)   // sortKeyY 클수록(화면 위=뒤) t↑ → 더 뒤
//
// ★ 이 문서(02)가 밴드표를 확정한다. M2-B의 RenderExtract.h::kSortLayerWorldBase(placeholder 100)를
//   본 헤더의 FloorLevelToSortLayer로 대체한다(양쪽 상수 정합: kSortLayerWorldBase == 100).
#pragma once

#include "mye/core/Base.h"

#include <cstdint>

namespace mye::render {

// ---------------------------------------------------------------------------
// 레이어 밴드 표 (docs/02 §Screen2D 깊이 함수) — 02 확정본
// ---------------------------------------------------------------------------
// 깊이[0,1]을 밴드로 분할한다. 값이 작을수록 화면 앞(near). "다리 위" 밴드가 "다리 아래" 밴드보다
// 앞에 오도록 World k 밴드는 k가 커질수록 base가 작아진다(위층=더 앞).
//
//   sortLayer            | 밴드(base~base+width)      | 용도
//   ---------------------|---------------------------|-------------------------------
//   BackgroundFar        | 0.95 ~ 1.00               | 원경(순서 그리기, Y-sort 무의미)
//   BackgroundNear       | 0.90 ~ 0.95               | 근경 패럴랙스
//   Ground               | 0.85 ~ 0.90               | 지면 타일(평면 깊이)
//   World 0..N           | 0.20 ~ 0.85를 (N+1)분할   | Y-sort 대상(캐릭터·오브젝트·삽입3D·다리층)
//   OverheadFX           | 0.10 ~ 0.20               | 머리 위 연출(나뭇잎·그림자)
//
// World 밴드: World 0(지상)이 가장 뒤쪽(큰 base), World k(위층)일수록 앞(작은 base).
// 이렇게 하면 "다리 위 A"(World 1)가 "다리 아래 B"(World 0)를 항상 가린다 — 밴드 분리가 곧 해법.

struct LayerBand {
    float base = 0.0f;    // 밴드 시작 깊이(가장 앞)
    float width = 0.0f;   // 밴드 폭(base+width = 가장 뒤)
};

// 고정 밴드 id (sortLayer 값). World 밴드는 kSortLayerWorldBase + k.
enum SortLayerId : uint16_t {
    kSortLayerBackgroundFar  = 0,
    kSortLayerBackgroundNear = 1,
    kSortLayerGround         = 2,
    kSortLayerOverheadFX     = 3,
    // 4..99 예약(추가 고정 밴드).
    kSortLayerWorldBase      = 100,   // World 0 밴드. World k = 100 + k. (RenderExtract.h와 정합)
    kSortLayerWorldMax       = 199,   // World 밴드 상한(World 0..99 지원)
};

// World 밴드 총 개수(0..kWorldBandCount-1). 다리/고가 층 수 + 1(지상). 밴드폭 산정에 사용.
inline constexpr int   kWorldBandCount   = 8;      // 지상 + 최대 7층(다리·고가). 프로젝트 설정 가능.
inline constexpr float kWorldBandTop     = 0.20f;  // World 밴드 전체 상단(가장 앞, 최상위 층)
inline constexpr float kWorldBandBottom  = 0.85f;  // World 밴드 전체 하단(가장 뒤, 지상 World 0)

// sortLayer → 밴드. 알 수 없는 id는 World 0으로 폴백.
LayerBand BandForSortLayer(uint16_t sortLayer);

// ---------------------------------------------------------------------------
// FloorLevel → World 밴드 sortLayer 변환 (RenderExtract.h placeholder 대체 정본)
// ---------------------------------------------------------------------------
// FloorLevel{k} → World k 밴드 sortLayer(kSortLayerWorldBase + k). k는 [0, kWorldBandCount-1] 클램프.
// (RenderExtract.h::FloorLevelToSortLayer가 이 규약의 협의값; 본 함수가 정본. 상수 kSortLayerWorldBase
//  == 100으로 양쪽 일치하므로 M2-B가 채운 sortLayer를 재계산 없이 그대로 소비 가능.)
uint16_t FloorLevelToSortLayer(int8_t floorLevel);

// ---------------------------------------------------------------------------
// 깊이 인코딩
// ---------------------------------------------------------------------------
// 뷰 Y 범위(월드 unit). 카메라 세로 뷰 범위 + 여유. sortKeyY 정규화 분모.
struct DepthViewParams {
    float viewTopY = 0.0f;      // 뷰 상단 월드 Y(가장 뒤). 보통 카메라중심 + 반높이 + 여유.
    float viewRangeY = 1.0f;    // 뷰 세로 범위(월드 unit). >0.
};

// (sortLayer, sortKeyY, orderInLayer) → NDC 깊이[0,1].
//   - 밴드 base+width 안에서 sortKeyY로 선형 배치(sortKeyY 클수록 뒤).
//   - orderInLayer는 밴드 내 아주 작은 미세 바이어스(타이브레이커; 같은 sortKeyY 안정화).
//     밴드 폭을 침범하지 않도록 극소 스텝(kOrderEpsilon)만 적용.
float EncodeDepth(uint16_t sortLayer, float sortKeyY, int16_t orderInLayer,
                  const DepthViewParams& view);

// orderInLayer 1스텝당 깊이 미세 바이어스(order 클수록 앞=작은 depth). 밴드 내 타이브레이커.
inline constexpr float kOrderEpsilon = 1.0e-5f;

// order 바이어스 총량 상한(밴드폭 대비 비율). |order*kOrderEpsilon|을 band.width*이 값으로 하드
// 클램프해, 큰 |order|(캐릭터/타일 밀기 등)라도 밴드폭의 이 비율을 넘지 못하게 한다 → order는
// sortKeyY 순서를 밴드 밖으로 뒤집거나 인접 밴드를 침범할 수 없다(불변식). 0.5 = 밴드 반폭까지.
inline constexpr float kOrderMaxBandFraction = 0.5f;

// 렌더러블 orderInLayer 관례 상수(매직넘버 제거). 밴드폭 대비 정량:
//   캐릭터(+300) → +0.003 depth(밴드폭 0.08125의 ~3.7%)만큼 앞으로 당김.
//   타일/다리 상판(-300) → 같은 폭만큼 뒤로 밀어, 같은 밴드에서 캐릭터가 자연히 앞에 온다.
// 둘의 상대차(0.006)는 '자기 밴드 바닥·상판'을 이기기엔 충분하되, 같은 밴드의 3D 앵커
// 오브젝트(석상, order 0)와는 depth 정렬(sortKeyY)이 우선하도록 절제된 값이다(behind_statue
// 마진 확보). kOrderEpsilon×|order| ≤ band.width×kOrderMaxBandFraction 클램프가 상한을 보증.
inline constexpr int16_t kOrderCharacter = 300;   // 캐릭터: 자기 밴드 지면/상판보다 앞
inline constexpr int16_t kOrderTileGround = -300; // 지면/다리 타일: 같은 밴드에서 뒤로 밀기

// 밴드 경계 가드(반개구간 [base, base+width) 구현). 인접 밴드가 공유 경계 depth로 수렴해
// z-fight 하지 않도록 각 밴드 상한을 이만큼 안쪽으로 당긴다(kOrderEpsilon과 동급의 극소값).
inline constexpr float kBandGuard = 1.0e-5f;

// AnchorBiased 3D 메시: 메시 뷰공간 Z를 [min,max]로 정규화한 n∈[0,1]을 앵커 깊이 주변
// 극소폭에만 매핑한다. depth = anchorDepth + (0.5 − n) × biasEps, biasEps = 밴드폭 ×
// kAnchorBiasBandFraction. '월드 unit당'이 아니라 '메시 정규화 z당' 정의라 메시 Z-두께와
// 무관하게 편차가 ±biasEps/2로 고정 → 두꺼운 메시에서도 인접 밴드/오브젝트 정렬 불침범
// (docs/02 §삽입 3D). 밴드폭의 10%면 편차 ±5% — 앵커 이웃(캐릭터 order 바이어스 등) 마진 내.
inline constexpr float kAnchorBiasBandFraction = 0.10f;

} // namespace mye::render
