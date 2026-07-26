// PakFileTests.cpp — .pak 패킹/마운트 왕복 검증 (docs/04 배포)
#include "TestFramework.h"

#include "mye/asset/PakFile.h"
#include "mye/asset/FileSystem.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::asset;

namespace {
Blob B(const std::vector<unsigned char>& bytes) {
    Blob b(bytes.size());
    for (size_t i = 0; i < bytes.size(); ++i) b[i] = static_cast<std::byte>(bytes[i]);
    return b;
}
Blob Text(const char* s) {
    const size_t n = std::strlen(s);
    Blob b(n);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<std::byte>(s[i]);
    return b;
}
} // namespace

MYE_TEST(PakWriteReadRoundTrip) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "mye_pak_roundtrip.pak";
    std::error_code ec;
    fs::remove(tmp, ec);
    const std::string tmpUtf8 = tmp.string();

    const Blob a = Text("hello world");
    const Blob b = B({0x00, 0x01, 0x02, 0xFF, 0x10, 0x00, 0x7E});  // 임베드 NUL 포함 바이너리
    const Blob empty;                                              // 빈 파일 엣지

    PakWriter w;
    w.AddFile("a.txt", a);
    w.AddFile("sub/b.bin", b);
    w.AddFile("sub/empty.dat", empty);
    MYE_EXPECT(w.Count() == 3);
    MYE_EXPECT(static_cast<bool>(w.Write(tmpUtf8)));

    auto opened = PakFileSystem::Open(tmpUtf8);
    MYE_EXPECT(static_cast<bool>(opened));
    auto& pak = *opened.Value();
    MYE_EXPECT(pak.IsValid());
    MYE_EXPECT(pak.Count() == 3);

    // Exists.
    MYE_EXPECT(pak.Exists("a.txt"));
    MYE_EXPECT(pak.Exists("sub/b.bin"));
    MYE_EXPECT(pak.Exists("sub/empty.dat"));
    MYE_EXPECT(!pak.Exists("missing.txt"));
    // 경로 정규화(역슬래시·중복 슬래시)도 매치.
    MYE_EXPECT(pak.Exists("sub\\b.bin"));

    // ReadAll 바이트 동일성.
    auto ra = pak.ReadAll("a.txt");
    MYE_EXPECT(static_cast<bool>(ra));
    MYE_EXPECT(ra.Value() == a);
    auto rb = pak.ReadAll("sub/b.bin");
    MYE_EXPECT(static_cast<bool>(rb));
    MYE_EXPECT(rb.Value() == b);
    auto re = pak.ReadAll("sub/empty.dat");
    MYE_EXPECT(static_cast<bool>(re));
    MYE_EXPECT(re.Value().empty());

    // Enumerate(prefix).
    int subCount = 0;
    pak.Enumerate("sub", [&](std::string_view) { ++subCount; });
    MYE_EXPECT(subCount == 2);
    int allCount = 0;
    pak.Enumerate("", [&](std::string_view) { ++allCount; });
    MYE_EXPECT(allCount == 3);

    fs::remove(tmp, ec);
}

MYE_TEST(PakMountThroughVfs) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "mye_pak_vfs.pak";
    std::error_code ec;
    fs::remove(tmp, ec);
    const std::string tmpUtf8 = tmp.string();

    const Blob data = Text("packed-asset");
    PakWriter w;
    w.AddFile("textures/logo.png", data);
    MYE_EXPECT(static_cast<bool>(w.Write(tmpUtf8)));

    auto opened = PakFileSystem::Open(tmpUtf8);
    MYE_EXPECT(static_cast<bool>(opened));

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::move(opened.Value()), /*priority*/ 10);

    MYE_EXPECT(vfs.Exists("assets://textures/logo.png"));
    auto r = vfs.ReadAll("assets://textures/logo.png");
    MYE_EXPECT(static_cast<bool>(r));
    MYE_EXPECT(r.Value() == data);

    fs::remove(tmp, ec);
}

MYE_TEST(PakOpenRejectsBadFile) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "mye_pak_bad.pak";
    {
        std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
        os << "not a pak file at all";
    }
    auto opened = PakFileSystem::Open(tmp.string());
    MYE_EXPECT(!opened);   // 잘못된 매직 → 실패
    std::error_code ec;
    fs::remove(tmp, ec);
}
