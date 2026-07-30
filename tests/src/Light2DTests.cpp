// Light2DTests.cpp — 2D 라이팅 순수 로직(감쇠·컬링·누적) (docs/02, M7 렌더 잔여)
#include "TestFramework.h"

#include "mye/scene/Light2D.h"

#include <cmath>
#include <vector>

using namespace mye;
using namespace mye::scene;

namespace { bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; } }

MYE_TEST(Light2DAttenuation) {
    // 중심(0) → 1, 반경(4) → 0.
    MYE_EXPECT(Near(LightAttenuation(0.0f, 4.0f), 1.0f));
    MYE_EXPECT(Near(LightAttenuation(4.0f, 4.0f), 0.0f));
    MYE_EXPECT(Near(LightAttenuation(5.0f, 4.0f), 0.0f));   // 밖
    // 반경 절반: (1-0.25)^2 = 0.5625.
    MYE_EXPECT(Near(LightAttenuation(2.0f, 4.0f), 0.5625f));
    // 단조 감소.
    MYE_EXPECT(LightAttenuation(1.0f, 4.0f) > LightAttenuation(2.0f, 4.0f));
    MYE_EXPECT(LightAttenuation(2.0f, 4.0f) > LightAttenuation(3.0f, 4.0f));
    // 반경 0 → 0(안전).
    MYE_EXPECT(Near(LightAttenuation(1.0f, 0.0f), 0.0f));
}

MYE_TEST(Light2DCircleRectIntersect) {
    Rect rc{0.0f, 0.0f, 10.0f, 10.0f};
    // 내부.
    MYE_EXPECT(CircleIntersectsRect(5.0f, 5.0f, 1.0f, rc));
    // 가장자리 밖이지만 반경이 닿음.
    MYE_EXPECT(CircleIntersectsRect(11.0f, 5.0f, 2.0f, rc));
    // 완전히 밖.
    MYE_EXPECT(!CircleIntersectsRect(20.0f, 20.0f, 3.0f, rc));
    // 모서리 근접.
    MYE_EXPECT(CircleIntersectsRect(-1.0f, -1.0f, 2.0f, rc));
    MYE_EXPECT(!CircleIntersectsRect(-3.0f, -3.0f, 2.0f, rc));
}

MYE_TEST(Light2DContributionAndAccumulate) {
    Light2D L;
    L.color = {1.0f, 0.5f, 0.0f, 1.0f};
    L.radius = 4.0f;
    L.intensity = 2.0f;

    // 광원 중심에서 최대 기여(색×intensity).
    LightSample acc;
    AddLightContribution(acc, L, Vec2{0, 0}, Vec2{0, 0});
    MYE_EXPECT(Near(acc.r, 2.0f) && Near(acc.g, 1.0f) && Near(acc.b, 0.0f));

    // 반경 밖 → 기여 0.
    LightSample edge;
    AddLightContribution(edge, L, Vec2{0, 0}, Vec2{5, 0});
    MYE_EXPECT(Near(edge.r, 0.0f));

    // 두 광원 누적(가산).
    LightSample two;
    Light2D w; w.color = Color::White(); w.radius = 4.0f; w.intensity = 1.0f;
    AddLightContribution(two, w, Vec2{0, 0}, Vec2{0, 0});   // +1,1,1
    AddLightContribution(two, w, Vec2{0, 0}, Vec2{0, 0});   // +1,1,1
    MYE_EXPECT(Near(two.r, 2.0f) && Near(two.g, 2.0f) && Near(two.b, 2.0f));

    // 비활성 광원 → 무기여.
    LightSample off; Light2D d = w; d.enabled = false;
    AddLightContribution(off, d, Vec2{0, 0}, Vec2{0, 0});
    MYE_EXPECT(Near(off.r, 0.0f));
}

MYE_TEST(Light2DViewportCulling) {
    std::vector<Vec2>  pos = { {5, 5}, {100, 100}, {-2, 5} };
    std::vector<float> rad = { 1.0f,   3.0f,        3.0f };
    Rect viewport{0, 0, 10, 10};

    std::vector<int> visible;
    CullLights(pos, rad, viewport, visible);
    // 0(내부)·2(반경으로 걸침) 보임, 1(멀리) 컬링.
    MYE_EXPECT(visible.size() == 2);
    MYE_EXPECT(visible[0] == 0 && visible[1] == 2);
}
