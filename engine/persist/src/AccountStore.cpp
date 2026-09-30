// mye/persist/AccountStore.cpp — 계정/세션 구현 (AccountStore.h 참조)
#include "mye/persist/AccountStore.h"

#include "JsonFile.h"

#include <format>
#include <span>
#include <bcrypt.h>
#include <charconv>      // from_chars (hex 파싱)

namespace mye::persist {

namespace {

bool ParseHex(std::string_view sv, uint64_t& value) {
    const auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value, 16);
    return sv.size() == 16 && result.ec == std::errc{} && result.ptr == sv.data() + sv.size();
}

// 구형 파일 로그인 검증에만 사용. 신규 계정에는 사용하지 않는다.
uint64_t HashPassword(std::string_view pw, uint64_t salt) {
    uint64_t h = salt ^ 0xcbf29ce484222325ull;
    for (char c : pw) { h ^= static_cast<uint8_t>(c); h *= 0x100000001b3ull; }
    for (int i = 0; i < 4096; ++i) {   // 키 스트레칭(코스트)
        h ^= salt + static_cast<uint64_t>(i);
        h *= 0x100000001b3ull;
        h ^= h >> 27;
    }
    return h;
}

constexpr int32_t kPasswordIterations = 600'000;

std::string HexBytes(std::span<const uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        result[i * 2] = digits[bytes[i] >> 4];
        result[i * 2 + 1] = digits[bytes[i] & 15];
    }
    return result;
}

bool ParseBytes(std::string_view text, std::span<uint8_t> bytes) {
    if (text.size() != bytes.size() * 2) return false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned value = 0;
        const char* begin = text.data() + i * 2;
        const auto parsed = std::from_chars(begin, begin + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != begin + 2) return false;
        bytes[i] = static_cast<uint8_t>(value);
    }
    return true;
}

Expected<void, Error> RandomBytes(std::span<uint8_t> bytes) {
    const NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) return Error{"AccountStore: random generation failed", status};
    return {};
}

Expected<std::array<uint8_t, 32>, Error> DerivePassword(std::string_view password, const Account& account) {
    BCRYPT_ALG_HANDLE raw = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&raw, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status < 0) return Error{"AccountStore: SHA256 provider unavailable", status};
    const auto close = [](void* handle) { BCryptCloseAlgorithmProvider(handle, 0); };
    std::unique_ptr<void, decltype(close)> algorithm(raw, close);
    std::array<uint8_t, 32> digest{};
    status = BCryptDeriveKeyPBKDF2(raw,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(account.passwordSalt.data()), static_cast<ULONG>(account.passwordSalt.size()),
        account.passwordIterations, digest.data(), static_cast<ULONG>(digest.size()), 0);
    if (status < 0) return Error{"AccountStore: password derivation failed", status};
    return digest;
}

Expected<void, Error> SetPassword(Account& account, std::string_view password) {
    Account replacement = account;
    if (auto r = RandomBytes(replacement.passwordSalt); !r) return r.GetError();
    replacement.passwordIterations = kPasswordIterations;
    auto digest = DerivePassword(password, replacement);
    if (!digest) return digest.GetError();
    replacement.passwordDigest = digest.Value();
    replacement.salt = replacement.passwordHash = 0;
    account = std::move(replacement);
    return {};
}

} // namespace

Expected<AccountId, Error> AccountStore::Register(std::string_view username, std::string_view password) {
    if (username.size() > 64 || password.size() > 128) return Error{"Register: credentials too long", 1};
    if (m_nextId >= INT64_MAX) return Error{"Register: id exhausted", 1};
    if (username.empty()) return Error{"Register: 사용자명이 비었습니다", 1};
    if (password.empty()) return Error{"Register: 비밀번호가 비었습니다", 2};
    const std::string uname(username);
    if (m_byName.find(uname) != m_byName.end())
        return Error{"Register: 이미 존재하는 사용자명 '" + uname + "'", 3};

    Account acc;
    acc.id = m_nextId;
    acc.username = uname;
    if (auto r = SetPassword(acc, password); !r) return r.GetError();
    ++m_nextId;
    m_byName.emplace(uname, acc.id);
    m_accounts.push_back(std::move(acc));
    return m_accounts.back().id;
}

LoginResult AccountStore::Login(std::string_view username, std::string_view password) {
    LoginResult res;
    if (username.size() > 64 || password.empty() || password.size() > 128) {
        res.error = "invalid credentials"; return res;
    }
    Account* acc = nullptr;
    for (auto& account : m_accounts) if (account.username == username) { acc = &account; break; }
    if (!acc || acc->banned) { res.error = "invalid credentials"; return res; }
    if (acc->passwordIterations == 0) {
        if (HashPassword(password, acc->salt) != acc->passwordHash) { res.error = "invalid credentials"; return res; }
        if (auto r = SetPassword(*acc, password); !r) { res.error = r.GetError().message; return res; }
    } else {
        auto digest = DerivePassword(password, *acc);
        if (!digest) { res.error = digest.GetError().message; return res; }
        uint8_t difference = 0;
        for (size_t i = 0; i < acc->passwordDigest.size(); ++i)
            difference |= digest.Value()[i] ^ acc->passwordDigest[i];
        if (difference != 0) { res.error = "invalid credentials"; return res; }
    }
    std::array<uint8_t, 32> random{};
    if (auto r = RandomBytes(random); !r) { res.error = r.GetError().message; return res; }
    const std::string token = HexBytes(random);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second.accountId == acc->id || now >= it->second.expiresAt) it = m_sessions.erase(it);
        else ++it;
    }
    m_sessions.emplace(token, Session{acc->id, now + std::chrono::hours(24)});
    res.ok = true;
    res.accountId = acc->id;
    res.token = token;
    return res;
}

AccountId AccountStore::ValidateSession(std::string_view token, std::chrono::steady_clock::time_point now) const {
    auto it = m_sessions.find(std::string(token));
    return it == m_sessions.end() || now >= it->second.expiresAt ? 0 : it->second.accountId;
}

void AccountStore::Logout(std::string_view token) {
    m_sessions.erase(std::string(token));
}

Expected<void, Error> AccountStore::Ban(AccountId id, std::string_view reason) {
    Account* acc = nullptr;
    for (Account& a : m_accounts) if (a.id == id) { acc = &a; break; }
    if (!acc) return Error{"Ban: 존재하지 않는 계정", 1};
    acc->banned = true;
    acc->banReason = std::string(reason);
    // 진행 중 세션 무효화(즉시 로그아웃 효과).
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second.accountId == id) it = m_sessions.erase(it);
        else ++it;
    }
    return {};
}

Expected<void, Error> AccountStore::Unban(AccountId id) {
    for (Account& a : m_accounts) if (a.id == id) { a.banned = false; a.banReason.clear(); return {}; }
    return Error{"Unban: 존재하지 않는 계정", 1};
}

bool AccountStore::IsBanned(AccountId id) const {
    for (const Account& a : m_accounts) if (a.id == id) return a.banned;
    return false;
}

const Account* AccountStore::FindByName(std::string_view username) const {
    auto it = m_byName.find(std::string(username));
    if (it == m_byName.end()) return nullptr;
    return FindById(it->second);
}

const Account* AccountStore::FindById(AccountId id) const {
    for (const Account& a : m_accounts) if (a.id == id) return &a;
    return nullptr;
}

json::Value AccountStore::ToJson() const {
    json::Value::Array arr;
    for (const Account& a : m_accounts) {
        json::Value::Object o;
        o["id"] = json::Value(static_cast<std::int64_t>(a.id));
        o["username"] = json::Value(a.username);
        if (a.passwordIterations == 0) {
            o["salt"] = json::Value(std::format("{:016x}", a.salt));
            o["hash"] = json::Value(std::format("{:016x}", a.passwordHash));
        } else {
            o["algorithm"] = json::Value(std::string("pbkdf2-sha256"));
            o["iterations"] = json::Value(int64_t{a.passwordIterations});
            o["salt"] = json::Value(HexBytes(a.passwordSalt));
            o["hash"] = json::Value(HexBytes(a.passwordDigest));
        }
        o["banned"] = json::Value(a.banned);
        if (!a.banReason.empty()) o["banReason"] = json::Value(a.banReason);
        arr.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["accounts"] = json::Value(std::move(arr));
    root["nextId"] = json::Value(static_cast<std::int64_t>(m_nextId));
    return json::Value(std::move(root));
}

Expected<void, Error> AccountStore::SaveToFile(std::string_view path) const {
    return detail::WriteJsonFile(detail::Utf8Path(path), ToJson());
}

Expected<void, Error> AccountStore::LoadFromFile(std::string_view path) {
    auto parsed = detail::ReadJsonFile(detail::Utf8Path(path));
    if (!parsed) return parsed.GetError();
    return LoadJson(parsed.Value());
}

Expected<void, Error> AccountStore::LoadJson(const json::Value& root) {
    AccountStore candidate;
    candidate.m_accounts.clear();
    candidate.m_byName.clear();
    candidate.m_sessions.clear();
    const json::Value* arr = root.Find("accounts");
    if (!arr || !arr->IsArray()) return Error{"AccountStore: missing accounts array", 1};
    {
        for (const json::Value& v : arr->AsArray()) {
            if (!v.IsObject()) return Error{"AccountStore: invalid record", 1};
            Account a;
            if (!detail::IntegerInRange(v, "id", 1, INT64_MAX - 1))
                return Error{"AccountStore: invalid account id", 1};
            if (const auto* p = v.Find("id")) a.id = static_cast<AccountId>(p->AsInt());
            if (const auto* p = v.Find("username")) a.username = p->AsString();
            const auto* salt = v.Find("salt");
            const auto* hash = v.Find("hash");
            if (!salt || !hash) return Error{"AccountStore: missing password record", 1};
            if (const auto* algorithm = v.Find("algorithm")) {
                if (algorithm->AsString() != "pbkdf2-sha256" ||
                    !detail::IntegerInRange(v, "iterations", kPasswordIterations, 2'000'000) ||
                    !ParseBytes(salt->AsString(), a.passwordSalt) || !ParseBytes(hash->AsString(), a.passwordDigest))
                    return Error{"AccountStore: invalid password record", 1};
                a.passwordIterations = static_cast<int32_t>(v.Find("iterations")->AsInt());
            } else if (!ParseHex(salt->AsString(), a.salt) || !ParseHex(hash->AsString(), a.passwordHash)) {
                return Error{"AccountStore: invalid legacy password record", 1};
            }
            if (const auto* p = v.Find("banned")) {
                if (!p->IsBool()) return Error{"AccountStore: invalid ban flag", 1};
                a.banned = p->AsBool();
            }
            if (const auto* p = v.Find("banReason")) {
                if (!p->IsString()) return Error{"AccountStore: invalid ban reason", 1};
                a.banReason = std::string(p->AsString());
            }
            if (a.username.empty() || a.username.size() > 64 || candidate.FindById(a.id) ||
                !candidate.m_byName.emplace(a.username, a.id).second)
                return Error{"AccountStore: empty or duplicate identity", 1};
            candidate.m_accounts.push_back(std::move(a));
        }
    }
    if (const auto* p = root.Find("nextId")) candidate.m_nextId = static_cast<AccountId>(p->AsInt());
    if (!detail::IntegerInRange(root, "nextId", 1, INT64_MAX))
        return Error{"AccountStore: invalid nextId", 1};
    for (const auto& account : candidate.m_accounts)
        if (account.id >= candidate.m_nextId) return Error{"AccountStore: reused nextId", 1};
    *this = std::move(candidate);
    return {};
}

} // namespace mye::persist
