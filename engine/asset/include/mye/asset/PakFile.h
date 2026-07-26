// mye/asset/PakFile.h — .pak 아카이브: 패킹(PakWriter) + 마운트(PakFileSystem) (docs/04 §VFS)
//
// 배포용으로 assets 디렉터리를 단일 .pak 파일로 묶는다. 런타임은 PakFileSystem 을 VFS 에
// 마운트해 loose 파일과 동일한 "mount://path" 경로로 접근한다(IFileSystem 경계 공유).
//
// 포맷 v1(리틀엔디안):
//   [Header 32B] magic "MYEPAK01" | u32 version=1 | u32 entryCount | u64 tocOffset | u64 reserved
//   [Data]       각 파일 블롭을 연속 배치(헤더 직후부터)
//   [TOC]        entryCount 개: u32 pathLen | path(utf8,'/') | u64 dataOffset(절대) | u64 dataSize
#pragma once

#include "mye/asset/FileSystem.h"
#include "mye/core/Base.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace mye::asset {

// 디렉터리 트리를 .pak 으로 묶는 라이터.
class PakWriter {
public:
    // 파일 1개 추가(relPath: '/' 구분 상대 경로, data: 파일 바이트).
    void AddFile(std::string relPath, Blob data);

    // OS 디렉터리를 재귀적으로 추가(rootDir 기준 상대 경로로 저장). 추가한 파일 수 반환.
    Expected<int, Error> AddDirectory(std::string_view rootDir);

    // .pak 파일로 기록.
    Expected<void, Error> Write(std::string_view outPath) const;

    int Count() const;

private:
    struct Entry { std::string path; Blob data; };
    std::vector<Entry> m_entries;
};

// .pak 을 마운트해 읽는 파일시스템(IFileSystem). 생성 시 TOC 를 메모리에 적재하고,
// ReadAll 은 읽기마다 파일을 열어 해당 블롭 구간만 읽는다(loose 와 동일한 스레드 안전성).
class PakFileSystem final : public IFileSystem {
public:
    // .pak 을 열어 TOC 를 적재. 실패해도 객체는 생성되며 IsValid()==false(모든 질의 실패).
    static Expected<std::unique_ptr<PakFileSystem>, Error> Open(std::string pakPath);
    ~PakFileSystem() override;

    bool IsValid() const;
    int  Count() const;

    bool Exists(std::string_view path) const override;
    Expected<Blob, Error> ReadAll(std::string_view path) override;
    void Enumerate(std::string_view dir, const EnumerateFn& fn) const override;

private:
    PakFileSystem();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
