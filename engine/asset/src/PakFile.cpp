// mye/asset/PakFile.cpp — PakFile.h 구현 (.pak 패킹/마운트)
#include "mye/asset/PakFile.h"

#include "mye/core/Log.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace mye::asset {

namespace {

namespace fs = std::filesystem;

constexpr char kMagic[8] = {'M', 'Y', 'E', 'P', 'A', 'K', '0', '1'};
constexpr uint32_t kVersion = 1;
constexpr size_t   kHeaderSize = 32;

fs::path Utf8ToPath(std::string_view u) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(u.data()), u.size()));
}
std::string PathToUtf8(const fs::path& p) {
    std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// 역슬래시→슬래시, 중복/선행/후행 슬래시 정리(FileSystem.cpp 규약과 동일).
std::string Normalize(std::string_view rel) {
    std::string out;
    out.reserve(rel.size());
    bool prevSlash = false;
    for (char c : rel) {
        if (c == '\\') c = '/';
        if (c == '/') {
            if (prevSlash || out.empty()) continue;
            prevSlash = true;
            out.push_back('/');
        } else {
            prevSlash = false;
            out.push_back(c);
        }
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

// 리틀엔디안 정수 append/read 헬퍼.
template <class T>
void PutLE(std::vector<std::byte>& b, T v) {
    for (size_t i = 0; i < sizeof(T); ++i)
        b.push_back(static_cast<std::byte>((static_cast<uint64_t>(v) >> (i * 8)) & 0xFF));
}
template <class T>
T GetLE(const std::byte* p) {
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<uint64_t>(static_cast<uint8_t>(p[i])) << (i * 8);
    return static_cast<T>(v);
}

} // namespace

// ---------------------------------------------------------------------------
// PakWriter
// ---------------------------------------------------------------------------
void PakWriter::AddFile(std::string relPath, Blob data) {
    m_entries.push_back({Normalize(relPath), std::move(data)});
}

int PakWriter::Count() const { return static_cast<int>(m_entries.size()); }

Expected<int, Error> PakWriter::AddDirectory(std::string_view rootDir) {
    fs::path base = Utf8ToPath(rootDir);
    std::error_code ec;
    if (!fs::is_directory(base, ec))
        return Error{"PakWriter::AddDirectory: not a directory '" + std::string(rootDir) + "'", 1};

    int added = 0;
    for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::error_code relEc;
        fs::path rel = fs::relative(it->path(), base, relEc);
        if (relEc) continue;

        std::ifstream in(it->path(), std::ios::binary | std::ios::ate);
        if (!in) continue;
        const std::streamsize size = in.tellg();
        in.seekg(0);
        Blob blob(static_cast<size_t>(size < 0 ? 0 : size));
        if (size > 0) in.read(reinterpret_cast<char*>(blob.data()), size);
        AddFile(PathToUtf8(rel), std::move(blob));
        ++added;
    }
    return added;
}

Expected<void, Error> PakWriter::Write(std::string_view outPath) const {
    // 데이터 오프셋 배치: 헤더 직후부터 블롭을 연속 배치, 그 뒤에 TOC.
    uint64_t offset = kHeaderSize;
    std::vector<uint64_t> offsets(m_entries.size());
    for (size_t i = 0; i < m_entries.size(); ++i) {
        offsets[i] = offset;
        offset += m_entries[i].data.size();
    }
    const uint64_t tocOffset = offset;

    std::vector<std::byte> header;
    header.reserve(kHeaderSize);
    for (char c : kMagic) header.push_back(static_cast<std::byte>(c));
    PutLE<uint32_t>(header, kVersion);
    PutLE<uint32_t>(header, static_cast<uint32_t>(m_entries.size()));
    PutLE<uint64_t>(header, tocOffset);
    PutLE<uint64_t>(header, 0);   // reserved
    // 헤더 크기 보정(정확히 32B).
    header.resize(kHeaderSize, std::byte{0});

    std::vector<std::byte> toc;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const auto& e = m_entries[i];
        PutLE<uint32_t>(toc, static_cast<uint32_t>(e.path.size()));
        for (char c : e.path) toc.push_back(static_cast<std::byte>(c));
        PutLE<uint64_t>(toc, offsets[i]);
        PutLE<uint64_t>(toc, static_cast<uint64_t>(e.data.size()));
    }

    fs::path out = Utf8ToPath(outPath);
    std::error_code ec;
    if (out.has_parent_path()) fs::create_directories(out.parent_path(), ec);
    std::ofstream os(out, std::ios::binary | std::ios::trunc);
    if (!os) return Error{"PakWriter::Write: cannot open '" + std::string(outPath) + "'", 2};

    os.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (const auto& e : m_entries)
        if (!e.data.empty())
            os.write(reinterpret_cast<const char*>(e.data.data()), static_cast<std::streamsize>(e.data.size()));
    os.write(reinterpret_cast<const char*>(toc.data()), static_cast<std::streamsize>(toc.size()));
    if (!os) return Error{"PakWriter::Write: write failed '" + std::string(outPath) + "'", 3};
    return {};
}

// ---------------------------------------------------------------------------
// PakFileSystem
// ---------------------------------------------------------------------------
struct PakFileSystem::Impl {
    std::string pakPath;
    bool valid = false;
    struct Ent { uint64_t offset; uint64_t size; };
    std::unordered_map<std::string, Ent> toc;
};

PakFileSystem::PakFileSystem() : m_impl(std::make_unique<Impl>()) {}
PakFileSystem::~PakFileSystem() = default;

Expected<std::unique_ptr<PakFileSystem>, Error> PakFileSystem::Open(std::string pakPath) {
    auto self = std::unique_ptr<PakFileSystem>(new PakFileSystem());
    self->m_impl->pakPath = pakPath;

    std::ifstream in(Utf8ToPath(pakPath), std::ios::binary);
    if (!in) return Error{"PakFileSystem::Open: cannot open '" + pakPath + "'", 1};

    std::byte header[kHeaderSize];
    in.read(reinterpret_cast<char*>(header), kHeaderSize);
    if (in.gcount() != static_cast<std::streamsize>(kHeaderSize))
        return Error{"PakFileSystem::Open: truncated header '" + pakPath + "'", 2};
    if (std::memcmp(header, kMagic, sizeof(kMagic)) != 0)
        return Error{"PakFileSystem::Open: bad magic '" + pakPath + "'", 3};
    const uint32_t version = GetLE<uint32_t>(header + 8);
    if (version != kVersion)
        return Error{"PakFileSystem::Open: unsupported version", 4};
    const uint32_t count = GetLE<uint32_t>(header + 12);
    const uint64_t tocOffset = GetLE<uint64_t>(header + 16);

    in.seekg(static_cast<std::streamoff>(tocOffset));
    if (!in) return Error{"PakFileSystem::Open: bad tocOffset '" + pakPath + "'", 5};

    self->m_impl->toc.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::byte lenB[4];
        in.read(reinterpret_cast<char*>(lenB), 4);
        if (in.gcount() != 4) return Error{"PakFileSystem::Open: truncated TOC", 6};
        const uint32_t pathLen = GetLE<uint32_t>(lenB);
        std::string path(pathLen, '\0');
        if (pathLen > 0) in.read(path.data(), pathLen);
        std::byte offB[8], sizB[8];
        in.read(reinterpret_cast<char*>(offB), 8);
        in.read(reinterpret_cast<char*>(sizB), 8);
        if (!in) return Error{"PakFileSystem::Open: truncated TOC entry", 7};
        self->m_impl->toc.emplace(Normalize(path),
                                  Impl::Ent{GetLE<uint64_t>(offB), GetLE<uint64_t>(sizB)});
    }

    self->m_impl->valid = true;
    MYE_LOG_INFO("Asset", "pak mounted '{}' ({} entries)", pakPath, count);
    return self;
}

bool PakFileSystem::IsValid() const { return m_impl->valid; }
int  PakFileSystem::Count() const { return static_cast<int>(m_impl->toc.size()); }

bool PakFileSystem::Exists(std::string_view path) const {
    return m_impl->toc.find(Normalize(path)) != m_impl->toc.end();
}

Expected<Blob, Error> PakFileSystem::ReadAll(std::string_view path) {
    auto it = m_impl->toc.find(Normalize(path));
    if (it == m_impl->toc.end())
        return Error{"PakFileSystem::ReadAll: not found '" + std::string(path) + "'", 1};

    std::ifstream in(Utf8ToPath(m_impl->pakPath), std::ios::binary);
    if (!in) return Error{"PakFileSystem::ReadAll: reopen failed", 2};
    in.seekg(static_cast<std::streamoff>(it->second.offset));
    Blob blob(static_cast<size_t>(it->second.size));
    if (it->second.size > 0) {
        in.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(it->second.size));
        if (in.gcount() != static_cast<std::streamsize>(it->second.size))
            return Error{"PakFileSystem::ReadAll: short read '" + std::string(path) + "'", 3};
    }
    return blob;
}

void PakFileSystem::Enumerate(std::string_view dir, const EnumerateFn& fn) const {
    const std::string prefix = Normalize(dir);
    for (const auto& [path, ent] : m_impl->toc) {
        (void)ent;
        if (prefix.empty() || path == prefix ||
            (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
             path[prefix.size()] == '/')) {
            fn(path);
        }
    }
}

} // namespace mye::asset
