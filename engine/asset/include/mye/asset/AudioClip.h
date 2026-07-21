// mye/asset/AudioClip.h — 오디오 클립 에셋 (docs/04 §임포터 라인업, M3-A)
//
// AudioImporter(wav/ogg)가 PCM으로 디코드한 결과. **재생은 M3-B(06)** — 04는 디코드·데이터만.
//   짧은 SFX(발소리 등)는 메모리 상주 PCM, BGM(ogg)은 스트리밍 플래그로 vorbis 유지.
#pragma once

#include "mye/asset/AssetGuid.h"

#include <cstdint>
#include <vector>

namespace mye::asset {

// PCM 샘플 포맷 — 디코드 결과 표현. 재생기(06)가 소비.
enum class AudioSampleFormat : uint8_t { S16, F32 };

// 오디오 원본 인코딩(스트리밍 판정·재디코드용 메타).
enum class AudioEncoding : uint8_t { PcmWav, Vorbis };

// 런타임 오디오 클립 — 디코드된 PCM(상주) 또는 스트리밍 메타.
struct AudioClip {
    MYE_ASSET_TYPE(mye::asset::AudioClip);

    uint32_t          sampleRate = 44100;   // Hz
    uint16_t          channels = 1;         // 1=mono, 2=stereo
    AudioSampleFormat format = AudioSampleFormat::S16;
    AudioEncoding     encoding = AudioEncoding::PcmWav;
    bool              streaming = false;     // true면 samples 비고 원본 유지(BGM)

    uint64_t          frameCount = 0;        // 채널당 샘플 프레임 수
    // 인터리브드 PCM. 상주(streaming=false)일 때만 채워진다. 크기 = frameCount*channels*bytesPerSample.
    std::vector<std::byte> samples;

    // 스트리밍(ogg BGM)일 때 원본 vorbis 바이트(06이 스트림 디코드). 상주면 비어있음.
    std::vector<std::byte> encodedSource;

    double DurationSeconds() const {
        return sampleRate ? static_cast<double>(frameCount) / sampleRate : 0.0;
    }
};

} // namespace mye::asset
