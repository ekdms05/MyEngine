// mye/asset/FileSystem.h — VFS: 가상 파일 시스템 + 마운트 (docs/04 §VFS와 마운트)
//
// 루즈 파일/pak/모드 오버레이를 동일한 "mount://path" 경로 체계로 접근한다.
// 같은 마운트 포인트에 여러 IFileSystem을 우선순위 정수로 겹쳐 마운트하면,
// 우선순위 내림차순으로 첫 매치가 이긴다(모드 pak은 base보다 높은 priority).
// M0 범위: LooseFileSystem + assets://·user:// 마운트, ReadAll만. pak은 M4.
#pragma once

#include "mye/core/Base.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::asset {

// 파일 전체를 담는 소유 바이트 버퍼(디코드·파싱 입력).
using Blob = std::vector<std::byte>;

// 확장 포인트: 플러그인이 커스텀 파일시스템(암호화 pak·네트워크 등)을 구현 가능.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // path는 마운트 포인트를 제거한 상대 경로("x/y.png"). 항상 '/' 구분자.
    virtual bool Exists(std::string_view path) const = 0;
    virtual Expected<Blob, Error> ReadAll(std::string_view path) = 0;

    using EnumerateFn = std::function<void(std::string_view relativePath)>;
    virtual void Enumerate(std::string_view dir, const EnumerateFn& fn) const = 0;
};

// OS 디렉터리를 마운트 포인트에 매핑하는 기본 파일시스템.
class LooseFileSystem final : public IFileSystem {
public:
    // rootDir: UTF-8 OS 절대/상대 디렉터리 경로.
    explicit LooseFileSystem(std::string rootDir);
    ~LooseFileSystem() override;

    bool Exists(std::string_view path) const override;
    Expected<Blob, Error> ReadAll(std::string_view path) override;
    void Enumerate(std::string_view dir, const EnumerateFn& fn) const override;

    std::string_view RootDir() const { return m_root; }

private:
    std::string m_root;
};

// 마운트 우선순위로 파일시스템을 겹쳐 vpath("assets://x/y.bin")를 해석한다.
class VirtualFileSystem {
public:
    MYE_SERVICE(VirtualFileSystem);

    VirtualFileSystem();
    ~VirtualFileSystem();
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // mountPoint: "assets" (스킴만, "://" 제외). priority 높을수록 먼저 검색.
    void Mount(std::string_view mountPoint, std::unique_ptr<IFileSystem> fs, int priority);
    void Unmount(std::string_view mountPoint, IFileSystem* fs);

    bool Exists(std::string_view vpath) const;                 // "assets://x/y.bin"
    Expected<Blob, Error> ReadAll(std::string_view vpath);

    // vpath를 (mountPoint, relativePath)로 분해. 실패 시 Error.
    static Expected<std::pair<std::string, std::string>, Error>
    SplitVpath(std::string_view vpath);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::asset
