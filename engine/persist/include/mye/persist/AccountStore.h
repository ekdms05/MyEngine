// mye/persist/AccountStore.h — 계정 등록·로그인·세션 (docs/mmorpg/03, M10)
//
// 윈도우 개발/서버 전제(외부 DB 없이 파일 영속). 계정은 사용자명(고유)+솔티드 비밀번호 해시.
// 로그인 성공 시 세션 토큰 발급. 중복로그인 단일화·재접속의 기반.
//
// Windows CNG PBKDF2-SHA256, 계정별 무작위 salt, 만료되는 무작위 세션 토큰.
// 구형 해시는 로그인 성공 시 교체한다. 전송 암호화는 별도 네트워크 경계의 책임이다.
#pragma once

#include "mye/core/Base.h"
#include "mye/core/Json.h"

#include <cstdint>
#include <array>
#include <chrono>
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
    std::array<uint8_t, 16> passwordSalt{};
    std::array<uint8_t, 32> passwordDigest{};
    int32_t     passwordIterations = 0;   // 0 = 구형 파일의 읽기·마이그레이션 전용
    bool        banned = false;
    std::string banReason;   // 제재 사유(GM 감사용)
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
    AccountId ValidateSession(std::string_view token,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

    // 로그아웃(세션 무효화).
    void Logout(std::string_view token);

    // ---- 제재(GM) ----
    // 계정 차단: 이후 로그인 거부 + 기존 세션 무효화. 없는 계정이면 실패.
    Expected<void, Error> Ban(AccountId id, std::string_view reason = {});
    // 차단 해제.
    Expected<void, Error> Unban(AccountId id);
    bool IsBanned(AccountId id) const;

    // 조회.
    const Account* FindByName(std::string_view username) const;
    const Account* FindById(AccountId id) const;
    size_t Count() const { return m_accounts.size(); }

    // ---- 영속화(계정만 — 세션은 휘발) ----
    json::Value ToJson() const;
    Expected<void, Error> LoadJson(const json::Value& root);
    Expected<void, Error> SaveToFile(std::string_view path) const;
    Expected<void, Error> LoadFromFile(std::string_view path);

private:
    std::vector<Account>                       m_accounts;
    std::unordered_map<std::string, AccountId> m_byName;
    struct Session {
        AccountId accountId;
        std::chrono::steady_clock::time_point expiresAt;
    };
    std::unordered_map<std::string, Session> m_sessions;
    AccountId m_nextId = 1;
};

} // namespace mye::persist
