// SocialMatchmakingTests.cpp — 던전 매칭(역할 구성·FIFO·다중 그룹) (M12 콘텐츠)
#include "TestFramework.h"

#include "mye/social/Matchmaking.h"

#include <algorithm>

using namespace mye;
using namespace mye::social;

namespace { bool Has(const std::vector<AccountId>& v, AccountId x) { return std::find(v.begin(), v.end(), x) != v.end(); } }

MYE_TEST(MatchmakingFormsGroupOnComposition) {
    Matchmaking mm(1, 1, 3);   // 탱1·힐1·딜3 = 5인

    // 구성 미달 → 그룹 없음.
    mm.Enqueue(1, Role::Tank);
    mm.Enqueue(2, Role::Healer);
    mm.Enqueue(3, Role::Dps);
    MYE_EXPECT(mm.FormGroups().empty());
    MYE_EXPECT(mm.QueueSize() == 3 && mm.InQueue(1));

    // 딜 2명 더 → 구성 충족, 그룹 1개 형성.
    mm.Enqueue(4, Role::Dps);
    mm.Enqueue(5, Role::Dps);
    auto groups = mm.FormGroups();
    MYE_EXPECT(groups.size() == 1);
    MYE_EXPECT(groups[0].Size() == 5);
    MYE_EXPECT(Has(groups[0].tanks, 1) && Has(groups[0].healers, 2) && groups[0].dps.size() == 3);
    // 매칭된 인원은 대기열에서 제거.
    MYE_EXPECT(mm.QueueSize() == 0 && !mm.InQueue(1));

    // 중복 등록 무시.
    mm.Enqueue(1, Role::Tank);
    MYE_EXPECT(!mm.Enqueue(1, Role::Dps) && mm.RoleCount(Role::Tank) == 1);
}

MYE_TEST(MatchmakingFifoAndMultipleGroups) {
    Matchmaking mm(1, 0, 1);   // 단순화: 탱1·딜1 = 2인

    // 2개 그룹 분량 등록(FIFO 순서 확인).
    for (AccountId t : {10u, 11u}) mm.Enqueue(t, Role::Tank);
    for (AccountId d : {20u, 21u, 22u}) mm.Enqueue(d, Role::Dps);

    auto groups = mm.FormGroups();
    MYE_EXPECT(groups.size() == 2);
    // FIFO: 먼저 온 탱10·딜20 → 첫 그룹, 탱11·딜21 → 둘째.
    MYE_EXPECT(Has(groups[0].tanks, 10) && Has(groups[0].dps, 20));
    MYE_EXPECT(Has(groups[1].tanks, 11) && Has(groups[1].dps, 21));
    // 남은 딜 22 는 탱 부족으로 대기 유지.
    MYE_EXPECT(mm.QueueSize() == 1 && mm.InQueue(22));
}

MYE_TEST(MatchmakingCancel) {
    Matchmaking mm(1, 1, 1);
    mm.Enqueue(1, Role::Tank);
    mm.Enqueue(2, Role::Healer);
    mm.Enqueue(3, Role::Dps);

    // 힐러 취소 → 구성 미달로 그룹 안 됨.
    MYE_EXPECT(mm.Cancel(2));
    MYE_EXPECT(!mm.InQueue(2) && mm.RoleCount(Role::Healer) == 0);
    MYE_EXPECT(mm.FormGroups().empty());
    MYE_EXPECT(!mm.Cancel(2));   // 이미 없음

    // 다시 힐러 → 그룹 형성.
    mm.Enqueue(9, Role::Healer);
    MYE_EXPECT(mm.FormGroups().size() == 1);
}
