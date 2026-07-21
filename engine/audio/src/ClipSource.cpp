// ClipSource.cpp — 상주/스트리밍 재생 소스 구현 (docs/06 §4, M3-B)
#include "mye/audio/ClipSource.h"

#include <cstring>

// 스트리밍 ogg 온디맨드 디코드: 04 와 같은 벤더 stb_vorbis 를 헤더 온리로 참조(구현 TU 는 asset 소유).
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

namespace mye::audio {

// ---- ClipView ----
float ClipView::SampleF32(uint64_t frame, uint16_t ch) const {
    if (frame >= frameCount || ch >= channels) return 0.0f;
    const uint64_t idx = frame * channels + ch;
    if (format == asset::AudioSampleFormat::F32) {
        const auto* p = reinterpret_cast<const float*>(samples);
        return p[idx];
    }
    // S16
    const auto* p = reinterpret_cast<const int16_t*>(samples);
    return static_cast<float>(p[idx]) / 32768.0f;
}

// ---- ResidentClipSource ----
uint64_t ResidentClipSource::ReadFrames(float* dst, uint64_t frames, bool loop) {
    if (!m_view.IsValid()) return 0;
    const uint16_t ch = m_view.channels;
    uint64_t written = 0;
    while (written < frames) {
        if (m_cursor >= m_view.frameCount) {
            if (!loop) break;
            m_cursor = 0;
            if (m_view.frameCount == 0) break;
        }
        for (uint16_t c = 0; c < ch; ++c)
            dst[written * ch + c] = m_view.SampleF32(m_cursor, c);
        ++m_cursor;
        ++written;
    }
    return written;
}

// ---- StreamingOggSource ----
StreamingOggSource::StreamingOggSource(std::span<const std::byte> oggBytes)
    : m_bytes(oggBytes.begin(), oggBytes.end()) {
    if (m_bytes.empty()) return;
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(
        reinterpret_cast<const unsigned char*>(m_bytes.data()),
        static_cast<int>(m_bytes.size()), &err, nullptr);
    if (!v) return;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    m_channels = static_cast<uint16_t>(info.channels);
    m_sampleRate = info.sample_rate;
    m_handle = v;
}

StreamingOggSource::~StreamingOggSource() {
    if (m_handle) stb_vorbis_close(static_cast<stb_vorbis*>(m_handle));
}

void StreamingOggSource::Rewind() {
    if (m_handle) stb_vorbis_seek_start(static_cast<stb_vorbis*>(m_handle));
}

uint64_t StreamingOggSource::ReadFrames(float* dst, uint64_t frames, bool loop) {
    if (!m_handle || m_channels == 0) return 0;
    auto* v = static_cast<stb_vorbis*>(m_handle);
    uint64_t written = 0;
    while (written < frames) {
        // stb_vorbis 는 int 프레임 단위. 남은 요청만큼 인터리브 F32 로 채운다.
        const int want = static_cast<int>(frames - written);
        const int got = stb_vorbis_get_samples_float_interleaved(
            v, m_channels, dst + written * m_channels, want * m_channels);
        if (got <= 0) {
            if (!loop) break;
            stb_vorbis_seek_start(v);
            // 되감은 뒤에도 못 읽으면(빈 스트림) 무한 루프 방지.
            const int got2 = stb_vorbis_get_samples_float_interleaved(
                v, m_channels, dst + written * m_channels, want * m_channels);
            if (got2 <= 0) break;
            written += static_cast<uint64_t>(got2);
            continue;
        }
        written += static_cast<uint64_t>(got);
    }
    return written;
}

// ---- 팩토리 ----
std::unique_ptr<IClipSource> MakeClipSource(const asset::AudioClip& clip) {
    if (clip.streaming) {
        auto src = std::make_unique<StreamingOggSource>(
            std::span<const std::byte>(clip.encodedSource));
        if (!src->IsValid()) return nullptr;
        return src;
    }
    ClipView view = ClipView::From(clip);
    if (!view.IsValid()) return nullptr;
    return std::make_unique<ResidentClipSource>(view);
}

} // namespace mye::audio
