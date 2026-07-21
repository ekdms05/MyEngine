// mye/render/DepthEncoder.cpp — 하이브리드 깊이 인코딩 구현 (docs/02 §하이브리드 깊이 정렬)
#include "mye/render/DepthEncoder.h"

#include <algorithm>

namespace mye::render {

namespace {

inline float Saturate01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// World k 밴드(base~base+width). k=0(지상)이 가장 뒤(큰 base), k↑일수록 앞(작은 base).
// 전체 World 구간 [kWorldBandTop, kWorldBandBottom]을 kWorldBandCount 등분한다.
LayerBand WorldBand(int k) {
    const int   n = kWorldBandCount;
    const int   kc = std::clamp(k, 0, n - 1);
    const float span = (kWorldBandBottom - kWorldBandTop) / static_cast<float>(n);
    // k=0 → 가장 뒤 슬롯(하단), k=n-1 → 가장 앞 슬롯(상단).
    const float base = kWorldBandTop + span * static_cast<float>(n - 1 - kc);
    return {base, span};
}

} // namespace

LayerBand BandForSortLayer(uint16_t sortLayer) {
    switch (sortLayer) {
        case kSortLayerBackgroundFar:  return {0.95f, 0.05f};
        case kSortLayerBackgroundNear: return {0.90f, 0.05f};
        case kSortLayerGround:         return {0.85f, 0.05f};
        case kSortLayerOverheadFX:     return {0.10f, 0.10f};
        default: break;
    }
    if (sortLayer >= kSortLayerWorldBase && sortLayer <= kSortLayerWorldMax) {
        return WorldBand(static_cast<int>(sortLayer - kSortLayerWorldBase));
    }
    // 알 수 없는 id → World 0 폴백.
    return WorldBand(0);
}

uint16_t FloorLevelToSortLayer(int8_t floorLevel) {
    int k = floorLevel < 0 ? 0 : static_cast<int>(floorLevel);
    k = std::clamp(k, 0, kWorldBandCount - 1);
    return static_cast<uint16_t>(kSortLayerWorldBase + k);
}

float EncodeDepth(uint16_t sortLayer, float sortKeyY, int16_t orderInLayer,
                  const DepthViewParams& view) {
    const LayerBand band = BandForSortLayer(sortLayer);

    // sortKeyY 정규화: viewTopY(가장 뒤=밴드 뒤쪽) 기준. sortKeyY가 클수록(화면 위=뒤) t↑ →
    // 밴드 뒤쪽(깊이 큼). viewBottomY = viewTopY - range(화면 아래=앞). 큰 sortKeyY = 큰 t = 뒤.
    // (이전 구현은 (viewTopY - sortKeyY)로 부호가 반대여서 화면 아래 캐릭터가 오히려 뒤로 밀렸다 —
    //  M2-C 통합에서 발견·수정. 문서 규약 "sortKeyY 클수록 t↑"과 정합.)
    const float range = view.viewRangeY > 1.0e-4f ? view.viewRangeY : 1.0e-4f;
    const float viewBottomY = view.viewTopY - range;
    const float t = Saturate01((sortKeyY - viewBottomY) / range);

    // orderInLayer: 밴드 내부 극소 타이브레이커(큰 order = 아주 약간 앞으로 당김 → 나중에 그림 우선).
    // 부호: order 클수록 앞(작은 depth). 밴드 폭을 침범하지 않도록 kOrderEpsilon만 적용.
    float depth = band.base + t * band.width - static_cast<float>(orderInLayer) * kOrderEpsilon;

    // 밴드 경계 클램프(인접 밴드 침범 방지).
    const float lo = band.base;
    const float hi = band.base + band.width;
    return std::clamp(depth, lo, hi);
}

} // namespace mye::render
