// NetInterpolatorTests.cpp — 원격 엔티티 스냅샷 보간 (docs/mmorpg/02, M9)
//
// SnapshotInterpolator 순수 로직 검증: 두 스냅샷 사이 선형 보간, 경계(이전/이후) 홀드,
// 자기 netId 제외, 등장/사멸 엔티티 처리, 버퍼 용량·재순서 방어. 소켓 무관 — 결정론.
#include "TestFramework.h"

#include "mye/net/SnapshotInterpolator.h"

#include <cmath>
#include <vector>

using namespace mye;
using namespace mye::net;

namespace {

std::vector<EntitySnap> MakeSnap(std::initializer_list<EntitySnap> ents) { return ents; }

// out 에서 netId 찾기(없으면 nullptr).
const EntitySnap* FindEnt(const std::vector<EntitySnap>& out, uint32_t netId) {
    for (const EntitySnap& e : out) if (e.netId == netId) return &e;
    return nullptr;
}

} // namespace

// 두 스냅샷 사이 정확히 중간 → 위치 절반(선형 보간).
MYE_TEST(NetInterpLerpsBetweenSnapshots) {
    SnapshotInterpolator interp;
    interp.Push(0,   MakeSnap({EntitySnap{1, 0.0f, 0.0f, 0}}));
    interp.Push(200, MakeSnap({EntitySnap{1, 10.0f, 20.0f, 5}}));

    std::vector<EntitySnap> out;
    interp.Sample(100, 0, out);
    const EntitySnap* e = FindEnt(out, 1);
    MYE_EXPECT(e != nullptr);
    if (e) {
        MYE_EXPECT_NEAR(e->x, 5.0f, 1e-4f);
        MYE_EXPECT_NEAR(e->y, 10.0f, 1e-4f);
    }
}

// 스냅샷 1개뿐(또는 표시 시각이 마지막 이후) → 외삽 없이 마지막 값 홀드.
MYE_TEST(NetInterpHoldsAtBoundaries) {
    SnapshotInterpolator interp;
    interp.Push(0,   MakeSnap({EntitySnap{1, 3.0f, 4.0f, 0}}));
    interp.Push(100, MakeSnap({EntitySnap{1, 5.0f, 8.0f, 2}}));

    std::vector<EntitySnap> out;
    interp.Sample(1000, 0, out);   // 마지막 이후
    const EntitySnap* e = FindEnt(out, 1);
    MYE_EXPECT(e != nullptr);
    if (e) { MYE_EXPECT_NEAR(e->x, 5.0f, 1e-4f); MYE_EXPECT_NEAR(e->y, 8.0f, 1e-4f); }

    interp.Sample(0, 0, out);      // 정확히 첫 프레임 시각
    e = FindEnt(out, 1);
    MYE_EXPECT(e != nullptr);
    if (e) { MYE_EXPECT_NEAR(e->x, 3.0f, 1e-4f); MYE_EXPECT_NEAR(e->y, 4.0f, 1e-4f); }
}

// 첫 프레임 이전 표시 시각 → 역외삽 없이 첫 프레임 값 홀드.
MYE_TEST(NetInterpClampsBeforeFirstFrame) {
    SnapshotInterpolator interp;
    interp.Push(100, MakeSnap({EntitySnap{1, 2.0f, 2.0f, 0}}));
    interp.Push(200, MakeSnap({EntitySnap{1, 4.0f, 4.0f, 0}}));

    std::vector<EntitySnap> out;
    interp.Sample(0, 0, out);
    const EntitySnap* e = FindEnt(out, 1);
    MYE_EXPECT(e != nullptr);
    if (e) { MYE_EXPECT_NEAR(e->x, 2.0f, 1e-4f); MYE_EXPECT_NEAR(e->y, 2.0f, 1e-4f); }
}

// 자기 netId(skipNetId)는 결과에서 제외 — 자기 몸은 클라 예측이 담당.
MYE_TEST(NetInterpSkipsOwnEntity) {
    SnapshotInterpolator interp;
    interp.Push(0,   MakeSnap({EntitySnap{7, 0.0f, 0.0f, 0}, EntitySnap{9, 1.0f, 1.0f, 0}}));
    interp.Push(100, MakeSnap({EntitySnap{7, 2.0f, 0.0f, 0}, EntitySnap{9, 1.0f, 3.0f, 0}}));

    std::vector<EntitySnap> out;
    interp.Sample(50, 7, out);
    MYE_EXPECT(FindEnt(out, 7) == nullptr);
    const EntitySnap* e = FindEnt(out, 9);
    MYE_EXPECT(e != nullptr);
    if (e) MYE_EXPECT_NEAR(e->y, 2.0f, 1e-4f);
}

// b에만 있는 엔티티(새로 등장)는 b 값, a에만 있는(사라지는 중)은 a 값을 홀드.
MYE_TEST(NetInterpAppearanceAndDisappearance) {
    SnapshotInterpolator interp;
    interp.Push(0,   MakeSnap({EntitySnap{1, 0.0f, 0.0f, 0}, EntitySnap{2, 6.0f, 6.0f, 0}}));
    interp.Push(100, MakeSnap({EntitySnap{1, 10.0f, 10.0f, 0}, EntitySnap{3, -4.0f, 0.0f, 0}}));

    std::vector<EntitySnap> out;
    interp.Sample(50, 0, out);

    const EntitySnap* e3 = FindEnt(out, 3);   // 새 등장 → b 값 그대로
    MYE_EXPECT(e3 != nullptr);
    if (e3) MYE_EXPECT_NEAR(e3->x, -4.0f, 1e-4f);

    const EntitySnap* e2 = FindEnt(out, 2);   // 사라지는 중 → a 값 홀드
    MYE_EXPECT(e2 != nullptr);
    if (e2) MYE_EXPECT_NEAR(e2->x, 6.0f, 1e-4f);
}

// 용량 초과 푸시 → 오래된 프레임 제거, 재순서(과거 시각) 푸시 → 무시.
MYE_TEST(NetInterpCapacityAndReorderDefense) {
    SnapshotInterpolator interp(3);
    interp.Push(100, MakeSnap({EntitySnap{1, 1.0f, 0.0f, 0}}));
    interp.Push(200, MakeSnap({EntitySnap{1, 2.0f, 0.0f, 0}}));
    interp.Push(300, MakeSnap({EntitySnap{1, 3.0f, 0.0f, 0}}));
    interp.Push(400, MakeSnap({EntitySnap{1, 4.0f, 0.0f, 0}}));   // cap 3 → 100 제거
    MYE_EXPECT(interp.BufferedCount() == 3);
    MYE_EXPECT(interp.NewestTimeMs() == 400);

    interp.Push(350, MakeSnap({EntitySnap{1, 99.0f, 0.0f, 0}}));  // 과거 → 무시
    MYE_EXPECT(interp.NewestTimeMs() == 400);

    std::vector<EntitySnap> out;
    interp.Sample(0, 0, out);   // 남은 가장 오래된 프레임 = 200
    const EntitySnap* e = FindEnt(out, 1);
    MYE_EXPECT(e != nullptr);
    if (e) MYE_EXPECT_NEAR(e->x, 2.0f, 1e-4f);
}
