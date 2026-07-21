// mye/asset/FileSystem.cpp — VFS 구현 (docs/04 §VFS와 마운트)
//
// M1 슬림 범위: LooseFileSystem(디스크 루즈 파일) + VFS 마운트 테이블(우선순위 내림차순
// 첫 매치). pak·모드 오버레이 파일시스템은 동일 IFileSystem 경계로 M4에서 추가.
//
// 경로 규약:
//  - vpath = "mount://relative/path" (스킴은 소문자, '://' 구분). 상대 경로는 항상 '/'.
//  - LooseFileSystem은 rootDir + '/' + relative를 std::filesystem로 해석한다.
//  - 전면 UTF-8: std::string은 UTF-8 바이트. Windows에서 std::filesystem::path는
//    u8string 경유로 구성해 넓은 문자 경계 변환을 라이브러리에 맡긴다.
#include "mye/asset/FileSystem.h"

#include "mye/core/Log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

namespace mye::asset {

namespace {

namespace fs = std::filesystem;

// UTF-8 std::string → std::filesystem::path (플랫폼 넓은 문자 경계는 표준 라이브러리 처리).
fs::path Utf8ToPath(std::string_view utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// std::filesystem::path → UTF-8 std::string.
std::string PathToUtf8(const fs::path& p) {
    std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// 상대 경로 정규화: 역슬래시→슬래시, 선행 슬래시 제거, ".."/"."는 거부(디렉터리 탈출 방지).
// 실패 시 빈 optional.
bool IsSafeRelative(std::string_view rel) {
    if (rel.find('\\') != std::string_view::npos) {
        // 호출 전에 정규화되어야 함(NormalizeRelative). 여기서는 방어적 검사.
    }
    // ".." 세그먼트 탐지 — 단순하지만 충분(세그먼트 경계 확인).
    size_t start = 0;
    while (start <= rel.size()) {
        size_t slash = rel.find('/', start);
        std::string_view seg =
            rel.substr(start, slash == std::string_view::npos ? std::string_view::npos
                                                              : slash - start);
        if (seg == "..") return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

// 역슬래시→슬래시, 중복 슬래시 축약, 선행/후행 슬래시 정리.
std::string NormalizeRelative(std::string_view rel) {
    std::string out;
    out.reserve(rel.size());
    bool prevSlash = false;
    for (char c : rel) {
        if (c == '\\') c = '/';
        if (c == '/') {
            if (prevSlash || out.empty()) continue;  // 선행/중복 슬래시 스킵
            prevSlash = true;
            out.push_back('/');
        } else {
            prevSlash = false;
            out.push_back(c);
        }
    }
    // 후행 슬래시 제거
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// LooseFileSystem
// ---------------------------------------------------------------------------
LooseFileSystem::LooseFileSystem(std::string rootDir) : m_root(std::move(rootDir)) {
    // 루트 경로도 정규화(후행 슬래시 제거).
    while (!m_root.empty() && (m_root.back() == '/' || m_root.back() == '\\')) {
        m_root.pop_back();
    }
}

LooseFileSystem::~LooseFileSystem() = default;

bool LooseFileSystem::Exists(std::string_view path) const {
    std::string rel = NormalizeRelative(path);
    if (!IsSafeRelative(rel)) return false;
    fs::path full = Utf8ToPath(m_root) / Utf8ToPath(rel);
    std::error_code ec;
    return fs::is_regular_file(full, ec);
}

Expected<Blob, Error> LooseFileSystem::ReadAll(std::string_view path) {
    std::string rel = NormalizeRelative(path);
    if (!IsSafeRelative(rel)) {
        return Error{"LooseFileSystem::ReadAll: unsafe relative path '" + std::string(path) + "'",
                     1};
    }
    fs::path full = Utf8ToPath(m_root) / Utf8ToPath(rel);

    std::error_code ec;
    if (!fs::is_regular_file(full, ec)) {
        return Error{"LooseFileSystem::ReadAll: not a file '" + PathToUtf8(full) + "'", 2};
    }
    const auto size = fs::file_size(full, ec);
    if (ec) {
        return Error{"LooseFileSystem::ReadAll: file_size failed '" + PathToUtf8(full) + "'", 3};
    }

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        return Error{"LooseFileSystem::ReadAll: open failed '" + PathToUtf8(full) + "'", 4};
    }

    Blob blob(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) {
            return Error{"LooseFileSystem::ReadAll: short read '" + PathToUtf8(full) + "'", 5};
        }
    }
    return blob;
}

void LooseFileSystem::Enumerate(std::string_view dir, const EnumerateFn& fn) const {
    std::string rel = NormalizeRelative(dir);
    if (!IsSafeRelative(rel)) return;
    fs::path base = Utf8ToPath(m_root);
    fs::path start = rel.empty() ? base : (base / Utf8ToPath(rel));

    std::error_code ec;
    if (!fs::is_directory(start, ec)) return;

    for (auto it = fs::recursive_directory_iterator(
             start, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        // base 기준 상대 경로를 '/' 구분 UTF-8으로 산출.
        std::error_code relEc;
        fs::path relPath = fs::relative(it->path(), base, relEc);
        if (relEc) continue;
        fn(PathToUtf8(relPath));
    }
}

// ---------------------------------------------------------------------------
// VirtualFileSystem
// ---------------------------------------------------------------------------
struct VirtualFileSystem::Impl {
    struct Entry {
        std::unique_ptr<IFileSystem> fs;
        int priority = 0;
    };
    // mountPoint(스킴) → 우선순위 내림차순 정렬된 파일시스템 목록.
    std::map<std::string, std::vector<Entry>, std::less<>> mounts;

    std::vector<Entry>* FindMount(std::string_view mountPoint) {
        auto it = mounts.find(mountPoint);
        return it == mounts.end() ? nullptr : &it->second;
    }
};

VirtualFileSystem::VirtualFileSystem() : m_impl(std::make_unique<Impl>()) {}
VirtualFileSystem::~VirtualFileSystem() = default;

void VirtualFileSystem::Mount(std::string_view mountPoint, std::unique_ptr<IFileSystem> fs,
                              int priority) {
    if (!fs) {
        MYE_LOG_WARN("Asset", "VFS::Mount: null filesystem for '{}'", std::string(mountPoint));
        return;
    }
    auto& list = m_impl->mounts[std::string(mountPoint)];
    list.push_back({std::move(fs), priority});
    // 우선순위 내림차순 유지(안정 정렬로 동일 우선순위는 등록 순서 보존).
    std::stable_sort(list.begin(), list.end(),
                     [](const Impl::Entry& a, const Impl::Entry& b) { return a.priority > b.priority; });
    MYE_LOG_INFO("Asset", "VFS::Mount {} priority={} count={}", std::string(mountPoint), priority,
                 static_cast<int>(list.size()));
}

void VirtualFileSystem::Unmount(std::string_view mountPoint, IFileSystem* fs) {
    auto* list = m_impl->FindMount(mountPoint);
    if (!list) return;
    std::erase_if(*list, [fs](const Impl::Entry& e) { return e.fs.get() == fs; });
    if (list->empty()) {
        m_impl->mounts.erase(std::string(mountPoint));
    }
}

Expected<std::pair<std::string, std::string>, Error>
VirtualFileSystem::SplitVpath(std::string_view vpath) {
    const size_t schemeEnd = vpath.find("://");
    if (schemeEnd == std::string_view::npos) {
        return Error{"VFS::SplitVpath: missing '://' in '" + std::string(vpath) + "'", 1};
    }
    if (schemeEnd == 0) {
        return Error{"VFS::SplitVpath: empty mount point in '" + std::string(vpath) + "'", 2};
    }
    std::string mount(vpath.substr(0, schemeEnd));
    std::string rel = NormalizeRelative(vpath.substr(schemeEnd + 3));
    return std::make_pair(std::move(mount), std::move(rel));
}

bool VirtualFileSystem::Exists(std::string_view vpath) const {
    auto split = SplitVpath(vpath);
    if (!split) return false;
    const auto& [mount, rel] = split.Value();
    auto* list = m_impl->FindMount(mount);
    if (!list) return false;
    for (const auto& e : *list) {
        if (e.fs->Exists(rel)) return true;
    }
    return false;
}

Expected<Blob, Error> VirtualFileSystem::ReadAll(std::string_view vpath) {
    auto split = SplitVpath(vpath);
    if (!split) return split.GetError();
    const auto& [mount, rel] = split.Value();
    auto* list = m_impl->FindMount(mount);
    if (!list) {
        return Error{"VFS::ReadAll: no mount for '" + mount + "://'", 3};
    }
    // 우선순위 내림차순 첫 매치.
    for (const auto& e : *list) {
        if (e.fs->Exists(rel)) {
            return e.fs->ReadAll(rel);
        }
    }
    return Error{"VFS::ReadAll: not found '" + std::string(vpath) + "'", 4};
}

} // namespace mye::asset
