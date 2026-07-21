// FileWatcher.cpp — win32 재귀 디렉터리 워처 (docs/04 §핫 리로드, M3-A)
//
// ReadDirectoryChangesW 를 전용 스레드에서 블로킹 호출해 rootDir 이하 변경을 통보받는다.
//   - 종료 신호는 별도 이벤트(m_stopEvent)로 WaitForMultipleObjects 를 깨워 클린하게 조인한다.
//   - 콜백은 워처 스레드에서 호출된다(헤더 계약). 소비자(AssetDatabase)가 큐잉·디바운스한다.
//   - 경로는 UTF-8 로 정규화해 전달한다(win32 경계에서만 UTF-16).
//
// FILE_NOTIFY_INFORMATION 의 Action 을 FileChangeKind 로 매핑한다. RENAMED_OLD/NEW 는 한 쌍으로
//   묶어 Renamed(oldPath 채움)로 합친다(OLD 다음 NEW 가 온다는 win32 계약 이용).
#include "mye/asset/FileWatcher.h"

#include "mye/core/Log.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mye::asset {

namespace {

// UTF-16 (win32) → UTF-8. count 는 wchar 개수(널 미포함).
std::string Utf16ToUtf8(const wchar_t* wstr, size_t count) {
    if (count == 0) return {};
    int needed = ::WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(count),
                                       nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr, static_cast<int>(count),
                          out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToUtf16(std::string_view utf8) {
    if (utf8.empty()) return {};
    int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), needed);
    return out;
}

// 역슬래시 → 슬래시 정규화(엔진 전면 '/' 경로 규약).
std::string NormalizeSlashes(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

}  // namespace

struct Win32DirectoryWatcher::Impl {
    ChangeFn          callback;
    std::string       rootUtf8;     // '/' 정규화된 루트(콜백 경로 접두)
    HANDLE            dirHandle = INVALID_HANDLE_VALUE;
    HANDLE            stopEvent = nullptr;   // 종료 시그널(수동 리셋)
    OVERLAPPED        overlapped{};          // 비동기 ReadDirectoryChangesW + hEvent
    std::thread       thread;
    std::atomic<bool> running{false};

    void ThreadMain();
    void Stop();
};

void Win32DirectoryWatcher::Impl::ThreadMain() {
    // FILE_NOTIFY_INFORMATION 가변 레코드를 담을 버퍼(정렬 요구: DWORD).
    std::vector<std::byte> buffer(64 * 1024);
    std::wstring pendingRenameOld;   // RENAMED_OLD_NAME 을 짝지어 Renamed 로 합침

    while (running.load()) {
        DWORD bytesReturned = 0;
        ::ResetEvent(overlapped.hEvent);
        BOOL ok = ::ReadDirectoryChangesW(
            dirHandle, buffer.data(), static_cast<DWORD>(buffer.size()),
            TRUE,  // 재귀(하위 트리 전부)
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned, &overlapped, nullptr);
        if (!ok) {
            MYE_LOG_ERROR("Asset", "FileWatcher: ReadDirectoryChangesW failed ({})",
                          static_cast<unsigned>(::GetLastError()));
            break;
        }

        HANDLE waits[2] = {stopEvent, overlapped.hEvent};
        DWORD wr = ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wr == WAIT_OBJECT_0) break;  // stopEvent — 종료

        DWORD transferred = 0;
        if (!::GetOverlappedResult(dirHandle, &overlapped, &transferred, FALSE) ||
            transferred == 0) {
            // 오버플로(버퍼 부족)면 통보 유실 — 다음 루프에서 재무장. 로그만 남긴다.
            continue;
        }

        // 레코드 순회.
        size_t offset = 0;
        for (;;) {
            auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
            const size_t nameChars = info->FileNameLength / sizeof(wchar_t);
            std::string rel = NormalizeSlashes(Utf16ToUtf8(info->FileName, nameChars));
            std::string full = rootUtf8;
            if (!full.empty() && full.back() != '/') full.push_back('/');
            full += rel;

            switch (info->Action) {
                case FILE_ACTION_ADDED:
                    if (callback) callback(FileChange{FileChangeKind::Created, full, {}});
                    break;
                case FILE_ACTION_REMOVED:
                    if (callback) callback(FileChange{FileChangeKind::Removed, full, {}});
                    break;
                case FILE_ACTION_MODIFIED:
                    if (callback) callback(FileChange{FileChangeKind::Modified, full, {}});
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    pendingRenameOld.assign(info->FileName, nameChars);
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME: {
                    std::string oldRel = NormalizeSlashes(
                        Utf16ToUtf8(pendingRenameOld.data(), pendingRenameOld.size()));
                    std::string oldFull = rootUtf8;
                    if (!oldFull.empty() && oldFull.back() != '/') oldFull.push_back('/');
                    oldFull += oldRel;
                    if (callback) callback(FileChange{FileChangeKind::Renamed, full, oldFull});
                    pendingRenameOld.clear();
                    break;
                }
                default:
                    break;
            }

            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
            if (offset >= buffer.size()) break;
        }
    }
}

void Win32DirectoryWatcher::Impl::Stop() {
    if (!running.exchange(false)) {
        // 스레드가 없어도 핸들 정리는 진행.
    } else if (stopEvent) {
        ::SetEvent(stopEvent);
    }
    // 진행 중 IO 취소(대기 중이면 stopEvent 로 이미 깨어난다).
    if (dirHandle != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(dirHandle, &overlapped);
    }
    if (thread.joinable()) thread.join();

    if (overlapped.hEvent) { ::CloseHandle(overlapped.hEvent); overlapped.hEvent = nullptr; }
    if (stopEvent) { ::CloseHandle(stopEvent); stopEvent = nullptr; }
    if (dirHandle != INVALID_HANDLE_VALUE) { ::CloseHandle(dirHandle); dirHandle = INVALID_HANDLE_VALUE; }
    callback = nullptr;
}

Win32DirectoryWatcher::Win32DirectoryWatcher() : m_impl(std::make_unique<Impl>()) {}
Win32DirectoryWatcher::~Win32DirectoryWatcher() { StopAll(); }

Expected<void, Error> Win32DirectoryWatcher::Watch(std::string_view rootDir, ChangeFn onChange) {
    // 재무장(이미 감시 중이면 먼저 정지).
    m_impl->Stop();

    m_impl->callback = std::move(onChange);
    m_impl->rootUtf8 = NormalizeSlashes(std::string(rootDir));

    std::wstring wdir = Utf8ToUtf16(rootDir);
    m_impl->dirHandle = ::CreateFileW(
        wdir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (m_impl->dirHandle == INVALID_HANDLE_VALUE) {
        return Error{"FileWatcher: CreateFileW failed for '" + std::string(rootDir) + "' (" +
                         std::to_string(::GetLastError()) + ")",
                     static_cast<int32_t>(::GetLastError())};
    }

    m_impl->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);         // 수동 리셋
    m_impl->overlapped = OVERLAPPED{};
    m_impl->overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr); // 수동 리셋(IO 완료)
    if (!m_impl->stopEvent || !m_impl->overlapped.hEvent) {
        m_impl->Stop();
        return Error{"FileWatcher: CreateEventW failed", -1};
    }

    m_impl->running.store(true);
    m_impl->thread = std::thread([impl = m_impl.get()]() { impl->ThreadMain(); });
    return {};
}

void Win32DirectoryWatcher::StopAll() {
    if (m_impl) m_impl->Stop();
}

}  // namespace mye::asset
