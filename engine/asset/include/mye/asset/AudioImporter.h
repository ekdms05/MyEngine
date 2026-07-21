// mye/asset/AudioImporter.h — WAV/OGG → AudioClip 임포터 (docs/04 §임포터 라인업, M3-A)
//
// M3-A 범위: 인터페이스 동결 + PCM 디코드 골격. **재생은 M3-B(06)**. WAV 는 자체 RIFF/PCM
//   파서(무의존), OGG 는 stb_vorbis 를 이번에 벤더링(third_party). 순수 디코드 헬퍼를 노출해
//   aseprite.exe 처럼 외부 도구 없이 단위 테스트한다(합성 WAV 바이트 직접 생성).
#pragma once

#include "mye/asset/AudioClip.h"
#include "mye/asset/Importer.h"
#include "mye/core/Base.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace mye::asset {

// 오디오 임포트 설정(.meta settings). 07 인스펙터 자동 생성(후속).
struct AudioImportSettings {
    bool forceMono = false;         // 스테레오→모노 다운믹스(SFX 3D 배치용)
    bool streaming = false;         // true면 상주 PCM 대신 원본 유지(BGM). ogg 기본 true 권장.
    AudioSampleFormat targetFormat = AudioSampleFormat::S16;   // 상주 PCM 포맷
};

class AudioImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "AudioImporter"; }
    std::span<const std::string_view> SourceExtensions() const override;   // {".wav", ".ogg"}
    uint32_t Version() const override { return 1; }
    AssetTypeId ProducedType() const override { return AudioClip::kAssetTypeId; }

    Expected<void*, Error> Import(ImportContext& ctx) override;
    void Destroy(void* assetObject) override;

    // ---- 순수 디코드 헬퍼(외부 도구 없이 단위 테스트) ----
    // RIFF/WAVE PCM(8/16/24/32bit, int/float) → AudioClip(target 포맷으로 변환).
    static Expected<AudioClip, Error> DecodeWav(std::span<const std::byte> wavBytes,
                                                const AudioImportSettings& settings);
    // Ogg Vorbis → AudioClip. streaming 이면 encodedSource 만 채우고 PCM 은 비운다.
    static Expected<AudioClip, Error> DecodeOgg(std::span<const std::byte> oggBytes,
                                                const AudioImportSettings& settings);
};

} // namespace mye::asset
