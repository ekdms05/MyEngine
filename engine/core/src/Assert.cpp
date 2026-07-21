// Assert.cpp — 어서트 실패 보고 + 최소 크래시 핸들러 (docs/01 로깅·어서트·크래시 덤프)
//
// Phase 0 범위: 크래시는 로그 기록만(미니덤프 없음 — MiniDumpWriteDump는 Phase 1/M6).
#include "mye/core/Assert.h"
#include "mye/core/CrashHandler.h"
#include "mye/core/Log.h"

#include <Windows.h>

#include <atomic>
#include <cstdlib>

namespace mye::detail {

bool ReportAssertFailure(AssertKind kind, const char* expr,
                         const char* file, int line, const char* message) {
    const char* kindName =
        kind == AssertKind::Assert ? "ASSERT" : (kind == AssertKind::Verify ? "VERIFY" : "CHECK");
    if (message && message[0]) {
        Log::WriteF(LogSeverity::Fatal, "Assert", "{} failed: ({}) at {}:{} — {}",
                    kindName, expr, file, line, message);
    } else {
        Log::WriteF(LogSeverity::Fatal, "Assert", "{} failed: ({}) at {}:{}",
                    kindName, expr, file, line);
    }
    // Fatal 심각도는 Log::Write가 전 싱크를 즉시 플러시한다 — 브레이크/종료 전에 기록 보존.
#if MYE_DEBUG
    (void)kind;
    return true;    // Debug: 브레이크 요청. 디버거 미부착이면 breakpoint 예외 → 크래시 필터가 로그 후 종료(fail-fast).
#else
    return kind == AssertKind::Check;   // Release: CHECK만 브레이크(직후 FatalExit)
#endif
}

[[noreturn]] void FatalExit() {
    // TODO(Phase 1/M6): MiniDumpWriteDump + 최근 로그 링버퍼 저장
    if (::IsDebuggerPresent()) MYE_DEBUG_BREAK();
    Log::Shutdown();                                     // 전 싱크 플러시 후 해제
    ::TerminateProcess(::GetCurrentProcess(), 3);        // CRT abort 다이얼로그·WER 억제
    std::abort();                                        // unreachable — [[noreturn]] 충족용
}

} // namespace mye::detail

namespace mye {
namespace {

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
        case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
        case EXCEPTION_GUARD_PAGE:               return "GUARD_PAGE";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
        case EXCEPTION_INVALID_HANDLE:           return "INVALID_HANDLE";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        default:                                 return "UNKNOWN";
    }
}

std::atomic<bool> g_inCrashFilter{false};

LONG WINAPI CrashExceptionFilter(EXCEPTION_POINTERS* pointers) {
    // 필터 내부 2차 크래시(재진입) 시 즉시 종료 — 무한 루프 방지
    if (g_inCrashFilter.exchange(true)) return EXCEPTION_EXECUTE_HANDLER;

    const EXCEPTION_RECORD* rec = pointers ? pointers->ExceptionRecord : nullptr;
    if (rec) {
        // 주의: std::format은 힙을 쓴다 — 힙 손상 크래시에서는 실패할 수 있는 best-effort (Phase 0 허용)
        Log::WriteF(LogSeverity::Fatal, "Crash",
                    "unhandled exception {} ({:#010x}) at {:#018x}",
                    ExceptionName(rec->ExceptionCode), rec->ExceptionCode,
                    reinterpret_cast<uintptr_t>(rec->ExceptionAddress));
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
            const char* op = rec->ExceptionInformation[0] == 0   ? "read"
                             : rec->ExceptionInformation[0] == 1 ? "write"
                                                                 : "execute (DEP)";
            Log::WriteF(LogSeverity::Fatal, "Crash",
                        "access violation: {} of address {:#018x}",
                        op, static_cast<uintptr_t>(rec->ExceptionInformation[1]));
        }
    } else {
        Log::Write(LogSeverity::Fatal, "Crash", "unhandled exception (no exception record)");
    }
    // TODO(Phase 1/M6): MiniDumpWriteDump(.dmp) + 최근 로그 링버퍼 첨부

    if (::IsDebuggerPresent()) return EXCEPTION_CONTINUE_SEARCH;   // 디버거가 잡게 통과
    return EXCEPTION_EXECUTE_HANDLER;                              // 로그만 남기고 조용히 종료
}

} // namespace

void InstallCrashHandler() {
    ::SetUnhandledExceptionFilter(&CrashExceptionFilter);
}

} // namespace mye
