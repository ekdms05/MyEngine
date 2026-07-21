// AudioTypes.cpp — 거리 감쇠·패닝 계산 (docs/06 §4, M3-B)
#include "mye/audio/AudioTypes.h"

namespace mye::audio {

float ComputeAttenuation(Vec2 listener, const SpatialParams& sp) {
    const Vec2 d = sp.worldPos - listener;
    const float dist = d.Length();
    if (dist <= sp.minDistance) return 1.0f;
    if (sp.maxDistance <= sp.minDistance) return sp.minGain;   // 퇴화 구간
    if (dist >= sp.maxDistance) return sp.minGain;
    // [minDistance,maxDistance] 선형 → [1,minGain].
    const float t = (dist - sp.minDistance) / (sp.maxDistance - sp.minDistance);
    return Lerp(1.0f, sp.minGain, t);
}

float ComputePan(Vec2 listener, const SpatialParams& sp) {
    if (sp.panScale <= 0.0f) return 0.0f;
    const float dx = sp.worldPos.x - listener.x;
    return Clamp(dx / sp.panScale, -1.0f, 1.0f);
}

} // namespace mye::audio
