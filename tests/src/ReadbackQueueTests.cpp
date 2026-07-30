// ReadbackQueueTests.cpp — ID 픽킹 리드백 큐 상태머신 + 좌표 매핑 (docs/02, M7 렌더 잔여)
#include "TestFramework.h"

#include "mye/render/ReadbackQueue.h"

#include <cstdint>

using namespace mye;
using namespace mye::render;

MYE_TEST(ScreenToRenderTargetMapping) {
    // 뷰포트 (100,50)~(100x100 스크린), 내부 RT 960x540.
    Rect vp{100.0f, 50.0f, 200.0f, 100.0f};
    int x = -1, y = -1;

    // 뷰포트 중심 → RT 중심.
    MYE_EXPECT(ScreenToRenderTarget(Vec2{200, 100}, vp, 960, 540, x, y));
    MYE_EXPECT(x == 480 && y == 270);

    // 좌상단.
    MYE_EXPECT(ScreenToRenderTarget(Vec2{100, 50}, vp, 960, 540, x, y));
    MYE_EXPECT(x == 0 && y == 0);

    // 뷰포트 밖 → false.
    MYE_EXPECT(!ScreenToRenderTarget(Vec2{50, 100}, vp, 960, 540, x, y));
    MYE_EXPECT(!ScreenToRenderTarget(Vec2{400, 100}, vp, 960, 540, x, y));

    // 우하단 근처(경계 클램프).
    MYE_EXPECT(ScreenToRenderTarget(Vec2{299, 149}, vp, 960, 540, x, y));
    MYE_EXPECT(x < 960 && y < 540 && x >= 0 && y >= 0);
}

MYE_TEST(ReadbackQueueLatency1) {
    ReadbackQueue<uint32_t> q(1);   // 1프레임 지연

    const uint64_t h = q.Enqueue(42u);
    MYE_EXPECT(q.PendingCount() == 1);

    // 아직 프레임 진행 전 → 준비 안 됨.
    uint64_t id = 0; uint32_t val = 0;
    MYE_EXPECT(!q.Poll(id, val));

    // 1프레임 전진 → 준비 → 폴 성공.
    q.Advance();
    MYE_EXPECT(q.Poll(id, val));
    MYE_EXPECT(id == h && val == 42u);
    MYE_EXPECT(q.PendingCount() == 0);
    MYE_EXPECT(!q.Poll(id, val));   // 비었음
}

MYE_TEST(ReadbackQueueLatency2AndFifo) {
    ReadbackQueue<int> q(2);   // 2프레임 지연

    const uint64_t a = q.Enqueue(10);
    q.Advance();                 // frame 1
    const uint64_t b = q.Enqueue(20);

    uint64_t id = 0; int val = 0;
    // frame 1: a 는 아직(1<2), b 는 방금.
    MYE_EXPECT(!q.Poll(id, val));

    q.Advance();                 // frame 2 → a 준비(2>=2)
    MYE_EXPECT(q.Poll(id, val));
    MYE_EXPECT(id == a && val == 10);   // FIFO: a 먼저
    MYE_EXPECT(!q.Poll(id, val));       // b 아직(2-1=1 < 2)

    q.Advance();                 // frame 3 → b 준비
    MYE_EXPECT(q.Poll(id, val));
    MYE_EXPECT(id == b && val == 20);
    MYE_EXPECT(q.PendingCount() == 0);
}
