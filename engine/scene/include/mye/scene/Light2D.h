// mye/scene/Light2D.h — 2D 점광원 컴포넌트 + 감쇠/컬링/누적 (docs/02, M7 렌더 잔여)
//
// 라이트 누적 버퍼 패스의 GPU 셰이더가 쓸 것과 동일한 순수 로직(감쇠·원-사각형 컬링·앰비언트
// 누적)을 CPU 측에 두어 결정론·단위 테스트한다. 실제 RGBA16F 누적 패스 배선은 GPU 후속.
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mye::scene {

// 2D 점광원 컴포넌트. 위치는 추출 시 엔티티 WorldTransform + offset 으로 결정.
struct Light2D {
    MYE_COMPONENT(Light2D);
    Vec2  offset{0.0f, 0.0f};       // 엔티티 기준 로컬 오프셋
    float radius = 3.0f;            // 영향 반경(world unit)
    Color color = Color::White();
    float intensity = 1.0f;
    bool  enabled = true;
};

// 점광원 감쇠: 거리 dist, 반경 radius → [0,1]. dist>=radius 면 0. 부드러운 이차 폴오프.
inline float LightAttenuation(float dist, float radius) {
    if (radius <= 0.0f) return 0.0f;
    if (dist <= 0.0f) return 1.0f;
    if (dist >= radius) return 0.0f;
    const float x = dist / radius;      // 0..1
    const float f = 1.0f - x * x;       // 이차
    return f * f;                        // 가장자리에서 부드럽게 0
}

// 원(중심 cx,cy 반경 r)이 사각형과 교차하는가(뷰포트 컬링).
inline bool CircleIntersectsRect(float cx, float cy, float r, const Rect& rc) {
    const float nx = std::max(rc.x, std::min(cx, rc.x + rc.w));
    const float ny = std::max(rc.y, std::min(cy, rc.y + rc.h));
    const float dx = cx - nx, dy = cy - ny;
    return (dx * dx + dy * dy) <= r * r;
}

// 라이트 누적 샘플(선형 RGB, 셰이더가 톤매핑). 앰비언트에서 시작해 광원 기여를 더한다.
struct LightSample { float r = 0.0f, g = 0.0f, b = 0.0f; };

// 한 광원(월드 위치 lightPos)이 samplePos 에 더하는 기여를 acc 에 누적.
inline void AddLightContribution(LightSample& acc, const Light2D& L, Vec2 lightPos, Vec2 samplePos) {
    if (!L.enabled) return;
    const float ddx = samplePos.x - lightPos.x;
    const float ddy = samplePos.y - lightPos.y;
    const float dist = std::sqrt(ddx * ddx + ddy * ddy);
    const float a = LightAttenuation(dist, L.radius) * L.intensity;
    acc.r += L.color.r * a;
    acc.g += L.color.g * a;
    acc.b += L.color.b * a;
}

// 라이트 뷰포트 컬링: 원이 뷰포트와 교차하는 광원 인덱스만 out 에 채운다.
inline void CullLights(const std::vector<Vec2>& positions, const std::vector<float>& radii,
                       const Rect& viewport, std::vector<int>& out) {
    out.clear();
    const size_t n = std::min(positions.size(), radii.size());
    for (size_t i = 0; i < n; ++i)
        if (CircleIntersectsRect(positions[i].x, positions[i].y, radii[i], viewport))
            out.push_back(static_cast<int>(i));
}

} // namespace mye::scene
