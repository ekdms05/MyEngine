// TestFramework.h — 자체 최소 테스트 프레임워크 (서드파티 금지)
//
// 사용:
//   MYE_TEST(MathVecDot) {
//       MYE_EXPECT(mye::Vec2::Dot({1,0},{0,1}) == 0.0f);
//       MYE_EXPECT_NEAR(1.0f, 1.0f + 1e-7f, 1e-5f);
//   }
// 러너(TestMain.cpp)가 등록된 전 테스트를 실행하고 실패 개수를 종료 코드로 반환한다.
#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

namespace mye::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

// 정적 초기화 순서 안전한 레지스트리
inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct AutoRegister {
    AutoRegister(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

// 현재 실행 중 테스트의 실패 카운터 (러너가 리셋·수집)
inline int& CurrentFailures() {
    static int failures = 0;
    return failures;
}

inline void ReportFailure(const char* what, const char* file, int line) {
    std::printf("    FAIL: %s (%s:%d)\n", what, file, line);
    ++CurrentFailures();
}

inline int RunAll() {
    int failedTests = 0;
    auto& cases = Registry();
    std::printf("[mye_tests] running %zu tests\n", cases.size());
    for (const auto& tc : cases) {
        CurrentFailures() = 0;
        std::printf("  [ RUN  ] %s\n", tc.name);
        tc.fn();
        if (CurrentFailures() > 0) {
            std::printf("  [ FAIL ] %s (%d expectation(s))\n", tc.name, CurrentFailures());
            ++failedTests;
        } else {
            std::printf("  [  OK  ] %s\n", tc.name);
        }
    }
    std::printf("[mye_tests] %d/%zu passed\n",
                static_cast<int>(cases.size()) - failedTests, cases.size());
    return failedTests;
}

} // namespace mye::test

#define MYE_TEST(Name)                                                        \
    static void MyeTest_##Name();                                             \
    static ::mye::test::AutoRegister s_myeTestReg_##Name{#Name, &MyeTest_##Name}; \
    static void MyeTest_##Name()

// 가변 인자: 중괄호 초기화 리스트의 콤마( Vec2{1, 2} 등 )를 허용
#define MYE_EXPECT(...)                                                       \
    do {                                                                      \
        if (!(__VA_ARGS__))                                                   \
            ::mye::test::ReportFailure(#__VA_ARGS__, __FILE__, __LINE__);     \
    } while (0)

#define MYE_EXPECT_NEAR(a, b, eps)                                            \
    do {                                                                      \
        if (!(std::abs((a) - (b)) <= (eps)))                                  \
            ::mye::test::ReportFailure(#a " ~= " #b, __FILE__, __LINE__);     \
    } while (0)
