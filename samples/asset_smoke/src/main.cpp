// asset_smoke — M1 토대 에셋 경로 GPU 스모크 테스트 (docs/04)
//
// 목적: 실제 PNG 파일을 assets 폴더에 만들고, AssetManager로 로드해
//   1) TextureHandle 유효성, 2) width/height 정확성, 3) 픽셀 왕복(premultiply 규약 포함)을
// 실디바이스(DX11 headless) 경로로 검증한다. 단위 테스트(mye_tests)는 GPU 없는 CPU 디코드만
// 커버하므로, 이 exe가 Upload(GPU) 경로 + CaptureBackbuffer 리드백까지 실제로 밟는다.
//
// 리드백: rhi::CaptureBackbuffer(개발·검증 전용, 스테이징 복사 + Map(READ))로 GPU 텍스처를
//   BMP(top-down BGRA)로 저장 → 다시 읽어 원본 픽셀(premultiplied)과 대조.
//
// 종료 코드: 0=통과, 그 외=실패(단계별 코드). 콘솔에 구체 수치 출력.
#include "mye/asset/AssetManager.h"
#include "mye/asset/AssetHandle.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Importer.h"
#include "mye/asset/Texture.h"
#include "mye/rhi/Rhi.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// ---- 최소 PNG 인코더(무압축 zlib stored, RGBA8) — AssetTests.cpp와 동일 알고리즘 ----
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
    for (size_t i = 0; i < len; ++i) { a = (a + data[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}
void PushBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t((x >> 24) & 0xFF)); v.push_back(uint8_t((x >> 16) & 0xFF));
    v.push_back(uint8_t((x >> 8) & 0xFF)); v.push_back(uint8_t(x & 0xFF));
}
void WriteChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PushBE32(out, uint32_t(data.size()));
    std::vector<uint8_t> td;
    td.insert(td.end(), type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    PushBE32(out, Crc32(td.data(), td.size()));
}
std::vector<uint8_t> EncodePng(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    PushBE32(ihdr, w); PushBE32(ihdr, h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    WriteChunk(png, "IHDR", ihdr);
    std::vector<uint8_t> raw;
    raw.reserve((w * 4 + 1) * h);
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba.data() + size_t(y) * w * 4;
        raw.insert(raw.end(), row, row + w * 4);
    }
    std::vector<uint8_t> zlib; zlib.push_back(0x78); zlib.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t bl = (std::min)(size_t(65535), raw.size() - off);
        bool last = (off + bl) >= raw.size();
        zlib.push_back(last ? 1 : 0);
        uint16_t len = uint16_t(bl), nlen = uint16_t(~len);
        zlib.push_back(uint8_t(len & 0xFF)); zlib.push_back(uint8_t((len >> 8) & 0xFF));
        zlib.push_back(uint8_t(nlen & 0xFF)); zlib.push_back(uint8_t((nlen >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + off, raw.begin() + off + bl);
        off += bl;
    }
    PushBE32(zlib, Adler32(raw.data(), raw.size()));
    WriteChunk(png, "IDAT", zlib);
    WriteChunk(png, "IEND", {});
    return png;
}

// 알려진 8×6 패턴(다양한 알파로 premultiply 검증). AssetTests의 4×4보다 큰 비정사각 크기를
// 써서 width/height·rowPitch 스위즐까지 검증한다.
constexpr uint32_t kW = 8, kH = 6;
std::vector<uint8_t> MakePattern() {
    std::vector<uint8_t> px; px.reserve(kW * kH * 4);
    for (uint32_t y = 0; y < kH; ++y)
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t r = uint8_t(x * 30 + 10);
            uint8_t g = uint8_t(y * 40 + 5);
            uint8_t b = uint8_t((x * y) & 0xFF);
            uint8_t a = uint8_t((x == 0 && y == 0) ? 0 : (x + y) % 3 == 0 ? 128 : 255);
            px.push_back(r); px.push_back(g); px.push_back(b); px.push_back(a);
        }
    return px;
}

// BMP(32bpp top-down BGRA, CaptureBackbuffer 포맷) 읽어 BGRA 픽셀 벡터 반환.
struct Bmp { uint32_t w = 0, h = 0; std::vector<uint8_t> bgra; };
bool ReadBmp(const std::filesystem::path& p, Bmp& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 54 || buf[0] != 'B' || buf[1] != 'M') return false;
    auto rd32 = [&](size_t o) {
        return uint32_t(buf[o]) | (uint32_t(buf[o + 1]) << 8) | (uint32_t(buf[o + 2]) << 16) |
               (uint32_t(buf[o + 3]) << 24);
    };
    uint32_t dataOff = rd32(10);
    int32_t bw = int32_t(rd32(18));
    int32_t bh = int32_t(rd32(22));
    out.w = uint32_t(bw);
    out.h = uint32_t(bh < 0 ? -bh : bh);
    size_t need = dataOff + size_t(out.w) * out.h * 4;
    if (buf.size() < need) return false;
    out.bgra.assign(buf.begin() + dataOff, buf.begin() + need);
    return true; // CaptureBackbuffer는 top-down(음수 height)으로 저장 → 행 순서 그대로.
}

uint8_t Premul(uint8_t c, uint8_t a) { return uint8_t((uint32_t(c) * a + 127u) / 255u); }

} // namespace

int main() {
    using namespace mye;
    namespace fs = std::filesystem;

    int stage = 0;
    auto fail = [&](const char* msg) {
        std::printf("[asset_smoke] FAIL(stage=%d): %s\n", stage, msg);
        return stage;
    };

    // 0) 작업 디렉터리: temp/mye_asset_smoke/assets 에 hero.png 기록.
    stage = 1;
    fs::path root = fs::temp_directory_path() / "mye_asset_smoke";
    fs::path assetsDir = root / "assets";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(assetsDir, ec);
    if (ec) return fail("cannot create assets dir");

    std::vector<uint8_t> pattern = MakePattern();
    std::vector<uint8_t> png = EncodePng(kW, kH, pattern);
    fs::path pngPath = assetsDir / "hero.png";
    {
        std::ofstream f(pngPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    }
    if (!fs::exists(pngPath)) return fail("png not written");
    std::printf("[asset_smoke] wrote %s (%zu bytes, %ux%u RGBA)\n",
                pngPath.string().c_str(), png.size(), kW, kH);

    // 1) DX11 디바이스(headless — 스왑체인 없이 텍스처만).
    stage = 2;
    auto devRes = rhi::CreateDevice(rhi::Backend::DX11, {});
    if (!devRes) return fail(devRes.GetError().message.c_str());
    std::unique_ptr<rhi::IDevice> device = std::move(devRes).Value();

    // 2) VFS 마운트 + AssetManager + PNG 임포터.
    stage = 3;
    asset::VirtualFileSystem vfs;
    vfs.Mount("assets", std::make_unique<asset::LooseFileSystem>(assetsDir.string()), 0);
    asset::AssetManager mgr(vfs, device.get());
    mgr.RegisterImporter(std::make_unique<asset::TextureImporter>());

    // 3) 동기 로드.
    stage = 4;
    asset::AssetHandle<asset::Texture> h = mgr.LoadSync<asset::Texture>("assets://hero.png");
    if (!h.IsValid()) return fail("handle invalid");
    if (h.State() != asset::AssetState::Loaded)
        return fail("asset not Loaded (state != Loaded)");
    asset::Texture* tex = h.Get();
    if (!tex) return fail("Texture::Get() nullptr");
    if (!tex->gpuTexture.IsValid()) return fail("gpuTexture handle invalid");
    if (!tex->sampler.IsValid()) return fail("sampler handle invalid");
    if (tex->width != kW || tex->height != kH) {
        std::printf("  got %ux%u expected %ux%u\n", tex->width, tex->height, kW, kH);
        return fail("texture size mismatch");
    }
    std::printf("[asset_smoke] loaded: gpuTexture valid, sampler valid, %ux%u, premul=%d\n",
                tex->width, tex->height, int(tex->settings.premultiplyAlpha));

    // 4) GPU 리드백: CaptureBackbuffer로 텍스처 → BMP.
    stage = 5;
    fs::path bmpPath = root / "readback.bmp";
    auto cap = rhi::CaptureBackbuffer(*device, tex->gpuTexture, bmpPath.string());
    if (!cap) return fail(cap.GetError().message.c_str());

    Bmp bmp;
    if (!ReadBmp(bmpPath, bmp)) return fail("cannot read back BMP");
    if (bmp.w != kW || bmp.h != kH) return fail("BMP size mismatch");

    // 5) 픽셀 대조: 기대값 = premultiply(pattern) (설정 기본 premultiplyAlpha=true).
    //    BMP는 BGRA. 원본은 RGBA straight → premul 후 RGB, 알파 그대로.
    stage = 6;
    int mismatches = 0;
    for (uint32_t i = 0; i < kW * kH; ++i) {
        uint8_t sr = pattern[i * 4 + 0], sg = pattern[i * 4 + 1];
        uint8_t sb = pattern[i * 4 + 2], sa = pattern[i * 4 + 3];
        uint8_t er = Premul(sr, sa), eg = Premul(sg, sa), eb = Premul(sb, sa);
        const uint8_t* px = bmp.bgra.data() + size_t(i) * 4; // B,G,R,A
        uint8_t gb = px[0], gg = px[1], gr = px[2], ga = px[3];
        if (gr != er || gg != eg || gb != eb || ga != sa) {
            if (mismatches < 4)
                std::printf("  px%u got BGRA(%u,%u,%u,%u) expect RGBA(%u,%u,%u,%u)\n",
                            i, gb, gg, gr, ga, er, eg, eb, sa);
            ++mismatches;
        }
    }
    if (mismatches != 0) {
        std::printf("[asset_smoke] %d/%u pixels mismatched\n", mismatches, kW * kH);
        return fail("pixel round-trip mismatch");
    }

    // 알파 0 픽셀(0,0)은 premultiplied RGB=0.
    if (!(bmp.bgra[0] == 0 && bmp.bgra[1] == 0 && bmp.bgra[2] == 0 && bmp.bgra[3] == 0))
        return fail("alpha-0 pixel RGB not zeroed");

    std::printf("[asset_smoke] PASS: %u/%u pixels match (premultiplied round-trip), alpha-0 RGB=0\n",
                kW * kH, kW * kH);

    // 정리(선택). 핸들 스코프 종료 → refcount 0 → 언로드(AssetManager가 GPU 리소스 반납).
    fs::remove_all(root, ec);
    return 0;
}
