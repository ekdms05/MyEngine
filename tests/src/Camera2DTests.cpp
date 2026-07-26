// Camera2DTests.cpp — 강화 카메라(경계 클램프·데드존 팔로우·셰이크) 검증 (docs/mmorpg/01, M7)
//
// 순수 수학(GPU 불필요). 픽킹(WorldToScreen/ScreenToWorld)이 흔들림에 영향받지 않음도 확인.
#include "TestFramework.h"

#include "mye/render/Camera2D.h"
#include "mye/core/Math.h"

#include <cmath>

using namespace mye;
using mye::render::Camera2D;
using mye::render::Camera2DDesc;

namespace {
Camera2D MakeCam() {
    Camera2DDesc d;            // 기본 뷰포트 960×540, zoom 1 → halfView = 10 × 5.625 unit
    d.pixelSnap = false;       // 스냅 없이 논리값 그대로 비교
    return Camera2D(d);
}
} // namespace

MYE_TEST(CameraWorldBoundsClamp) {
    Camera2D cam = MakeCam();
    cam.SetWorldBounds(Rect{0.0f, 0.0f, 100.0f, 100.0f});   // minX=10 maxX=90, minY=5.625 maxY=94.375

    cam.SetPosition(Vec2{50.0f, 50.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 50.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 50.0f));

    cam.SetPosition(Vec2{1000.0f, 1000.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 90.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 94.375f));

    cam.SetPosition(Vec2{-1000.0f, -1000.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 10.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 5.625f));

    // 뷰보다 작은 경계 → 축 중앙 고정.
    cam.SetWorldBounds(Rect{0.0f, 0.0f, 4.0f, 4.0f});
    cam.SetPosition(Vec2{100.0f, 100.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 2.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 2.0f));
}

MYE_TEST(CameraFollowDeadzone) {
    Camera2D cam = MakeCam();
    cam.SetPosition(Vec2{0.0f, 0.0f});

    // 데드존 안 → 이동 없음.
    cam.FollowDeadzone(Vec2{1.5f, -1.0f}, Vec2{2.0f, 2.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 0.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 0.0f));

    // +X로 데드존 초과 → 초과분만 이동.
    cam.FollowDeadzone(Vec2{5.0f, 0.0f}, Vec2{2.0f, 2.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 3.0f));   // 5 - 2
    MYE_EXPECT(ApproxEqual(cam.Position().y, 0.0f));

    // -Y로 초과 → y 이동.
    cam.FollowDeadzone(Vec2{3.0f, -5.0f}, Vec2{2.0f, 2.0f});
    MYE_EXPECT(ApproxEqual(cam.Position().x, 3.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, -3.0f));  // -5 + 2
}

MYE_TEST(CameraShakeDecaysAndBounded) {
    Camera2D cam = MakeCam();
    cam.SetPosition(Vec2{7.0f, 9.0f});

    MYE_EXPECT(!cam.IsShaking());
    MYE_EXPECT(ApproxEqual(cam.ShakeOffset().x, 0.0f) && ApproxEqual(cam.ShakeOffset().y, 0.0f));

    cam.AddShake(3.0f, 0.5f);
    MYE_EXPECT(cam.IsShaking());

    // 진행 중 오프셋은 항상 진폭 이하(감쇠).
    cam.TickShake(0.25f);   // elapsed 0.25, decay 0.5 → |offset| ≤ 1.5
    const Vec2 o = cam.ShakeOffset();
    MYE_EXPECT(std::fabs(o.x) <= 1.5f + 1e-4f);
    MYE_EXPECT(std::fabs(o.y) <= 1.5f + 1e-4f);

    // 흔들림은 논리 위치를 바꾸지 않는다(픽킹 불변).
    MYE_EXPECT(ApproxEqual(cam.Position().x, 7.0f));
    MYE_EXPECT(ApproxEqual(cam.Position().y, 9.0f));

    // 지속시간 경과 → 종료 + 오프셋 0.
    cam.TickShake(0.5f);    // elapsed 0.75 > 0.5
    MYE_EXPECT(!cam.IsShaking());
    MYE_EXPECT(ApproxEqual(cam.ShakeOffset().x, 0.0f) && ApproxEqual(cam.ShakeOffset().y, 0.0f));
}

MYE_TEST(CameraPickingUnaffectedByShake) {
    Camera2D cam = MakeCam();
    cam.SetPosition(Vec2{0.0f, 0.0f});
    const Vec2 before = cam.ScreenToWorld(Vec2{480.0f, 270.0f});   // 화면 중앙 → 월드 원점
    cam.AddShake(5.0f, 1.0f);
    cam.TickShake(0.1f);
    const Vec2 after = cam.ScreenToWorld(Vec2{480.0f, 270.0f});
    MYE_EXPECT(ApproxEqual(before.x, after.x));
    MYE_EXPECT(ApproxEqual(before.y, after.y));
}
