// mye/persist/AccountStore.cpp — 계정/세션 구현 (AccountStore.h 참조)
#include "mye/persist/AccountStore.h"

#include "mye/core/Base.h"    // HashFnv1a64
#include "mye/core/Json.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <charconv>      // from_chars (hex 파싱)
#include <system_error>

namespace mye::persist {

namespace {

uint64_t ParseHex(std::string_view sv) {
    uint64_t v = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), v, 16);
    return v;
}

// 결정론 솔티드 해시(스트레칭). [주의] 로직 검증용 — 프로덕션은 argon2/bcrypt.
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

uint64_t DeriveSalt(std::string_view username) {
    // [주의] 결정론 솔트 — 프로덕션은 계정마다 CSPRNG 랜덤 솔트.
    return HashFnv1a64(std::string(username) + "|mye-salt") | 1ull;
}

std::string HexToken(uint64_t a, uint64_t b) {
    return std::format("{:016x}{:016x}", a, b);
}

} // namespace

Expected<AccountId, Error> AccountStore::Register(std::string_view username, std::string_view password) {
    if (username.empty()) return Error{"Register: 사용자명이 비었습니다", 1};
    if (password.empty()) return Error{"Register: 비밀번호가 비었습니다", 2};
    const std::string uname(username);
    if (m_byName.find(uname) != m_byName.end())
        return Error{"Register: 이미 존재하는 사용자명 '" + uname + "'", 3};

    Account acc;
    acc.id = m_nextId++;
    acc.username = uname;
    acc.salt = DeriveSalt(username);
    acc.passwordHash = HashPassword(password, acc.salt);
    m_byName.emplace(uname, acc.id);
    m_accounts.push_back(std::move(acc));
    return m_accounts.back().id;
}

LoginResult AccountStore::Login(std::string_view username, std::string_view password) {
    LoginResult res;
    const Account* acc = FindByName(username);
    if (!acc) { res.error = "존재하지 않는 사용자"; return res; }
    if (acc->banned) { res.error = "차단된 계정"; return res; }
    if (HashPassword(password, acc->salt) != acc->passwordHash) {
        res.error = "비밀번호 불일치";
        return res;
    }
    // 단일 세션: 이 계정의 기존 세션 무효화.
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second == acc->id) it = m_sessions.erase(it);
        else ++it;
    }
    // [주의] 결정론 토큰 — 프로덕션은 CSPRNG.
    const std::string token = HexToken(acc->id, m_sessionCounter++ * 0x9E3779B97F4A7C15ull);
    m_sessions.emplace(token, acc->id);
    res.ok = true;
    res.accountId = acc->id;
    res.token = token;
    return res;
}

AccountId AccountStore::ValidateSession(std::string_view token) const {
    auto it = m_sessions.find(std::string(token));
    return it == m_sessions.end() ? 0 : it->second;
}

void AccountStore::Logout(std::string_view token) {
    m_sessions.erase(std::string(token));
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

Expected<void, Error> AccountStore::SaveToFile(std::string_view path) const {
    json::Value::Array arr;
    for (const Account& a : m_accounts) {
        json::Value::Object o;
        o["id"] = json::Value(static_cast<std::int64_t>(a.id));
        o["username"] = json::Value(a.username);
        // salt/hash는 64비트 — JSON 수치(double)의 정밀도 손실을 피해 hex 문자열로 보관.
        o["salt"] = json::Value(std::format("{:016x}", a.salt));
        o["hash"] = json::Value(std::format("{:016x}", a.passwordHash));
        o["banned"] = json::Value(a.banned);
        arr.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["accounts"] = json::Value(std::move(arr));
    root["nextId"] = json::Value(static_cast<std::int64_t>(m_nextId));
    const std::string text = json::Stringify(json::Value(std::move(root)));

    // 원자적 쓰기(임시 → rename).
    const std::string tmp = std::string(path) + ".tmp";
    { std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
      if (!os) return Error{"SaveToFile: 열기 실패 '" + std::string(path) + "'", 1};
      os.write(text.data(), static_cast<std::streamsize>(text.size()));
      if (!os) return Error{"SaveToFile: 쓰기 실패", 2}; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) return Error{"SaveToFile: rename 실패", 3};
    return {};
}

Expected<void, Error> AccountStore::LoadFromFile(std::string_view path) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return Error{"LoadFromFile: 열기 실패 '" + std::string(path) + "'", 1};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = json::Parse(text);
    if (!parsed) return parsed.GetError();
    const json::Value& root = parsed.Value();

    m_accounts.clear();
    m_byName.clear();
    m_sessions.clear();
    const json::Value* arr = root.Find("accounts");
    if (arr && arr->IsArray()) {
        for (const json::Value& v : arr->AsArray()) {
            if (!v.IsObject()) continue;
            Account a;
            if (const auto* p = v.Find("id")) a.id = static_cast<AccountId>(p->AsInt());
            if (const auto* p = v.Find("username")) a.username = p->AsString();
            if (const auto* p = v.Find("salt")) a.salt = ParseHex(p->AsString());
            if (const auto* p = v.Find("hash")) a.passwordHash = ParseHex(p->AsString());
            if (const auto* p = v.Find("banned")) a.banned = p->AsBool();
            m_byName.emplace(a.username, a.id);
            m_accounts.push_back(std::move(a));
        }
    }
    if (const auto* p = root.Find("nextId")) m_nextId = static_cast<AccountId>(p->AsInt());
    if (m_nextId < 1) m_nextId = 1;
    return {};
}

} // namespace mye::persist
