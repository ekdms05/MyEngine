// mye/asset/FileWatcher.h — 소스 파일 변경 감시 (docs/04 §핫 리로드, M3-A)
//
// 저수준 파일 워처의 API 경계는 04가 소유한다(01은 win32 ReadDirectoryChangesW 원시 통보만
// 제공 예정이나, M3-A에서는 04가 얇은 win32 폴러/워처를 직접 벤더링해 자립한다). 디바운스·
// 리임포트 정책은 04(AssetDatabase) 소유.
//
// M3-A 범위: 인터페이스 동결 + win32 구현 골격. 콜백은 워처 스레드에서 올 수 있으므로
//   소비자(AssetDatabase)는 이벤트를 큐잉해 메인 틱에서 소진한다.
#pragma once

#include "mye/core/Base.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace mye::asset {

// 변경 종류 — 리임포트 트리거·GUID 매핑 갱신 판정에 사용.
enum class FileChangeKind : uint8_t { Created, Modified, Removed, Renamed };

struct FileChange {
    FileChangeKind kind = FileChangeKind::Modified;
    std::string    path;       // OS 절대 경로(UTF-8)
    std::string    oldPath;    // Renamed 일 때 이전 경로, 그 외 빈 문자열
};

// 확장 포인트: 플러그인이 대체 워처(폴링·네트워크)를 구현 가능. 기본은 Win32DirectoryWatcher.
class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;

    using ChangeFn = std::function<void(const FileChange&)>;

    // rootDir 이하를 재귀 감시. 콜백은 임의(워처) 스레드에서 호출될 수 있다.
    virtual Expected<void, Error> Watch(std::string_view rootDir, ChangeFn onChange) = 0;
    virtual void StopAll() = 0;

    // 폴링형 워처를 위한 틱(win32 이벤트형은 no-op). 에디터 틱에서 호출.
    virtual void Poll() {}
};

// win32 ReadDirectoryChangesW 기반 재귀 워처. 전용 스레드에서 통보를 받아 콜백 전달.
class Win32DirectoryWatcher final : public IFileWatcher {
public:
    Win32DirectoryWatcher();
    ~Win32DirectoryWatcher() override;
    Win32DirectoryWatcher(const Win32DirectoryWatcher&) = delete;
    Win32DirectoryWatcher& operator=(const Win32DirectoryWatcher&) = delete;

    Expected<void, Error> Watch(std::string_view rootDir, ChangeFn onChange) override;
    void StopAll() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
