// AssetTests.cpp — mye_asset 검증 (04): AssetGuid·.meta 왕복·VFS 마운트·PNG 디코드·핸들 캐시
//
// GPU 없는 단위 테스트만 둔다(TextureImporter::Upload는 실디바이스 필요 → M1-B 통합 데모).
// PNG는 테스트 내에서 최소 인코더(무압축 DEFLATE stored 블록)로 알려진 4×4 패턴을 만들어
// stb 디코드 결과(+premultiply)를 정확히 대조한다.
#include "TestFramework.h"

#include "mye/asset/AssetGuid.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetMeta.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/asset/Texture.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mye;
using namespace mye::asset;

// ===========================================================================
// 최소 PNG 인코더 (테스트 전용) — 무압축(zlib stored) RGBA8. stb로 디코드 가능.
// ===========================================================================
namespace {

uint32_t Crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void PushBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void WriteChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PushBE32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> typeAndData;
    typeAndData.insert(typeAndData.end(), type, type + 4);
    typeAndData.insert(typeAndData.end(), data.begin(), data.end());
    out.insert(out.end(), typeAndData.begin(), typeAndData.end());
    PushBE32(out, Crc32(typeAndData.data(), typeAndData.size()));
}

// rgba: width*height*4 straight-alpha 바이트. 무압축 PNG(색타입 6, 8bit) 생성.
std::vector<uint8_t> EncodePng(uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    // IHDR
    std::vector<uint8_t> ihdr;
    PushBE32(ihdr, width);
    PushBE32(ihdr, height);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // color type RGBA
    ihdr.push_back(0);   // compression
    ihdr.push_back(0);   // filter
    ihdr.push_back(0);   // interlace
    WriteChunk(png, "IHDR", ihdr);

    // 필터 바이트(0=None) + 스캔라인으로 raw 데이터 구성.
    std::vector<uint8_t> raw;
    raw.reserve((width * 4 + 1) * height);
    for (uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);   // filter: None
        const uint8_t* row = rgba.data() + static_cast<size_t>(y) * width * 4;
        raw.insert(raw.end(), row, row + width * 4);
    }

    // zlib stored: 2바이트 헤더(0x78 0x01) + stored 블록들 + adler32.
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        size_t blockLen = std::min<size_t>(65535, raw.size() - offset);
        bool last = (offset + blockLen) >= raw.size();
        zlib.push_back(last ? 1 : 0);   // BFINAL, BTYPE=00(stored)
        uint16_t len = static_cast<uint16_t>(blockLen);
        uint16_t nlen = static_cast<uint16_t>(~len);
        zlib.push_back(static_cast<uint8_t>(len & 0xFF));
        zlib.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLen);
        offset += blockLen;
    }
    PushBE32(zlib, Adler32(raw.data(), raw.size()));

    WriteChunk(png, "IDAT", zlib);
    WriteChunk(png, "IEND", {});
    return png;
}

std::vector<std::byte> ToBytes(const std::vector<uint8_t>& v) {
    std::vector<std::byte> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) out[i] = static_cast<std::byte>(v[i]);
    return out;
}

// 알려진 4×4 패턴: 각 픽셀 (R,G,B,A). 알파를 다양하게 두어 premultiply를 검증.
std::vector<uint8_t> MakePattern4x4() {
    std::vector<uint8_t> px;
    px.reserve(4 * 4 * 4);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            uint8_t r = static_cast<uint8_t>(x * 64 + 15);
            uint8_t g = static_cast<uint8_t>(y * 64 + 15);
            uint8_t b = static_cast<uint8_t>((x + y) * 16);
            uint8_t a = static_cast<uint8_t>((x == 0 && y == 0) ? 0 : (x + y) % 2 ? 128 : 255);
            px.push_back(r);
            px.push_back(g);
            px.push_back(b);
            px.push_back(a);
        }
    }
    return px;
}

int g_tempCounter = 0;

std::filesystem::path MakeTempDir() {
    auto base = std::filesystem::temp_directory_path() /
                ("mye_asset_test_" + std::to_string(g_tempCounter++));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

} // namespace

// ===========================================================================
// AssetGuid
// ===========================================================================
MYE_TEST(AssetGuidRoundTrip) {
    AssetGuid g = AssetGuid::Generate();
    MYE_EXPECT(g.IsValid());

    std::string s = g.ToString();
    MYE_EXPECT(s.size() == 36);
    MYE_EXPECT(s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-');
    // 버전4 표식: 15번째 문자(인덱스 14)는 '4'.
    MYE_EXPECT(s[14] == '4');
    // variant: 인덱스 19는 8/9/a/b 중 하나.
    MYE_EXPECT(s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');

    auto parsed = AssetGuid::FromString(s);
    MYE_EXPECT(parsed);
    MYE_EXPECT(parsed.Value() == g);

    // 두 번 생성하면 다르다.
    MYE_EXPECT(!(AssetGuid::Generate() == AssetGuid::Generate()));

    // 잘못된 형식 거부.
    MYE_EXPECT(!AssetGuid::FromString("not-a-guid"));
    MYE_EXPECT(!AssetGuid::FromString("00000000-0000-0000-0000-00000000000"));    // 35자
    MYE_EXPECT(!AssetGuid::FromString("00000000_0000_0000_0000_000000000000"));   // 잘못된 구분자
    MYE_EXPECT(!AssetGuid::FromString("zzzzzzzz-0000-0000-0000-000000000000"));   // 비hex
}

MYE_TEST(AssetGuidKnownStringParse) {
    auto g = AssetGuid::FromString("0123456789abcdef-fedc-ba98-7654-3210aabbccdd");
    // 하이픈 위치가 규약과 다르므로 실패해야 한다.
    MYE_EXPECT(!g);

    auto g2 = AssetGuid::FromString("01234567-89ab-cdef-fedc-ba9876543210");
    MYE_EXPECT(g2);
    MYE_EXPECT(g2.Value().hi == 0x0123456789abcdefull);
    MYE_EXPECT(g2.Value().lo == 0xfedcba9876543210ull);
    // 라운드트립 문자열 동일.
    MYE_EXPECT(g2.Value().ToString() == "01234567-89ab-cdef-fedc-ba9876543210");
}

// ===========================================================================
// AssetMeta 왕복
// ===========================================================================
MYE_TEST(AssetMetaRoundTrip) {
    AssetMeta m = AssetMeta::CreateFor("TextureImporter", 3);
    MYE_EXPECT(m.guid.IsValid());
    MYE_EXPECT(m.importer == "TextureImporter");
    MYE_EXPECT(m.importerVersion == 3);

    // settings에 임의 JSON.
    json::Value::Object settings;
    settings.emplace("premultiplyAlpha", json::Value(true));
    settings.emplace("filter", json::Value(std::string("Point")));
    m.settings = json::Value(std::move(settings));

    // 서브 에셋 2개.
    AssetGuid sub1 = AssetGuid::Generate();
    AssetGuid sub2 = AssetGuid::Generate();
    m.subAssets.emplace("sheet", sub1);
    m.subAssets.emplace("anim/idle", sub2);

    std::string text = m.Stringify();
    auto parsed = AssetMeta::Parse(text);
    MYE_EXPECT(parsed);
    if (parsed) {
        const AssetMeta& p = parsed.Value();
        MYE_EXPECT(p.guid == m.guid);
        MYE_EXPECT(p.importer == "TextureImporter");
        MYE_EXPECT(p.importerVersion == 3);
        MYE_EXPECT(p.settings.Find("premultiplyAlpha") != nullptr &&
                   p.settings.Find("premultiplyAlpha")->AsBool() == true);
        MYE_EXPECT(p.settings.Find("filter") != nullptr &&
                   p.settings.Find("filter")->AsString() == std::string_view("Point"));
        MYE_EXPECT(p.subAssets.size() == 2);
        MYE_EXPECT(p.subAssets.count("sheet") == 1 && p.subAssets.at("sheet") == sub1);
        MYE_EXPECT(p.subAssets.count("anim/idle") == 1 && p.subAssets.at("anim/idle") == sub2);
    }

    // 잘못된 .meta 거부.
    MYE_EXPECT(!AssetMeta::Parse("{}"));                                  // guid 없음
    MYE_EXPECT(!AssetMeta::Parse(R"({"guid":"bad"})"));                   // guid 파싱 실패
    MYE_EXPECT(!AssetMeta::Parse(R"({"guid":"01234567-89ab-cdef-fedc-ba9876543210"})"));  // importer 없음
}

// ===========================================================================
// VFS 마운트·경로 해석
// ===========================================================================
MYE_TEST(VfsSplitVpath) {
    auto ok = VirtualFileSystem::SplitVpath("assets://sprites/hero.png");
    MYE_EXPECT(ok);
    MYE_EXPECT(ok.Value().first == "assets");
    MYE_EXPECT(ok.Value().second == "sprites/hero.png");

    // 역슬래시·중복 슬래시 정규화.
    auto norm = VirtualFileSystem::SplitVpath("assets://a\\\\b//c.png");
    MYE_EXPECT(norm);
    MYE_EXPECT(norm.Value().second == "a/b/c.png");

    MYE_EXPECT(!VirtualFileSystem::SplitVpath("no-scheme/path"));
    MYE_EXPECT(!VirtualFileSystem::SplitVpath("://empty-mount"));
}

MYE_TEST(VfsLooseFileSystemReadAndMount) {
    auto dir = MakeTempDir();
    std::filesystem::create_directories(dir / "sub");
    // 파일 2개 기록.
    {
        std::ofstream f(dir / "hello.txt", std::ios::binary);
        f << "hello world";
    }
    {
        std::ofstream f(dir / "sub" / "data.bin", std::ios::binary);
        const char bytes[] = {0x01, 0x02, 0x03, 0x04};
        f.write(bytes, 4);
    }

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dir.string()), 0);

    MYE_EXPECT(vfs.Exists("assets://hello.txt"));
    MYE_EXPECT(vfs.Exists("assets://sub/data.bin"));
    MYE_EXPECT(!vfs.Exists("assets://missing.txt"));
    MYE_EXPECT(!vfs.Exists("other://hello.txt"));   // 마운트 없음

    auto blob = vfs.ReadAll("assets://hello.txt");
    MYE_EXPECT(blob);
    if (blob) {
        std::string s(reinterpret_cast<const char*>(blob.Value().data()), blob.Value().size());
        MYE_EXPECT(s == "hello world");
    }

    auto bin = vfs.ReadAll("assets://sub/data.bin");
    MYE_EXPECT(bin && bin.Value().size() == 4);
    if (bin) MYE_EXPECT(static_cast<uint8_t>(bin.Value()[0]) == 0x01);

    // 디렉터리 탈출 거부.
    MYE_EXPECT(!vfs.ReadAll("assets://../secret"));

    // 존재하지 않는 파일.
    MYE_EXPECT(!vfs.ReadAll("assets://nope.txt"));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

MYE_TEST(VfsMountPriority) {
    auto dirLow = MakeTempDir() / "low";
    auto dirHigh = MakeTempDir() / "high";
    std::filesystem::create_directories(dirLow);
    std::filesystem::create_directories(dirHigh);
    { std::ofstream f(dirLow / "x.txt"); f << "LOW"; }
    { std::ofstream f(dirHigh / "x.txt"); f << "HIGH"; }

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dirLow.string()), 0);
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dirHigh.string()), 100);

    auto blob = vfs.ReadAll("assets://x.txt");
    MYE_EXPECT(blob);
    if (blob) {
        std::string s(reinterpret_cast<const char*>(blob.Value().data()), blob.Value().size());
        MYE_EXPECT(s == "HIGH");   // 우선순위 높은 마운트가 이긴다.
    }

    std::error_code ec;
    std::filesystem::remove_all(dirLow.parent_path(), ec);
    std::filesystem::remove_all(dirHigh.parent_path(), ec);
}

// ===========================================================================
// PNG 디코드 + premultiply
// ===========================================================================
MYE_TEST(TextureImporterDecodePngStraight) {
    auto pattern = MakePattern4x4();
    auto png = EncodePng(4, 4, pattern);
    auto bytes = ToBytes(png);

    auto decoded = TextureImporter::DecodePng(bytes, /*premultiplyAlpha=*/false);
    MYE_EXPECT(decoded);
    if (decoded) {
        const DecodedImage& img = decoded.Value();
        MYE_EXPECT(img.width == 4 && img.height == 4);
        MYE_EXPECT(img.pixels.size() == 4 * 4 * 4);
        // straight: 원본 그대로.
        bool match = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (static_cast<uint8_t>(img.pixels[i]) != pattern[i]) { match = false; break; }
        }
        MYE_EXPECT(match);
    }
}

MYE_TEST(TextureImporterDecodePngPremultiplied) {
    auto pattern = MakePattern4x4();
    auto png = EncodePng(4, 4, pattern);
    auto bytes = ToBytes(png);

    auto decoded = TextureImporter::DecodePng(bytes, /*premultiplyAlpha=*/true);
    MYE_EXPECT(decoded);
    if (decoded) {
        const DecodedImage& img = decoded.Value();
        MYE_EXPECT(img.width == 4 && img.height == 4);
        bool match = true;
        for (size_t p = 0; p < 16; ++p) {
            uint8_t r = pattern[p * 4 + 0];
            uint8_t g = pattern[p * 4 + 1];
            uint8_t b = pattern[p * 4 + 2];
            uint8_t a = pattern[p * 4 + 3];
            auto pm = [a](uint8_t c) -> uint8_t {
                return static_cast<uint8_t>((static_cast<uint32_t>(c) * a + 127u) / 255u);
            };
            uint8_t er = pm(r), eg = pm(g), eb = pm(b);
            const std::byte* d = img.pixels.data() + p * 4;
            if (static_cast<uint8_t>(d[0]) != er || static_cast<uint8_t>(d[1]) != eg ||
                static_cast<uint8_t>(d[2]) != eb || static_cast<uint8_t>(d[3]) != a) {
                match = false;
                break;
            }
        }
        MYE_EXPECT(match);
        // 알파 0 픽셀(0,0)은 RGB도 0.
        MYE_EXPECT(static_cast<uint8_t>(img.pixels[0]) == 0 &&
                   static_cast<uint8_t>(img.pixels[1]) == 0 &&
                   static_cast<uint8_t>(img.pixels[2]) == 0 &&
                   static_cast<uint8_t>(img.pixels[3]) == 0);
    }

    // 쓰레기 입력 거부.
    std::vector<std::byte> garbage(32, std::byte{0xAB});
    MYE_EXPECT(!TextureImporter::DecodePng(garbage, false));
    MYE_EXPECT(!TextureImporter::DecodePng({}, false));
}

// ===========================================================================
// AssetManager 핸들 refcount·캐시 공유
// ===========================================================================
// 주: LoadSync<T>는 mye_asset TU에서 Texture만 명시적 인스턴스화된다(헤더에 정의 없음).
// GPU 없는 커스텀 타입을 테스트에서 새로 인스턴스화할 수 없으므로, 캐시/refcount/실패 경로는
// 공개 슬롯 API(GetSlot/Retain/Release)와 Texture 핸들 경로로 검증한다.

MYE_TEST(AssetHandleSlotApiSafety) {
    VirtualFileSystem vfs;
    AssetManager mgr(vfs, /*device=*/nullptr);

    // 존재하지 않는 슬롯은 nullptr.
    MYE_EXPECT(mgr.GetSlot(0, 1) == nullptr);
    MYE_EXPECT(mgr.GetSlot(999, 1) == nullptr);

    // dangling: 없는 슬롯의 Retain/Release는 무해.
    mgr.RetainSlot(0, 1);
    mgr.ReleaseSlot(0, 1);
}

MYE_TEST(AssetManagerLoadSyncNoImporterFails) {
    auto dir = MakeTempDir();
    { std::ofstream f(dir / "hero.png", std::ios::binary); f << "PNGDATA"; }

    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dir.string()), 0);
    AssetManager mgr(vfs, /*device=*/nullptr);

    // 임포터 미등록 → Failed 핸들.
    AssetHandle<Texture> h = mgr.LoadSync<Texture>("assets://hero.png");
    MYE_EXPECT(h.IsValid());
    MYE_EXPECT(h.State() == AssetState::Failed);
    MYE_EXPECT(h.Get() == nullptr);

    // 같은 경로 재요청 → 캐시된 Failed 슬롯 공유(같은 상태).
    AssetHandle<Texture> h2 = mgr.LoadSync<Texture>("assets://hero.png");
    MYE_EXPECT(h2.State() == AssetState::Failed);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

MYE_TEST(AssetManagerReadFailFails) {
    auto dir = MakeTempDir();
    VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<LooseFileSystem>(dir.string()), 0);
    AssetManager mgr(vfs, nullptr);
    mgr.RegisterImporter(std::make_unique<TextureImporter>());

    // 파일 없음 → Failed.
    AssetHandle<Texture> h = mgr.LoadSync<Texture>("assets://ghost.png");
    MYE_EXPECT(h.State() == AssetState::Failed);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
