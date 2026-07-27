// PersistAccountTests.cpp — 계정 등록·로그인·세션·영속 검증 (docs/mmorpg/03, M10)
#include "TestFramework.h"

#include "mye/persist/AccountStore.h"

#include <filesystem>
#include <string>

using namespace mye;
using namespace mye::persist;

MYE_TEST(AccountRegisterAndLogin) {
    AccountStore store;

    auto reg = store.Register("alice", "hunter2");
    MYE_EXPECT(static_cast<bool>(reg));
    const AccountId aliceId = reg.Value();
    MYE_EXPECT(aliceId != 0);

    // 중복 사용자명 거부.
    MYE_EXPECT(!store.Register("alice", "other"));
    // 빈 값 거부.
    MYE_EXPECT(!store.Register("", "pw"));
    MYE_EXPECT(!store.Register("bob", ""));

    // 로그인 성공 → 토큰 발급 + 세션 유효.
    LoginResult ok = store.Login("alice", "hunter2");
    MYE_EXPECT(ok.ok);
    MYE_EXPECT(ok.accountId == aliceId);
    MYE_EXPECT(!ok.token.empty());
    MYE_EXPECT(store.ValidateSession(ok.token) == aliceId);

    // 잘못된 비밀번호 → 실패.
    LoginResult bad = store.Login("alice", "wrong");
    MYE_EXPECT(!bad.ok);
    MYE_EXPECT(bad.token.empty());

    // 존재하지 않는 사용자 → 실패.
    MYE_EXPECT(!store.Login("nobody", "x").ok);

    // 로그아웃 → 세션 무효.
    store.Logout(ok.token);
    MYE_EXPECT(store.ValidateSession(ok.token) == 0);
}

MYE_TEST(AccountSingleSessionInvalidatesOld) {
    AccountStore store;
    (void)store.Register("carol", "pw");
    LoginResult a = store.Login("carol", "pw");
    LoginResult b = store.Login("carol", "pw");   // 재로그인
    MYE_EXPECT(a.ok && b.ok);
    MYE_EXPECT(a.token != b.token);
    // 단일 세션: 새 토큰만 유효, 옛 토큰 무효.
    MYE_EXPECT(store.ValidateSession(b.token) == b.accountId);
    MYE_EXPECT(store.ValidateSession(a.token) == 0);
}

MYE_TEST(AccountPersistRoundtrip) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_accounts.json").string();
    std::error_code ec; fs::remove(path, ec);

    AccountId daveId = 0;
    {
        AccountStore store;
        (void)store.Register("dave", "secret");
        daveId = store.Register("erin", "pass123").Value();
        MYE_EXPECT(static_cast<bool>(store.SaveToFile(path)));
    }
    {
        AccountStore loaded;
        MYE_EXPECT(static_cast<bool>(loaded.LoadFromFile(path)));
        MYE_EXPECT(loaded.Count() == 2);
        // 로드 후에도 비밀번호 검증(솔트/해시 보존).
        MYE_EXPECT(loaded.Login("dave", "secret").ok);
        MYE_EXPECT(!loaded.Login("dave", "nope").ok);
        LoginResult erin = loaded.Login("erin", "pass123");
        MYE_EXPECT(erin.ok && erin.accountId == daveId);
        // 새 등록은 이어지는 id(nextId 보존).
        auto newId = loaded.Register("frank", "pw");
        MYE_EXPECT(static_cast<bool>(newId));
        MYE_EXPECT(newId.Value() > daveId);
    }
    fs::remove(path, ec);
}
