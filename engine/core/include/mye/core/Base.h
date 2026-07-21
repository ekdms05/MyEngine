// mye/core/Base.h — 무의존 기반: 해시·ID·Expected·공용 별칭 (docs/01)
//
// 규약:
//  - 엔진 코드는 예외를 던지지 않는다. 오류는 Expected<T, Error> + 로그/어서트.
//  - 문자열은 전면 UTF-8 (std::string = UTF-8 바이트). win32 경계에서만 UTF-16 변환.
//  - EventTypeId / ServiceId 는 "이름 문자열의 FNV-1a 64bit 해시" — DLL 경계에서 안정.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace mye {

// ---------------------------------------------------------------------------
// FNV-1a 64bit 해시 (constexpr) — 이벤트 타입 ID·ServiceId의 정본 해시
// ---------------------------------------------------------------------------
constexpr uint64_t HashFnv1a64(std::string_view text) noexcept {
    uint64_t hash = 14695981039346656037ull;            // offset basis
    for (char c : text) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;                       // FNV prime
    }
    return hash;
}

// ---------------------------------------------------------------------------
// 이벤트 타입 ID / 서비스 ID (docs/01 — 이벤트 버스·EngineContext)
// ---------------------------------------------------------------------------
using EventTypeId = uint64_t;   // 이벤트 이름 FNV-1a 64 해시
using ServiceId   = uint64_t;   // 서비스 이름 FNV-1a 64 해시

// 이벤트 struct 안에 심는다:  struct FooEvent { MYE_EVENT(FooEvent); ... };
#define MYE_EVENT(TypeName) \
    static constexpr ::mye::EventTypeId kEventTypeId = ::mye::HashFnv1a64(#TypeName)

// 서비스(EngineContext에 등록되는 인터페이스) 클래스 안에 심는다.
#define MYE_SERVICE(TypeName) \
    static constexpr ::mye::ServiceId kServiceId = ::mye::HashFnv1a64(#TypeName)

// ---------------------------------------------------------------------------
// Error / Expected<T, E> — 예외 불사용 에러 전파 (docs/01 오픈이슈 #1 채택안)
// ---------------------------------------------------------------------------
struct Error {
    std::string message;    // UTF-8
    int32_t     code = 0;   // 서브시스템 정의 코드 (0 = 미지정)
};

template <typename T, typename E = Error>
class [[nodiscard]] Expected {
public:
    Expected(const T& value) : m_data(value) {}
    Expected(T&& value) : m_data(std::move(value)) {}
    Expected(const E& error) : m_data(error) {}
    Expected(E&& error) : m_data(std::move(error)) {}

    bool HasValue() const noexcept { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const noexcept { return HasValue(); }

    T&       Value() &        { return std::get<T>(m_data); }
    const T& Value() const &  { return std::get<T>(m_data); }
    T&&      Value() &&       { return std::get<T>(std::move(m_data)); }

    E&       GetError() &       { return std::get<E>(m_data); }
    const E& GetError() const & { return std::get<E>(m_data); }

private:
    std::variant<T, E> m_data;
};

// 값 없는 성공을 표현하는 단위 타입: Expected<Unit> 사용
struct Unit {};

template <typename E>
class [[nodiscard]] Expected<void, E> {
public:
    Expected() = default;                       // 성공
    Expected(const E& error) : m_error(error), m_hasError(true) {}
    Expected(E&& error) : m_error(std::move(error)), m_hasError(true) {}

    bool HasValue() const noexcept { return !m_hasError; }
    explicit operator bool() const noexcept { return HasValue(); }
    const E& GetError() const { return m_error; }

private:
    E    m_error{};
    bool m_hasError = false;
};

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 변환 (win32 W-API 경계 전용) — 구현: src/App.cpp 또는 platform
// ---------------------------------------------------------------------------
std::wstring Widen(std::string_view utf8);
std::string  Narrow(std::wstring_view utf16);

} // namespace mye
