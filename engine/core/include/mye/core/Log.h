// mye/core/Log.h — 카테고리 × 심각도 로그, 싱크 플러그인 구조 (docs/01)
//
// M0 범위: 콘솔(VT 컬러)·파일·OutputDebugString 싱크. 비동기 배출 스레드는 Phase 2.
#pragma once

#include "mye/core/Base.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace mye {

enum class LogSeverity : uint8_t { Trace, Debug, Info, Warn, Error, Fatal };

struct LogMessage {
    LogSeverity      severity;
    std::string_view category;   // 정적 등록 카테고리 이름 (수명: 정적 문자열)
    std::string_view message;    // UTF-8, 포매팅 완료 텍스트
    uint64_t         timestampNs; // 프로세스 시작 기준 경과 ns
};

// 로그 싱크 — 확장 포인트 (플러그인이 원격 싱크 등록 가능)
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogMessage& msg) = 0;
    virtual void Flush() {}
};

// 내장 싱크 (구현: src/Log.cpp)
std::unique_ptr<ILogSink> CreateConsoleSink();                       // VT 컬러
std::unique_ptr<ILogSink> CreateFileSink(std::string_view utf8Path); // 회전은 M1+
std::unique_ptr<ILogSink> CreateDebugOutputSink();                   // OutputDebugStringW

// 전역 로그 시스템 (GuardedMain이 Init/Shutdown 소유)
class Log {
public:
    // 기본 싱크(콘솔 VT + DebugOutput + 파일) 장착. logFilePathUtf8이 비면 파일 싱크 생략.
    // 기본 경로는 작업 디렉터리 기준 — GuardedMain이 userDir 결정 후 명시 경로로 호출 권장.
    static void Init(std::string_view logFilePathUtf8 = "logs/MyEngine.log");
    static void Shutdown();                              // Flush 후 싱크 해제
    static void AddSink(std::unique_ptr<ILogSink> sink);
    static void SetMinSeverity(LogSeverity severity);    // 전역 필터 (카테고리별 필터는 Phase 2)

    // 포매팅 완료 문자열 발행 (Fatal은 즉시 플러시)
    static void Write(LogSeverity severity, std::string_view category, std::string_view message);

    template <typename... Args>
    static void WriteF(LogSeverity severity, std::string_view category,
                       std::format_string<Args...> fmt, Args&&... args) {
        Write(severity, category, std::format(fmt, std::forward<Args>(args)...));
    }
};

} // namespace mye

// 사용 예: MYE_LOG_INFO("Core", "window {}x{}", w, h);
#define MYE_LOG(severity, category, ...) \
    ::mye::Log::WriteF(severity, category, __VA_ARGS__)
#define MYE_LOG_TRACE(category, ...) MYE_LOG(::mye::LogSeverity::Trace, category, __VA_ARGS__)
#define MYE_LOG_DEBUG(category, ...) MYE_LOG(::mye::LogSeverity::Debug, category, __VA_ARGS__)
#define MYE_LOG_INFO(category, ...)  MYE_LOG(::mye::LogSeverity::Info,  category, __VA_ARGS__)
#define MYE_LOG_WARN(category, ...)  MYE_LOG(::mye::LogSeverity::Warn,  category, __VA_ARGS__)
#define MYE_LOG_ERROR(category, ...) MYE_LOG(::mye::LogSeverity::Error, category, __VA_ARGS__)
#define MYE_LOG_FATAL(category, ...) MYE_LOG(::mye::LogSeverity::Fatal, category, __VA_ARGS__)
