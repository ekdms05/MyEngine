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

MYE_TEST(AccountBanBlocksLoginAndPersists) {
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mye_accounts_ban.json").string();
    std::error_code ec; fs::remove(path, ec);

    AccountStore store;
    const AccountId id = store.Register("griefer", "pw").Value();

    // 정상 로그인 → 세션 발급.
    LoginResult ok = store.Login("griefer", "pw");
    MYE_EXPECT(ok.ok);
    MYE_EXPECT(store.ValidateSession(ok.token) == id);

    // 밴: 이후 로그인 거부 + 기존 세션 즉시 무효화.
    MYE_EXPECT(static_cast<bool>(store.Ban(id, "exploit")));
    MYE_EXPECT(store.IsBanned(id));
    MYE_EXPECT(store.ValidateSession(ok.token) == 0);      // 세션 무효화
    LoginResult blocked = store.Login("griefer", "pw");
    MYE_EXPECT(!blocked.ok);                               // 로그인 거부

    // 없는 계정 밴/해제는 실패.
    MYE_EXPECT(!store.Ban(9999, "x"));
    MYE_EXPECT(!store.Unban(9999));

    // 영속 후에도 밴 유지(사유 포함).
    MYE_EXPECT(static_cast<bool>(store.SaveToFile(path)));
    AccountStore loaded;
    MYE_EXPECT(static_cast<bool>(loaded.LoadFromFile(path)));
    MYE_EXPECT(loaded.IsBanned(id));
    MYE_EXPECT(loaded.FindById(id)->banReason == "exploit");
    MYE_EXPECT(!loaded.Login("griefer", "pw").ok);

    // 해제 → 다시 로그인 가능.
    MYE_EXPECT(static_cast<bool>(loaded.Unban(id)));
    MYE_EXPECT(!loaded.IsBanned(id));
    MYE_EXPECT(loaded.Login("griefer", "pw").ok);

    fs::remove(path, ec);
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

MYE_TEST(AccountUsesRandomSaltsAndExpiringSessions) {
    AccountStore store;
    const auto first = store.Register("first", "same-password").Value();
    const auto second = store.Register("second", "same-password").Value();
    const auto* a = store.FindById(first);
    const auto* b = store.FindById(second);
    MYE_EXPECT(a->passwordIterations >= 600'000);
    MYE_EXPECT(a->passwordSalt != b->passwordSalt);
    MYE_EXPECT(a->passwordDigest != b->passwordDigest);
    const auto login = store.Login("first", "same-password");
    MYE_EXPECT(login.ok && login.token.size() == 64);
    MYE_EXPECT(store.ValidateSession(login.token, std::chrono::steady_clock::now() + std::chrono::hours(25)) == 0);
    MYE_EXPECT(!store.Register("oversized", std::string(129, 'x')));
}

MYE_TEST(AccountLegacyLoginMigratesPasswordAndRejectsInvalidBanFlag) {
    // Fixture from the previous file format: password "legacy-pass", salt 0x1234.
    auto parsed = json::Parse(R"({"nextId":2,"accounts":[{"id":1,"username":"legacy",
        "salt":"0000000000001234","hash":"1d5f868ffda34e5d","banned":false}]})");
    MYE_EXPECT(parsed);
    if (!parsed) return;
    AccountStore store;
    MYE_EXPECT(store.LoadJson(parsed.Value()));
    MYE_EXPECT(!store.Login("legacy", "wrong").ok);
    MYE_EXPECT(store.Login("legacy", "legacy-pass").ok);
    MYE_EXPECT(store.FindById(1)->passwordIterations == 600'000);
    const auto migrated = store.ToJson();
    AccountStore reloaded;
    MYE_EXPECT(reloaded.LoadJson(migrated));
    MYE_EXPECT(reloaded.Login("legacy", "legacy-pass").ok);
    auto invalid = parsed.Value().AsObject();
    auto accounts = invalid["accounts"].AsArray();
    auto account = accounts[0].AsObject();
    account["banned"] = json::Value(std::string("false"));
    accounts[0] = json::Value(std::move(account));
    invalid["accounts"] = json::Value(std::move(accounts));
    MYE_EXPECT(!reloaded.LoadJson(json::Value(std::move(invalid))));
    MYE_EXPECT(reloaded.FindById(1)->passwordIterations == 600'000);
}
