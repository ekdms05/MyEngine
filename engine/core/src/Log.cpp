// Log.cpp — 콘솔(VT 컬러)·파일·OutputDebugString 싱크 + 심각도 필터 (docs/01 로깅·어서트)
//
// M0 범위: 락(뮤텍스) 동기 배출. MPSC 큐·비동기 배출 스레드·카테고리 필터는 Phase 2.
#include "mye/core/Log.h"

#include <Windows.h>

#include <cstdio>
#include <format>
#include <mutex>
#include <vector>

namespace mye {
namespace {

// ---------------------------------------------------------------------------
// UTF-8 → UTF-16 로컬 헬퍼 (win32 W-API 경계 전용)
//
// mye::Widen(구현: App.cpp)을 쓰지 않는 이유: 정적 라이브러리에서 App.cpp 오브젝트를
// 끌어오면 CreateApplication(exe 제공) 미정의 참조가 생겨 mye_tests처럼
// Application을 정의하지 않는 실행 파일의 링크가 깨진다. 통합 단계에서
// Widen/Narrow가 독립 TU로 이동하면 이 헬퍼는 제거 가능.
// ---------------------------------------------------------------------------
std::wstring ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                          static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), len);
    return out;
}

// ---------------------------------------------------------------------------
// 프로세스 시작(정확히는 최초 로그 초기화) 기준 경과 ns — QPC 정수 연산
// ---------------------------------------------------------------------------
uint64_t NowNs() {
    static const uint64_t s_freq = [] {
        LARGE_INTEGER f{};
        ::QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    static const uint64_t s_start = [] {
        LARGE_INTEGER c{};
        ::QueryPerformanceCounter(&c);
        return static_cast<uint64_t>(c.QuadPart);
    }();
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    const uint64_t ticks = static_cast<uint64_t>(now.QuadPart) - s_start;
    const uint64_t sec = ticks / s_freq;
    const uint64_t rem = ticks % s_freq;
    return sec * 1'000'000'000ull + rem * 1'000'000'000ull / s_freq;
}

constexpr std::string_view SeverityTag(LogSeverity s) {
    switch (s) {
        case LogSeverity::Trace: return "TRACE";
        case LogSeverity::Debug: return "DEBUG";
        case LogSeverity::Info:  return "INFO ";
        case LogSeverity::Warn:  return "WARN ";
        case LogSeverity::Error: return "ERROR";
        case LogSeverity::Fatal: return "FATAL";
    }
    return "?????";
}

// VT SGR 코드 (심각도별 라인 컬러)
constexpr std::string_view SeverityColor(LogSeverity s) {
    switch (s) {
        case LogSeverity::Trace: return "90";     // 회색
        case LogSeverity::Debug: return "36";     // 시안
        case LogSeverity::Info:  return "0";      // 기본
        case LogSeverity::Warn:  return "33";     // 노랑
        case LogSeverity::Error: return "31";     // 빨강
        case LogSeverity::Fatal: return "1;91";   // 밝은 빨강 볼드
    }
    return "0";
}

// "[hh:mm:ss.mmm][LEVEL][Category] " — 타임스탬프는 프로세스 상대 시각
std::string FormatPrefix(const LogMessage& msg) {
    const uint64_t ms = msg.timestampNs / 1'000'000ull;
    return std::format("[{:02}:{:02}:{:02}.{:03}][{}][{}] ",
                       ms / 3'600'000ull, (ms / 60'000ull) % 60ull,
                       (ms / 1000ull) % 60ull, ms % 1000ull,
                       SeverityTag(msg.severity), msg.category);
}

// 로그 파일 경로의 부모 디렉터리 생성 (얕은 경로용 — 이미 있으면 무시)
void CreateParentDirectories(std::wstring_view widePath) {
    for (size_t i = 1; i < widePath.size(); ++i) {
        const wchar_t c = widePath[i];
        if (c != L'/' && c != L'\\') continue;
        std::wstring dir(widePath.substr(0, i));
        if (dir.size() == 2 && dir[1] == L':') continue;   // "C:" 드라이브 루트
        ::CreateDirectoryW(dir.c_str(), nullptr);          // 실패(이미 존재 등)는 무시
    }
}

// ---------------------------------------------------------------------------
// 콘솔 싱크 — VT 컬러. 리다이렉트(파이프/파일)면 컬러 없이 평문.
// ---------------------------------------------------------------------------
class ConsoleSink final : public ILogSink {
public:
    ConsoleSink() {
        HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE &&
            ::GetConsoleMode(handle, &mode)) {
            m_vtEnabled =
                ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
            ::SetConsoleOutputCP(CP_UTF8);   // UTF-8 바이트를 그대로 출력 (전면 UTF-8 규약)
        }
    }

    void Write(const LogMessage& msg) override {
        std::string line;
        if (m_vtEnabled) {
            line = std::format("\x1b[{}m{}{}\x1b[0m\n",
                               SeverityColor(msg.severity), FormatPrefix(msg), msg.message);
        } else {
            line = std::format("{}{}\n", FormatPrefix(msg), msg.message);
        }
        std::fwrite(line.data(), 1, line.size(), stdout);
    }

    void Flush() override { std::fflush(stdout); }

private:
    bool m_vtEnabled = false;
};

// ---------------------------------------------------------------------------
// 파일 싱크 — CreateFileW + append. 회전은 M1+. M0 완료 기준 "로그 파일 생성"의 담당.
// ---------------------------------------------------------------------------
class FileSink final : public ILogSink {
public:
    explicit FileSink(std::string_view utf8Path) {
        const std::wstring wide = ToWide(utf8Path);
        if (wide.empty()) return;
        CreateParentDirectories(wide);
        HANDLE handle = ::CreateFileW(wide.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return;
        m_file = handle;

        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        WriteBytes(std::format(
            "=== MyEngine log session {:04}-{:02}-{:02} {:02}:{:02}:{:02} ===\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond));
    }

    ~FileSink() override {
        if (m_file) {
            ::FlushFileBuffers(m_file);
            ::CloseHandle(m_file);
        }
    }

    void Write(const LogMessage& msg) override {
        if (!m_file) return;
        std::string line = FormatPrefix(msg);
        line.append(msg.message);
        line.push_back('\n');
        WriteBytes(line);
    }

    void Flush() override {
        if (m_file) ::FlushFileBuffers(m_file);
    }

    bool IsOpen() const { return m_file != nullptr; }

private:
    void WriteBytes(std::string_view bytes) {
        DWORD written = 0;
        ::WriteFile(m_file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }

    HANDLE m_file = nullptr;
};

// ---------------------------------------------------------------------------
// OutputDebugStringW 싱크 — VS 출력 창·DebugView 용
// ---------------------------------------------------------------------------
class DebugOutputSink final : public ILogSink {
public:
    void Write(const LogMessage& msg) override {
        std::string line = FormatPrefix(msg);
        line.append(msg.message);
        line.push_back('\n');
        ::OutputDebugStringW(ToWide(line).c_str());
    }
};

// ---------------------------------------------------------------------------
// 전역 로그 상태 — 뮤텍스 동기 배출 (스레드 안전은 이 수준까지, docs/01 M0)
// ---------------------------------------------------------------------------
struct LogState {
    std::vector<std::unique_ptr<ILogSink>> sinks;
    LogSeverity minSeverity = LogSeverity::Trace;
    std::mutex mutex;
};

LogState& State() {
    static LogState s;
    return s;
}

} // namespace

// ---- 내장 싱크 팩토리 ----

std::unique_ptr<ILogSink> CreateConsoleSink() { return std::make_unique<ConsoleSink>(); }

std::unique_ptr<ILogSink> CreateFileSink(std::string_view utf8Path) {
    auto sink = std::make_unique<FileSink>(utf8Path);
    if (!sink->IsOpen())
        Log::WriteF(LogSeverity::Warn, "Log", "failed to open log file: {}", utf8Path);
    return sink;
}

std::unique_ptr<ILogSink> CreateDebugOutputSink() {
    return std::make_unique<DebugOutputSink>();
}

// ---- Log ----

void Log::Init(std::string_view logFilePathUtf8) {
    (void)NowNs();   // 타임스탬프 기준점 고정
    AddSink(CreateConsoleSink());
    AddSink(CreateDebugOutputSink());
    if (!logFilePathUtf8.empty())
        AddSink(CreateFileSink(logFilePathUtf8));   // M0 완료 기준: 시작 시 로그 파일 생성
}

void Log::Shutdown() {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    for (auto& sink : s.sinks) sink->Flush();
    s.sinks.clear();
}

void Log::AddSink(std::unique_ptr<ILogSink> sink) {
    if (!sink) return;
    auto& s = State();
    std::lock_guard lock(s.mutex);
    s.sinks.push_back(std::move(sink));
}

void Log::SetMinSeverity(LogSeverity severity) { State().minSeverity = severity; }

void Log::Write(LogSeverity severity, std::string_view category, std::string_view message) {
    auto& s = State();
    if (severity < s.minSeverity) return;
    std::lock_guard lock(s.mutex);
    const LogMessage msg{severity, category, message, NowNs()};
    for (auto& sink : s.sinks) sink->Write(msg);
    if (severity == LogSeverity::Fatal)
        for (auto& sink : s.sinks) sink->Flush();
}

} // namespace mye
