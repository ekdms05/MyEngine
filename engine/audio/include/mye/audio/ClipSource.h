// mye/audio/ClipSource.h — 재생 소스(상주 PCM / 스트리밍 ogg) 어댑터 (docs/06 §4, M3-B)
//
// AudioClip(04) 을 믹서가 소비할 수 있는 형태로 감싼다. 두 모드:
//   - 상주(streaming=false): AudioClip.samples(인터리브 S16/F32) 를 비소유 뷰로 참조.
//   - 스트리밍(streaming=true): AudioClip.encodedSource(ogg 바이트)를 stb_vorbis 로 온디맨드 디코드.
// 믹서는 ClipSource::ReadFrames 로 F32 인터리브 프레임을 당겨간다(포맷 통일 → 믹싱 단순화).
#pragma once

#include "mye/asset/AudioClip.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mye::audio {

// 믹서가 소비하는 최소 클립 뷰(비소유). 상주 PCM 전용 경로 — 테스트가 합성 PCM 을 직접 넘길 때 사용.
struct ClipView {
    const std::byte*         samples = nullptr;   // 인터리브 PCM(포맷 = format)
    uint64_t                 frameCount = 0;
    uint16_t                 channels = 1;
    uint32_t                 sampleRate = 44100;
    asset::AudioSampleFormat format = asset::AudioSampleFormat::S16;

    static ClipView From(const asset::AudioClip& clip) {
        return ClipView{
            clip.samples.data(), clip.frameCount, clip.channels,
            clip.sampleRate, clip.format};
    }
    bool IsValid() const { return samples != nullptr && frameCount > 0 && channels > 0; }

    // frame 위치의 채널 ch 샘플을 F32 정규화 [-1,1] 로 반환.
    float SampleF32(uint64_t frame, uint16_t ch) const;
};

// 재생 소스 인터페이스: 커서를 들고 F32 인터리브 프레임을 읽어준다.
// 상주/스트리밍 공통 표면 — 믹서는 구현을 모른다.
class IClipSource {
public:
    virtual ~IClipSource() = default;

    virtual uint16_t Channels() const = 0;
    virtual uint32_t SampleRate() const = 0;
    virtual uint64_t FrameCount() const = 0;   // 스트리밍이라 미정이면 0

    // dst(outChannels 인터리브 F32)에 최대 frames 프레임을 읽어 채운다.
    // 반환 = 실제로 채운 프레임 수. loop=false 에서 끝에 닿으면 반환값 < frames.
    // loop=true 면 끝에서 되감아 항상 frames 를 채운다(소스 길이 0 이면 0).
    // 채널 수 변환(mono↔stereo)은 믹서가 아니라 여기서 수행하지 않는다 — 소스 원본 채널로 낸다.
    virtual uint64_t ReadFrames(float* dst, uint64_t frames, bool loop) = 0;

    virtual void Rewind() = 0;
};

// 상주 PCM 소스(AudioClip.samples 비소유 참조). 소유자(핸들)가 클립 수명을 보장해야 한다.
class ResidentClipSource final : public IClipSource {
public:
    explicit ResidentClipSource(ClipView view) : m_view(view) {}

    uint16_t Channels() const override { return m_view.channels; }
    uint32_t SampleRate() const override { return m_view.sampleRate; }
    uint64_t FrameCount() const override { return m_view.frameCount; }
    uint64_t ReadFrames(float* dst, uint64_t frames, bool loop) override;
    void Rewind() override { m_cursor = 0; }

private:
    ClipView m_view;
    uint64_t m_cursor = 0;   // 프레임 단위
};

// 스트리밍 ogg 소스(AudioClip.encodedSource 를 stb_vorbis 로 온디맨드 디코드).
// 생성 실패(손상 ogg) 시 IsValid()==false — 믹서가 무음 취급.
class StreamingOggSource final : public IClipSource {
public:
    explicit StreamingOggSource(std::span<const std::byte> oggBytes);
    ~StreamingOggSource() override;

    StreamingOggSource(const StreamingOggSource&) = delete;
    StreamingOggSource& operator=(const StreamingOggSource&) = delete;

    bool IsValid() const { return m_handle != nullptr; }

    uint16_t Channels() const override { return m_channels; }
    uint32_t SampleRate() const override { return m_sampleRate; }
    uint64_t FrameCount() const override { return 0; }   // 스트리밍은 미정
    uint64_t ReadFrames(float* dst, uint64_t frames, bool loop) override;
    void Rewind() override;

private:
    void* m_handle = nullptr;    // stb_vorbis* (헤더 오염 회피 위해 void*)
    std::vector<std::byte> m_bytes;
    uint16_t m_channels = 0;
    uint32_t m_sampleRate = 0;
};

// 팩토리: AudioClip 을 보고 상주/스트리밍 소스를 만든다. 실패 시 nullptr.
std::unique_ptr<IClipSource> MakeClipSource(const asset::AudioClip& clip);

} // namespace mye::audio
