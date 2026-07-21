// mye/asset/Importer.h — 임포터 프레임워크 + TextureImporter (docs/04 §임포터)
//
// M0 범위: 소스(PNG) → 런타임 Texture 에셋을 만드는 동기 임포트 경로.
// (M1의 임포트 캐시·캐시 키·AssetDatabase 전체 스캔은 후속.) 확장 포인트로
// 플러그인이 IAssetImporter를 등록할 수 있게 인터페이스를 동결한다.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/asset/FileSystem.h"
#include "mye/asset/Texture.h"
#include "mye/core/Base.h"

#include <span>
#include <string>
#include <string_view>

namespace mye::rhi { class IDevice; }

namespace mye::asset {

// 임포트 시점 컨텍스트 — 소스 접근·GPU 업로드 경로 제공.
struct ImportContext {
    std::string_view sourcePath;         // VFS vpath 또는 OS 경로
    Blob             sourceBytes;        // 이미 읽어온 소스 바이트(임포터는 디코드만)
    rhi::IDevice*    device = nullptr;   // Finalize에서 GPU 업로드에 사용(02 역주입)
    const void*      settings = nullptr; // 타입별 임포트 설정 인스턴스(예: TextureImportSettings)
};

// 확장 포인트: 플러그인이 커스텀 임포터를 AssetManager::RegisterImporter로 등록.
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;
    virtual const char* Name() const = 0;
    virtual std::span<const std::string_view> SourceExtensions() const = 0;  // {".png"}
    virtual uint32_t Version() const = 0;                // 올리면 전체 리임포트(캐시 키, M1)
    virtual AssetTypeId ProducedType() const = 0;        // 이 임포터가 만드는 에셋 타입

    // 소스 바이트 → 런타임 에셋 객체. 성공 시 out에 new된 객체 포인터(소유권 이전).
    // TextureImporter는 여기서 PNG 디코드 + rhi 텍스처 생성까지 수행한다.
    virtual Expected<void*, Error> Import(ImportContext& ctx) = 0;
    virtual void Destroy(void* assetObject) = 0;         // Import가 만든 객체 해제
};

// PNG 임포터(third_party/stb 소비). 픽셀아트 규약(02)에 맞춰
// 포인트 샘플·밉맵 없음·premultiplied alpha로 rhi Texture를 만든다.
class TextureImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "TextureImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;
    uint32_t Version() const override { return 1; }
    AssetTypeId ProducedType() const override { return Texture::kAssetTypeId; }

    Expected<void*, Error> Import(ImportContext& ctx) override;
    void Destroy(void* assetObject) override;

    // 순수 PNG→CPU 픽셀 디코드(GPU 없이 단위 테스트 가능). stb_image 사용.
    // premultiply 설정 시 알파 프리멀티플까지 적용한 RGBA8을 돌려준다.
    static Expected<DecodedImage, Error> DecodePng(std::span<const std::byte> pngBytes,
                                                   bool premultiplyAlpha);

    // 디코드된 CPU 이미지 + 설정 → rhi GPU 텍스처/샘플러 생성 → Texture 채움.
    // (rhiTextureNote의 호출 순서를 이 함수가 구현한다.) 구현: src/TextureImporter.cpp
    static Expected<Texture, Error> Upload(rhi::IDevice& device, const DecodedImage& img,
                                           const TextureImportSettings& settings);
};

} // namespace mye::asset
