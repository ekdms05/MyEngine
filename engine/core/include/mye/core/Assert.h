// mye/core/Assert.h — 어서트 3종 (docs/01)
//
//  MYE_ASSERT(expr, ...) : Debug 전용. 실패 시 로그 + 브레이크. Release에서는 완전 제거(평가 안 함).
//  MYE_VERIFY(expr, ...) : 모든 빌드에서 평가. 실패 시 로그+브레이크(Release는 로그만). 식으로 사용 가능(bool 반환).
//  MYE_CHECK(expr)       : Release 포함 치명 검사. 실패 시 (M0: 로그 후) 즉시 종료. 크래시 덤프는 Phase 1.
#pragma once

#include <cstdint>

namespace mye::detail {

enum class AssertKind : uint8_t { Assert, Verify, Check };

// 실패 보고(로그·덤프). 반환 true = 디버그 브레이크 요청.  구현: src/Assert.cpp
bool ReportAssertFailure(AssertKind kind, const char* expr,
                         const char* file, int line, const char* message /*nullable*/);

[[noreturn]] void FatalExit();   // MYE_CHECK 실패 시 호출 (M0: 로그 플러시 후 abort)

} // namespace mye::detail

#define MYE_DEBUG_BREAK() __debugbreak()

// 내부 공용 — msg는 문자열 리터럴 또는 생략
#define MYE_DETAIL_ASSERT_IMPL(kind, expr, msg)                                              \
    (static_cast<bool>(expr) ||                                                              \
     (::mye::detail::ReportAssertFailure(kind, #expr, __FILE__, __LINE__, msg)               \
          ? (MYE_DEBUG_BREAK(), false)                                                       \
          : false))

#if MYE_DEBUG
    #define MYE_ASSERT(expr, ...) \
        ((void)MYE_DETAIL_ASSERT_IMPL(::mye::detail::AssertKind::Assert, expr, "" __VA_ARGS__))
#else
    #define MYE_ASSERT(expr, ...) ((void)0)
#endif

// 모든 빌드에서 expr을 평가한다. if (MYE_VERIFY(ptr)) { ... } 패턴 허용.
#if MYE_DEBUG
    #define MYE_VERIFY(expr, ...) \
        (static_cast<bool>(expr) || \
         (::mye::detail::ReportAssertFailure(::mye::detail::AssertKind::Verify, #expr, __FILE__, __LINE__, "" __VA_ARGS__) \
              ? (MYE_DEBUG_BREAK(), false) : false))
#else
    #define MYE_VERIFY(expr, ...) \
        (static_cast<bool>(expr) || \
         (::mye::detail::ReportAssertFailure(::mye::detail::AssertKind::Verify, #expr, __FILE__, __LINE__, "" __VA_ARGS__), false))
#endif

#define MYE_CHECK(expr)                                                                       \
    do {                                                                                      \
        if (!static_cast<bool>(expr)) {                                                       \
            ::mye::detail::ReportAssertFailure(::mye::detail::AssertKind::Check, #expr,       \
                                               __FILE__, __LINE__, nullptr);                  \
            ::mye::detail::FatalExit();                                                       \
        }                                                                                     \
    } while (0)
