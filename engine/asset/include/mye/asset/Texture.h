// mye/asset/Texture.h — Texture 에셋 타입 (docs/04 §임포터 라인업, docs/02 픽셀아트 규약)
//
// 04는 GPU를 모르지 않되(같은 L2), Texture 에셋은 rhi TextureHandle을 보유한다.
// TextureImporter가 PNG를 디코드해 픽셀 버퍼를 만들고, 로드 Finalize 단계에서
// rhi::IDevice::CreateTexture로 GPU에 올린 뒤 핸들을 이 구조체에 심는다.
// 픽셀아트 규약(02): 포인트 샘플·밉맵 없음·premultiplied alpha.
#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/rhi/RhiTypes.h"

#include <cstdint>

namespace mye::asset {

// PNG 임포트 설정(.meta settings로 직렬화). 픽셀아트 기본값.
struct TextureImportSettings {
    bool               generateMips = false;                 // 픽셀아트 = false
    bool               premultiplyAlpha = true;              // 02 통일 규약
    bool               srgb = false;                         // 픽셀아트 색은 직접 저장(sRGB 변환 안 함)
    rhi::Filter        filter = rhi::Filter::Point;          // 픽셀아트 = Point
    rhi::AddressMode   addressMode = rhi::AddressMode::Clamp;
};

// 임포터/로더가 CPU에서 만든 디코드 결과(GPU 업로드 전 중간 표현).
struct DecodedImage {
    uint32_t             width = 0;
    uint32_t             height = 0;
    // RGBA8, premultiplied(설정 시). 크기 = width*height*4. 행 피치 = width*4.
    std::vector<std::byte> pixels;
};

// 런타임 Texture 에셋 객체(AssetHandle<Texture>가 가리킴).
struct Texture {
    MYE_ASSET_TYPE(Texture);

    rhi::TextureHandle   gpuTexture;     // rhi 소유(Device::CreateTexture 결과)
    rhi::SamplerHandle   sampler;        // 설정에서 파생(Point/Clamp 등)
    uint32_t             width = 0;
    uint32_t             height = 0;
    rhi::Format          format = rhi::Format::RGBA8Unorm;
    TextureImportSettings settings;
};

} // namespace mye::asset
