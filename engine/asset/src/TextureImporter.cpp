// mye/asset/TextureImporter.cpp — PNG 임포터 (docs/04 §임포터, docs/02 픽셀아트 규약)
//
// STB_IMAGE_IMPLEMENTATION은 이 TU 하나에서만 켠다(third_party/stb 소비 규약).
// 경로: PNG 바이트 → stb_image RGBA8 디코드 → (옵션) premultiplied alpha → rhi 텍스처.
// 픽셀아트 규약(02): 포인트 샘플·밉맵 없음·premultiplied alpha. sRGB 변환은 하지 않는다
// (stb 8bit 경로 그대로, 색은 직접 저장).
//
// GPU 업로드 순서(rhiTextureNote / Dx11Device.cpp:753-833 확인):
//   1) TextureDesc{Tex2D, RGBA8Unorm, w, h, mips=1, X1, usage=Sampled} 구성
//   2) TextureInitData{data=픽셀, rowPitch=w*4, slicePitch=0}
//   3) device.CreateTexture(desc, &init) → SRV 자동 생성 → gpuTexture
//   4) SamplerDesc{Point, Clamp×3} → device.CreateSampler → sampler
#define STB_IMAGE_IMPLEMENTATION
// stb는 실패해도 예외 대신 nullptr을 돌려주므로 예외 불사용 규약과 정합. setjmp 경로만 사용.
#include "stb_image.h"

#include "mye/asset/Importer.h"
#include "mye/core/Log.h"
#include "mye/rhi/Rhi.h"

#include <array>
#include <climits>
#include <cstring>

namespace mye::asset {

namespace {
constexpr std::array<std::string_view, 1> kPngExts{".png"};
}

std::span<const std::string_view> TextureImporter::SourceExtensions() const {
    return kPngExts;
}

Expected<DecodedImage, Error>
TextureImporter::DecodePng(std::span<const std::byte> pngBytes, bool premultiplyAlpha) {
    if (pngBytes.empty()) {
        return Error{"TextureImporter::DecodePng: empty input", 1};
    }
    if (pngBytes.size() > static_cast<size_t>(INT_MAX)) {
        return Error{"TextureImporter::DecodePng: input too large", 2};
    }

    int w = 0, h = 0, channelsInFile = 0;
    // 항상 RGBA8(4채널)로 강제 디코드. stb는 8비트/채널.
    stbi_uc* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(pngBytes.data()), static_cast<int>(pngBytes.size()), &w,
        &h, &channelsInFile, 4);
    if (!decoded) {
        const char* reason = stbi_failure_reason();
        return Error{std::string("TextureImporter::DecodePng: stb decode failed: ") +
                         (reason ? reason : "unknown"),
                     3};
    }
    if (w <= 0 || h <= 0) {
        stbi_image_free(decoded);
        return Error{"TextureImporter::DecodePng: non-positive dimensions", 4};
    }

    const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    const size_t byteCount = pixelCount * 4;

    DecodedImage img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.pixels.resize(byteCount);

    // premultiply: straight alpha RGBA → premultiplied (R*A/255 등). 정수 반올림은 (x*a + 127)/255.
    if (premultiplyAlpha) {
        for (size_t i = 0; i < pixelCount; ++i) {
            const stbi_uc* src = decoded + i * 4;
            const uint32_t a = src[3];
            auto pm = [a](uint32_t c) -> std::byte {
                return static_cast<std::byte>((c * a + 127u) / 255u);
            };
            std::byte* dst = img.pixels.data() + i * 4;
            dst[0] = pm(src[0]);
            dst[1] = pm(src[1]);
            dst[2] = pm(src[2]);
            dst[3] = static_cast<std::byte>(a);
        }
    } else {
        std::memcpy(img.pixels.data(), decoded, byteCount);
    }

    stbi_image_free(decoded);
    return img;
}

Expected<Texture, Error>
TextureImporter::Upload(rhi::IDevice& device, const DecodedImage& img,
                        const TextureImportSettings& settings) {
    if (img.width == 0 || img.height == 0) {
        return Error{"TextureImporter::Upload: zero-sized image", 1};
    }
    const size_t expected = static_cast<size_t>(img.width) * img.height * 4;
    if (img.pixels.size() != expected) {
        return Error{"TextureImporter::Upload: pixel buffer size mismatch", 2};
    }

    // (1) TextureDesc. 픽셀아트는 srgb=false → RGBA8Unorm. mips=1(밉맵 없음 — DX11은 mips>1일 때
    //     초기 데이터를 거부).
    rhi::TextureDesc desc;
    desc.dim = rhi::TextureDim::Tex2D;
    desc.format = settings.srgb ? rhi::Format::RGBA8UnormSrgb : rhi::Format::RGBA8Unorm;
    desc.width = img.width;
    desc.height = img.height;
    desc.depthOrLayers = 1;
    desc.mips = 1;
    desc.samples = rhi::SampleCount::X1;
    desc.usage = rhi::TextureUsage::Sampled;
    desc.debugName = "TextureImporter";

    // (2) TextureInitData. rowPitch = width*4 (RGBA8), slicePitch=0(2D 허용).
    rhi::TextureInitData init;
    init.data = img.pixels.data();
    init.rowPitch = img.width * 4;
    init.slicePitch = 0;

    // (3) 텍스처 생성(usage Sampled → SRV 자동 생성).
    rhi::TextureHandle tex = device.CreateTexture(desc, &init);
    if (!tex.IsValid()) {
        return Error{"TextureImporter::Upload: CreateTexture returned invalid handle", 3};
    }

    // (4) 샘플러(픽셀아트 = Point/Clamp, 설정에서 파생).
    rhi::SamplerDesc sd;
    sd.filter = settings.filter;
    sd.u = settings.addressMode;
    sd.v = settings.addressMode;
    sd.w = settings.addressMode;
    sd.debugName = "TextureImporter";
    rhi::SamplerHandle sampler = device.CreateSampler(sd);
    if (!sampler.IsValid()) {
        device.Destroy(tex);
        return Error{"TextureImporter::Upload: CreateSampler returned invalid handle", 4};
    }

    Texture t;
    t.gpuTexture = tex;
    t.sampler = sampler;
    t.width = img.width;
    t.height = img.height;
    t.format = desc.format;
    t.settings = settings;
    return t;
}

Expected<void*, Error> TextureImporter::Import(ImportContext& ctx) {
    // 설정: 주입된 것이 있으면 사용, 없으면 픽셀아트 기본값.
    TextureImportSettings settings;
    if (ctx.settings) {
        settings = *static_cast<const TextureImportSettings*>(ctx.settings);
    }

    auto decoded = DecodePng(ctx.sourceBytes, settings.premultiplyAlpha);
    if (!decoded) {
        return decoded.GetError();
    }

    if (!ctx.device) {
        return Error{"TextureImporter::Import: no rhi device injected", 1};
    }

    auto uploaded = Upload(*ctx.device, decoded.Value(), settings);
    if (!uploaded) {
        return uploaded.GetError();
    }

    // 소유권 이전 — AssetManager가 슬롯에 심고, Destroy로 해제한다.
    Texture* obj = new Texture(std::move(uploaded.Value()));
    return static_cast<void*>(obj);
}

void TextureImporter::Destroy(void* assetObject) {
    delete static_cast<Texture*>(assetObject);
}

void TextureImporter::DestroyWithDevice(void* assetObject, rhi::IDevice* device) {
    // GPU 핸들 반납(임포터가 Import에서 device로 생성했으므로 여기서 책임 반납).
    if (auto* tex = static_cast<Texture*>(assetObject); tex && device) {
        if (tex->gpuTexture.IsValid()) device->Destroy(tex->gpuTexture);
        if (tex->sampler.IsValid()) device->Destroy(tex->sampler);
    }
    delete static_cast<Texture*>(assetObject);
}

// ---- 비동기 로더 ------------------------------------------------------------
namespace {
// Parse의 산출물(워커→메인 이송): 디코드된 CPU 이미지 + 업로드 설정.
// ParsedAsset.cpuData가 이 타입을 가리킨다(TextureAsyncLoader만 해석).
struct ParsedTexture {
    DecodedImage          image;
    TextureImportSettings settings;
};
} // namespace

Expected<ParsedAsset, Error>
TextureAsyncLoader::Parse(LoadContext& /*ctx*/, std::span<const std::byte> runtimeData) {
    // 워커 스레드: 순수 CPU 디코드. GPU/디바이스 접근 금지.
    // M2-A: 임포트 설정 주입(.meta)은 후속 — 픽셀아트 기본값 사용.
    TextureImportSettings settings;
    auto decoded = TextureImporter::DecodePng(runtimeData, settings.premultiplyAlpha);
    if (!decoded) return decoded.GetError();

    auto* pt = new ParsedTexture{std::move(decoded.Value()), settings};
    ParsedAsset out;
    out.cpuData = pt;
    return out;
}

Expected<void*, Error>
TextureAsyncLoader::Finalize(LoadContext& /*ctx*/, ParsedAsset& parsed, rhi::IDevice* device) {
    // 메인 스레드: GPU 업로드. device가 nullptr이면 헤드리스 — CPU 스텁 Texture로 커밋.
    auto* pt = static_cast<ParsedTexture*>(parsed.cpuData);
    if (!pt) return Error{"TextureAsyncLoader::Finalize: null parsed data", 1};

    if (!device) {
        // 헤드리스(테스트): GPU 핸들 없이 CPU 메타데이터만 담은 Texture를 커밋한다.
        // 픽셀 정확성은 Parse 단계(DecodedImage)에서 검증되며, Finalize는 파이프라인 배선만.
        auto* obj = new Texture();
        obj->width = pt->image.width;
        obj->height = pt->image.height;
        obj->format = pt->settings.srgb ? rhi::Format::RGBA8UnormSrgb : rhi::Format::RGBA8Unorm;
        obj->settings = pt->settings;
        delete pt;
        parsed.cpuData = nullptr;
        return static_cast<void*>(obj);
    }

    auto uploaded = TextureImporter::Upload(*device, pt->image, pt->settings);
    delete pt;
    parsed.cpuData = nullptr;
    if (!uploaded) return uploaded.GetError();

    auto* obj = new Texture(std::move(uploaded.Value()));
    return static_cast<void*>(obj);
}

void TextureAsyncLoader::DiscardParsed(ParsedAsset& parsed) {
    delete static_cast<ParsedTexture*>(parsed.cpuData);
    parsed.cpuData = nullptr;
}

void TextureAsyncLoader::Unload(void* assetObject, rhi::IDevice* device) {
    if (auto* tex = static_cast<Texture*>(assetObject); tex && device) {
        if (tex->gpuTexture.IsValid()) device->Destroy(tex->gpuTexture);
        if (tex->sampler.IsValid()) device->Destroy(tex->sampler);
    }
    delete static_cast<Texture*>(assetObject);
}

} // namespace mye::asset
