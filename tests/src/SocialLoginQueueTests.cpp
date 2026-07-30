// SocialLoginQueueTests.cpp — 로그인 대기열(순번·용량·우선순위·admit) (M12 소셜)
#include "TestFramework.h"

#include "mye/social/LoginQueue.h"

using namespace mye;
using namespace mye::social;

MYE_TEST(LoginQueueCapacityAndAdmit) {
    LoginQueue q(2);   // 동접 2

    MYE_EXPECT(q.Request(1) == LoginQueue::Result::Admitted);
    MYE_EXPECT(q.Request(2) == LoginQueue::Result::Admitted);
    MYE_EXPECT(q.ActiveCount() == 2);

    // 초과 → 대기열.
    MYE_EXPECT(q.Request(3) == LoginQueue::Result::Queued);
    MYE_EXPECT(q.Request(4) == LoginQueue::Result::Queued);
    MYE_EXPECT(q.QueueLength() == 2);
    MYE_EXPECT(q.Position(3) == 1 && q.Position(4) == 2);   // FIFO 순번
    MYE_EXPECT(q.Position(99) == 0);                        // 대기 아님

    // 중복 요청.
    MYE_EXPECT(q.Request(1) == LoginQueue::Result::AlreadyActive);
    MYE_EXPECT(q.Request(3) == LoginQueue::Result::AlreadyQueued);

    // 활성 하나 로그아웃 → admit 으로 대기열 앞(3) 입장.
    q.Release(1);
    auto admitted = q.Admit();
    MYE_EXPECT(admitted.size() == 1 && admitted[0] == 3);
    MYE_EXPECT(q.IsActive(3) && !q.IsQueued(3));
    MYE_EXPECT(q.Position(4) == 1);   // 4 가 앞으로 당겨짐
    MYE_EXPECT(q.ActiveCount() == 2);
}

MYE_TEST(LoginQueuePriority) {
    LoginQueue q(1);
    MYE_EXPECT(q.Request(1) == LoginQueue::Result::Admitted);   // 슬롯 참

    // 일반 우선순위 순서로 대기.
    q.Request(10, 0);
    q.Request(11, 0);
    // 높은 우선순위(VIP)는 앞으로.
    q.Request(20, 5);
    MYE_EXPECT(q.Position(20) == 1);   // 우선순위 최상위
    MYE_EXPECT(q.Position(10) == 2 && q.Position(11) == 3);

    // 슬롯 나면 우선순위 높은 20 먼저 admit.
    q.Release(1);
    auto a = q.Admit();
    MYE_EXPECT(a.size() == 1 && a[0] == 20);
}

MYE_TEST(LoginQueueCancel) {
    LoginQueue q(0);   // 용량 0 → 전원 대기(점검/만석 시나리오)
    q.Request(1); q.Request(2); q.Request(3);
    MYE_EXPECT(q.QueueLength() == 3 && q.Position(2) == 2);

    // 2 취소 → 3 이 앞으로.
    q.Cancel(2);
    MYE_EXPECT(!q.IsQueued(2) && q.QueueLength() == 2);
    MYE_EXPECT(q.Position(1) == 1 && q.Position(3) == 2);

    // 용량 늘리고 admit → 순서대로 입장.
    q.SetCapacity(5);
    auto a = q.Admit();
    MYE_EXPECT(a.size() == 2 && a[0] == 1 && a[1] == 3);
    MYE_EXPECT(q.QueueLength() == 0);
}
