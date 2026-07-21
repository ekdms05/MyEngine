// AudioImporter.cpp — WAV/OGG → AudioClip 임포터 (docs/04 §임포터 라인업, M3-A)
//
// M3-A 범위: PCM 디코드만(재생은 M3-B/06). WAV 는 자체 RIFF/PCM 파서(무의존)로 8/16/24/32bit
//   int 및 32bit float 를 지원한다. OGG 는 벤더링한 stb_vorbis 로 S16 인터리브드로 디코드한다.
//   순수 디코드 헬퍼(DecodeWav/DecodeOgg)는 외부 도구 없이 단위 테스트 가능하도록 정적 노출한다.
//
// 포맷 변환 규약:
//   - 모든 소스 샘플을 우선 f32(정규화 [-1,1])로 승격한 뒤, settings.targetFormat(S16/F32)으로
//     최종 변환한다(단일 경로 → 코너케이스 최소화). forceMono 면 채널 평균으로 다운믹스한다.
//   - streaming=true(주로 OGG BGM)면 PCM 은 비우고 encodedSource 에 원본 바이트만 담는다(06 스트림 디코드).
#include "mye/asset/AudioImporter.h"

#include "mye/core/Log.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

// 벤더 stb_vorbis: 파일 I/O·푸시 API 는 impl TU 에서 꺼두었다. 여기선 선언만 필요.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

namespace mye::asset {

namespace {

// ---- 리틀엔디언 정수 리더(RIFF 는 LE) ----
uint16_t ReadU16LE(const std::byte* p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[0]) |
                                 (static_cast<uint16_t>(static_cast<uint8_t>(p[1])) << 8));
}
uint32_t ReadU32LE(const std::byte* p) {
    return static_cast<uint32_t>(static_cast<uint8_t>(p[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(p[3])) << 24);
}

bool FourCC(const std::byte* p, const char (&tag)[5]) {
    return static_cast<char>(p[0]) == tag[0] && static_cast<char>(p[1]) == tag[1] &&
           static_cast<char>(p[2]) == tag[2] && static_cast<char>(p[3]) == tag[3];
}

// WAVE fmt 태그.
constexpr uint16_t kWaveFormatPcm        = 0x0001;
constexpr uint16_t kWaveFormatFloat      = 0x0003;
constexpr uint16_t kWaveFormatExtensible = 0xFFFE;

// f32 정규화 샘플 → 최종 클립(포맷 변환 + 모노 다운믹스 + 상주 PCM 채움).
// interleavedF32: srcChannels 인터리브드, 크기 = frames*srcChannels.
AudioClip FinalizeClip(std::vector<float>& interleavedF32, uint32_t sampleRate,
                       uint16_t srcChannels, const AudioImportSettings& settings) {
    AudioClip clip;
    clip.sampleRate = sampleRate;
    clip.encoding = AudioEncoding::PcmWav;
    clip.streaming = false;

    const uint64_t frames = srcChannels ? (interleavedF32.size() / srcChannels) : 0;
    const uint16_t outChannels = (settings.forceMono || srcChannels == 1) ? 1 : srcChannels;
    clip.channels = outChannels;
    clip.frameCount = frames;
    clip.format = settings.targetFormat;

    // 채널 축소(다운믹스) 뷰를 만든다: outChannels==1 이고 src>1 이면 채널 평균.
    auto sampleAt = [&](uint64_t frame, uint16_t outCh) -> float {
        if (outChannels == srcChannels) {
            return interleavedF32[frame * srcChannels + outCh];
        }
        // 모노 다운믹스: 소스 전 채널 평균.
        float sum = 0.0f;
        for (uint16_t c = 0; c < srcChannels; ++c) sum += interleavedF32[frame * srcChannels + c];
        return sum / static_cast<float>(srcChannels);
    };

    const uint64_t outSamples = frames * outChannels;
    if (settings.targetFormat == AudioSampleFormat::F32) {
        clip.samples.resize(outSamples * sizeof(float));
        auto* dst = reinterpret_cast<float*>(clip.samples.data());
        for (uint64_t f = 0; f < frames; ++f)
            for (uint16_t c = 0; c < outChannels; ++c) *dst++ = sampleAt(f, c);
    } else {  // S16
        clip.samples.resize(outSamples * sizeof(int16_t));
        auto* dst = reinterpret_cast<int16_t*>(clip.samples.data());
        for (uint64_t f = 0; f < frames; ++f) {
            for (uint16_t c = 0; c < outChannels; ++c) {
                float v = sampleAt(f, c);
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                // 대칭 스케일: [-1,1] → [-32767,32767]. 반올림.
                *dst++ = static_cast<int16_t>(std::lround(v * 32767.0f));
            }
        }
    }
    return clip;
}

}  // namespace

std::span<const std::string_view> AudioImporter::SourceExtensions() const {
    static const std::array<std::string_view, 2> kExt{".wav", ".ogg"};
    return kExt;
}

// ---- WAV(RIFF/WAVE) 디코드: 8/16/24/32bit int, 32bit float ----
Expected<AudioClip, Error> AudioImporter::DecodeWav(std::span<const std::byte> wav,
                                                    const AudioImportSettings& settings) {
    const std::byte* p = wav.data();
    const size_t n = wav.size();
    if (n < 44) return Error{"DecodeWav: too small for RIFF/WAVE header", -1};
    if (!FourCC(p, "RIFF")) return Error{"DecodeWav: missing RIFF magic", -1};
    if (!FourCC(p + 8, "WAVE")) return Error{"DecodeWav: not a WAVE file", -1};

    uint16_t audioFormat = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const std::byte* dataPtr = nullptr;
    size_t dataLen = 0;
    bool haveFmt = false;

    // 청크 순회(RIFF chunk: 4B id + 4B size + payload, 홀수 크기는 1B 패딩).
    size_t off = 12;
    while (off + 8 <= n) {
        const std::byte* chunk = p + off;
        uint32_t chunkSize = ReadU32LE(chunk + 4);
        const std::byte* payload = chunk + 8;
        if (payload + chunkSize > p + n) chunkSize = static_cast<uint32_t>((p + n) - payload);

        if (FourCC(chunk, "fmt ")) {
            if (chunkSize < 16) return Error{"DecodeWav: fmt chunk too small", -1};
            audioFormat   = ReadU16LE(payload + 0);
            channels      = ReadU16LE(payload + 2);
            sampleRate    = ReadU32LE(payload + 4);
            bitsPerSample = ReadU16LE(payload + 14);
            // WAVE_FORMAT_EXTENSIBLE: 실제 포맷은 SubFormat GUID 의 첫 2바이트.
            if (audioFormat == kWaveFormatExtensible && chunkSize >= 26) {
                audioFormat = ReadU16LE(payload + 24);
            }
            haveFmt = true;
        } else if (FourCC(chunk, "data")) {
            dataPtr = payload;
            dataLen = chunkSize;
        }
        off += 8 + chunkSize + (chunkSize & 1);  // 홀수 패딩
    }

    if (!haveFmt) return Error{"DecodeWav: no fmt chunk", -1};
    if (!dataPtr) return Error{"DecodeWav: no data chunk", -1};
    if (channels == 0) return Error{"DecodeWav: zero channels", -1};
    if (sampleRate == 0) return Error{"DecodeWav: zero sample rate", -1};

    const bool isFloat = (audioFormat == kWaveFormatFloat);
    const bool isPcm = (audioFormat == kWaveFormatPcm);
    if (!isFloat && !isPcm)
        return Error{"DecodeWav: unsupported audio format (only PCM/float)", -1};

    const uint32_t bytesPerSample = bitsPerSample / 8u;
    if (bytesPerSample == 0) return Error{"DecodeWav: zero bits per sample", -1};
    if (isFloat && bitsPerSample != 32)
        return Error{"DecodeWav: float WAV must be 32-bit", -1};
    if (isPcm && bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32)
        return Error{"DecodeWav: unsupported PCM bit depth (8/16/24/32)", -1};

    const uint32_t frameStride = bytesPerSample * channels;
    const uint64_t frames = frameStride ? (dataLen / frameStride) : 0;
    const uint64_t totalSamples = frames * channels;

    // 소스 샘플 → f32 정규화 [-1,1].
    std::vector<float> f32(totalSamples);
    const std::byte* s = dataPtr;
    for (uint64_t i = 0; i < totalSamples; ++i, s += bytesPerSample) {
        float v = 0.0f;
        if (isFloat) {  // 32bit float LE
            uint32_t bits = ReadU32LE(s);
            std::memcpy(&v, &bits, sizeof(float));
        } else if (bitsPerSample == 8) {  // 8bit PCM 은 부호없음(0..255, 128 중앙).
            int32_t u = static_cast<int32_t>(static_cast<uint8_t>(s[0]));
            v = (static_cast<float>(u) - 128.0f) / 128.0f;
        } else if (bitsPerSample == 16) {
            int16_t x = static_cast<int16_t>(ReadU16LE(s));
            v = static_cast<float>(x) / 32768.0f;
        } else if (bitsPerSample == 24) {  // 부호있는 24bit LE → 부호확장.
            int32_t x = static_cast<int32_t>(static_cast<uint8_t>(s[0])) |
                        (static_cast<int32_t>(static_cast<uint8_t>(s[1])) << 8) |
                        (static_cast<int32_t>(static_cast<uint8_t>(s[2])) << 16);
            if (x & 0x00800000) x |= static_cast<int32_t>(0xFF000000);
            v = static_cast<float>(x) / 8388608.0f;
        } else {  // 32bit int PCM
            int32_t x = static_cast<int32_t>(ReadU32LE(s));
            v = static_cast<float>(x) / 2147483648.0f;
        }
        f32[i] = v;
    }

    return FinalizeClip(f32, sampleRate, channels, settings);
}

// ---- OGG Vorbis 디코드: stb_vorbis(메모리) → S16 → f32 → 최종 포맷 ----
Expected<AudioClip, Error> AudioImporter::DecodeOgg(std::span<const std::byte> ogg,
                                                    const AudioImportSettings& settings) {
    if (ogg.empty()) return Error{"DecodeOgg: empty input", -1};

    // 스트리밍(BGM): PCM 은 비우고 원본 vorbis 바이트만 보관(06 이 스트림 디코드).
    if (settings.streaming) {
        // 헤더 유효성만 확인(정적 열기 후 즉시 닫음) — 손상 파일 조기 검출.
        int chOut = 0, srOut = 0, err = 0;
        stb_vorbis* v = stb_vorbis_open_memory(
            reinterpret_cast<const unsigned char*>(ogg.data()),
            static_cast<int>(ogg.size()), &err, nullptr);
        if (!v) return Error{"DecodeOgg: stb_vorbis_open_memory failed (streaming probe)", err};
        stb_vorbis_info info = stb_vorbis_get_info(v);
        chOut = info.channels;
        srOut = static_cast<int>(info.sample_rate);
        stb_vorbis_close(v);

        AudioClip clip;
        clip.encoding = AudioEncoding::Vorbis;
        clip.streaming = true;
        clip.sampleRate = static_cast<uint32_t>(srOut);
        clip.channels = (settings.forceMono || chOut == 1) ? 1 : static_cast<uint16_t>(chOut);
        clip.format = settings.targetFormat;
        clip.frameCount = 0;  // 스트리밍은 미리 알 수 없음(06 이 산정) — 0 으로 둔다.
        clip.encodedSource.assign(ogg.begin(), ogg.end());
        return clip;
    }

    // 상주(SFX): 전체를 S16 인터리브드로 원샷 디코드.
    int channels = 0, sampleRate = 0;
    short* pcm = nullptr;
    int frames = stb_vorbis_decode_memory(
        reinterpret_cast<const unsigned char*>(ogg.data()),
        static_cast<int>(ogg.size()), &channels, &sampleRate, &pcm);
    if (frames < 0 || !pcm) {
        if (pcm) free(pcm);
        return Error{"DecodeOgg: stb_vorbis_decode_memory failed", -1};
    }
    if (channels <= 0 || sampleRate <= 0) {
        free(pcm);
        return Error{"DecodeOgg: invalid channels/sampleRate", -1};
    }

    const uint16_t srcChannels = static_cast<uint16_t>(channels);
    const uint64_t totalSamples = static_cast<uint64_t>(frames) * srcChannels;
    std::vector<float> f32(totalSamples);
    for (uint64_t i = 0; i < totalSamples; ++i) f32[i] = pcm[i] / 32768.0f;
    free(pcm);  // stb_vorbis_decode_memory 는 malloc 반환 — free 로 해제.

    return FinalizeClip(f32, static_cast<uint32_t>(sampleRate), srcChannels, settings);
}

Expected<void*, Error> AudioImporter::Import(ImportContext& ctx) {
    // 확장자 판정.
    std::string_view path = ctx.sourcePath;
    auto endsWith = [&](std::string_view suffix) {
        if (path.size() < suffix.size()) return false;
        std::string tail(path.substr(path.size() - suffix.size()));
        for (char& c : tail) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return tail == suffix;
    };

    AudioImportSettings settings;
    if (ctx.settings) settings = *static_cast<const AudioImportSettings*>(ctx.settings);

    std::span<const std::byte> bytes(ctx.sourceBytes);
    Expected<AudioClip, Error> decoded = Error{"AudioImporter: unknown extension", -1};
    if (endsWith(".wav")) {
        decoded = DecodeWav(bytes, settings);
    } else if (endsWith(".ogg")) {
        // OGG 기본 권장: 스트리밍(BGM). 명시 설정이 없으면 상주 SFX 로 둔다(설정 UI는 후속).
        decoded = DecodeOgg(bytes, settings);
    } else {
        return Error{"AudioImporter: unsupported extension for '" + std::string(path) + "'", -1};
    }

    if (!decoded) {
        MYE_LOG_ERROR("Asset", "AudioImporter: decode failed '{}': {}", path,
                      decoded.GetError().message);
        return decoded.GetError();
    }

    auto* clip = new AudioClip(std::move(decoded.Value()));
    MYE_LOG_INFO("Asset", "AudioImporter: '{}' {}ch {}Hz {} frames ({:.3f}s)", path,
                 clip->channels, clip->sampleRate, clip->frameCount, clip->DurationSeconds());
    return static_cast<void*>(clip);
}

void AudioImporter::Destroy(void* obj) { delete static_cast<AudioClip*>(obj); }

}  // namespace mye::asset
