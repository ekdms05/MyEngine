// mye/persist/AccountStore.h — 계정 등록·로그인·세션 (docs/mmorpg/03, M10)
//
// 윈도우 개발/서버 전제(외부 DB 없이 파일 영속). 계정은 사용자명(고유)+솔티드 비밀번호 해시.
// 로그인 성공 시 세션 토큰 발급. 중복로그인 단일화·재접속의 기반.
//
// [보안 주의] 해시/토큰은 로직 검증용 결정론 구현이다. 프로덕션은 argon2/bcrypt +
//   CSPRNG 토큰으로 교체해야 한다(설계 docs/mmorpg/03).
#pragma once

#include "mye/core/Base.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mye::persist {

using AccountId = uint64_t;

struct Account {
    AccountId   id = 0;
    std::string username;
    uint64_t    salt = 0;
    uint64_t    passwordHash = 0;
    bool        banned = false;
};

struct LoginResult {
    bool        ok = false;
    AccountId   accountId = 0;
    std::string token;      // 세션 토큰(로그인 성공 시)
    std::string error;
};

class AccountStore {
public:
    // 계정 등록. 사용자명 중복·빈 값이면 실패(accountId=0).
    Expected<AccountId, Error> Register(std::string_view username, std::string_view password);

    // 로그인. 성공 시 세션 토큰 발급(기존 세션 무효화 = 단일 세션). 실패 사유는 result.error.
    LoginResult Login(std::string_view username, std::string_view password);

    // 세션 토큰 → 계정 id(0=무효/만료). 재접속·요청 인증에 사용.
    AccountId ValidateSession(std::string_view token) const;

    // 로그아웃(세션 무효화).
    void Logout(std::string_view token);

    // 조회.
    const Account* FindByName(std::string_view username) const;
    const Account* FindById(AccountId id) const;
    size_t Count() const { return m_accounts.size(); }

    // ---- 영속화(계정만 — 세션은 휘발) ----
    Expected<void, Error> SaveToFile(std::string_view path) const;
    Expected<void, Error> LoadFromFile(std::string_view path);

private:
    std::vector<Account>                       m_accounts;
    std::unordered_map<std::string, AccountId> m_byName;
    std::unordered_map<std::string, AccountId> m_sessions;   // token → accountId
    AccountId m_nextId = 1;
    uint64_t  m_sessionCounter = 1;
};

} // namespace mye::persist
