// AudioPlaybackTests.cpp — mye_audio M3-B: 믹서·버스·큐·크로스페이드·공간화 (헤드리스 수치 검증)
//
// 실제 사운드 장치 없이(무음 모드) SoftwareMixer/AudioEngine 에 보이스를 큐잉하고 RenderInto/
//   RenderOffline 로 오프라인 렌더해 출력 버퍼를 수치 검증한다:
//   - 큐한 상주 PCM 샘플이 출력에 나타나는지
//   - 버스/마스터 볼륨·뮤트가 곱해지는지, 정지가 즉시 무음화하는지
//   - 패닝이 L/R 을 분리하는지, 공간화 감쇠가 거리에 따라 단조 감소하는지
//   - BGM 크로스페이드 램프가 in 단조 증가·out 단조 감소인지
//   - 장치 초기화 실패(백엔드 없음)에도 크래시 없이 우아 처리
#include "TestFramework.h"

#include "mye/asset/AudioClip.h"
#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioEngine.h"
#include "mye/audio/ClipSource.h"
#include "mye/audio/SoftwareMixer.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace mye;
using namespace mye::audio;

namespace {

// 상수 진폭 모노 S16 클립(디버그 검증이 쉽도록 DC 값). frames 프레임, 값 = amp.
asset::AudioClip MakeConstS16(uint32_t frames, int16_t amp, uint16_t channels = 1,
                              uint32_t sampleRate = 44100) {
    asset::AudioClip c;
    c.sampleRate = sampleRate;
    c.channels = channels;
    c.format = asset::AudioSampleFormat::S16;
    c.streaming = false;
    c.frameCount = frames;
    c.samples.resize(static_cast<size_t>(frames) * channels * sizeof(int16_t));
    auto* p = reinterpret_cast<int16_t*>(c.samples.data());
    for (size_t i = 0; i < static_cast<size_t>(frames) * channels; ++i) p[i] = amp;
    return c;
}

// 스테레오 상수 클립(L=lAmp, R=rAmp).
asset::AudioClip MakeConstStereoS16(uint32_t frames, int16_t lAmp, int16_t rAmp,
                                    uint32_t sampleRate = 44100) {
    asset::AudioClip c;
    c.sampleRate = sampleRate;
    c.channels = 2;
    c.format = asset::AudioSampleFormat::S16;
    c.streaming = false;
    c.frameCount = frames;
    c.samples.resize(static_cast<size_t>(frames) * 2 * sizeof(int16_t));
    auto* p = reinterpret_cast<int16_t*>(c.samples.data());
    for (uint32_t f = 0; f < frames; ++f) { p[f * 2 + 0] = lAmp; p[f * 2 + 1] = rAmp; }
    return c;
}

float PeakAbs(const std::vector<float>& buf) {
    float m = 0.0f;
    for (float x : buf) m = std::max(m, std::abs(x));
    return m;
}

}  // namespace

// ===========================================================================
// 상주 소스: 큐한 샘플이 출력에 나타난다(step=1 정확 재생)
// ===========================================================================
MYE_TEST(MixerResidentClipAppearsInOutput) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(64, 16384);   // 0.5 진폭
    auto src = MakeClipSource(clip);
    MYE_EXPECT(src != nullptr);

    PlayParams pp; pp.bus = BusId::SFX; pp.volume = 1.0f;
    VoiceHandle h = mixer.Play(std::move(src), pp);
    MYE_EXPECT(h.IsValid());
    MYE_EXPECT(mixer.IsVoiceActive(h));

    std::vector<float> out(32 * 2, 0.0f);
    mixer.RenderInto(out.data(), 32);

    // 0.5 진폭이 L/R 양쪽에 나타나야 한다(중앙 팬, 게인 1).
    const float expected = 16384.0f / 32768.0f;
    for (uint32_t f = 0; f < 32; ++f) {
        MYE_EXPECT_NEAR(out[f * 2 + 0], expected, 1e-3f);
        MYE_EXPECT_NEAR(out[f * 2 + 1], expected, 1e-3f);
    }
}

// ===========================================================================
// 소스 소진 → 자동 정지, 이후 무음
// ===========================================================================
MYE_TEST(MixerVoiceEndsAndGoesSilent) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(16, 20000);
    VoiceHandle h = mixer.Play(MakeClipSource(clip), PlayParams{});
    MYE_EXPECT(h.IsValid());

    // 32 프레임 렌더(클립은 16 프레임) → 소진 후 비활성.
    std::vector<float> out(32 * 2, 0.0f);
    mixer.RenderInto(out.data(), 32);
    MYE_EXPECT(!mixer.IsVoiceActive(h));

    // 다음 블록은 완전 무음.
    std::vector<float> out2(16 * 2, 1.0f);
    mixer.RenderInto(out2.data(), 16);
    MYE_EXPECT(PeakAbs(out2) == 0.0f);
}

// ===========================================================================
// 버스 볼륨·마스터 볼륨·뮤트 적용
// ===========================================================================
MYE_TEST(MixerBusAndMasterVolume) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(32, 32767);   // ~1.0 진폭

    // SFX 버스 0.5, 마스터 0.5 → 총 0.25.
    mixer.SetBusVolume(BusId::SFX, 0.5f);
    mixer.SetBusVolume(BusId::Master, 0.5f);
    PlayParams pp; pp.bus = BusId::SFX;
    mixer.Play(MakeClipSource(clip), pp);

    std::vector<float> out(16 * 2, 0.0f);
    mixer.RenderInto(out.data(), 16);
    const float expected = (32767.0f / 32768.0f) * 0.5f * 0.5f;
    MYE_EXPECT_NEAR(out[0], expected, 1e-3f);
    MYE_EXPECT_NEAR(out[1], expected, 1e-3f);

    // 뮤트 → 무음.
    SoftwareMixer m2(44100);
    m2.SetBusMute(BusId::SFX, true);
    m2.Play(MakeClipSource(clip), pp);
    std::vector<float> out2(16 * 2, 0.0f);
    m2.RenderInto(out2.data(), 16);
    MYE_EXPECT(PeakAbs(out2) == 0.0f);
}

// ===========================================================================
// 정지 → 즉시 무음
// ===========================================================================
MYE_TEST(MixerStopSilences) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(128, 30000);
    VoiceHandle h = mixer.Play(MakeClipSource(clip), PlayParams{});
    mixer.Stop(h);
    MYE_EXPECT(!mixer.IsVoiceActive(h));

    std::vector<float> out(16 * 2, 0.0f);
    mixer.RenderInto(out.data(), 16);
    MYE_EXPECT(PeakAbs(out) == 0.0f);
}

// ===========================================================================
// 패닝: pan=-1 → 좌만, pan=+1 → 우만
// ===========================================================================
MYE_TEST(MixerPanning) {
    auto clip = MakeConstS16(16, 20000);

    {
        SoftwareMixer mixer(44100);
        PlayParams pp; pp.pan = -1.0f;
        mixer.Play(MakeClipSource(clip), pp);
        std::vector<float> out(8 * 2, 0.0f);
        mixer.RenderInto(out.data(), 8);
        MYE_EXPECT(out[0] > 0.1f);              // L 존재
        MYE_EXPECT_NEAR(out[1], 0.0f, 1e-4f);   // R 무음
    }
    {
        SoftwareMixer mixer(44100);
        PlayParams pp; pp.pan = 1.0f;
        mixer.Play(MakeClipSource(clip), pp);
        std::vector<float> out(8 * 2, 0.0f);
        mixer.RenderInto(out.data(), 8);
        MYE_EXPECT_NEAR(out[0], 0.0f, 1e-4f);   // L 무음
        MYE_EXPECT(out[1] > 0.1f);              // R 존재
    }
}

// ===========================================================================
// 스테레오 클립 채널 보존
// ===========================================================================
MYE_TEST(MixerStereoClipChannels) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstStereoS16(16, 10000, -10000);
    mixer.Play(MakeClipSource(clip), PlayParams{});
    std::vector<float> out(8 * 2, 0.0f);
    mixer.RenderInto(out.data(), 8);
    MYE_EXPECT_NEAR(out[0], 10000.0f / 32768.0f, 1e-3f);
    MYE_EXPECT_NEAR(out[1], -10000.0f / 32768.0f, 1e-3f);
}

// ===========================================================================
// 공간화: 거리 증가 → 게인 단조 감소, minDistance 안쪽은 최대
// ===========================================================================
MYE_TEST(MixerSpatialAttenuationMonotonic) {
    auto clip = MakeConstS16(16, 30000);

    auto renderAtDistance = [&](float dist) -> float {
        SoftwareMixer mixer(44100);
        mixer.SetListener(Vec2{0, 0});
        PlayParams pp;
        SpatialParams sp;
        sp.worldPos = Vec2{dist, 0};
        sp.minDistance = 1.0f;
        sp.maxDistance = 11.0f;
        sp.minGain = 0.0f;
        sp.panScale = 1000.0f;   // 팬 영향 최소화(진폭만 보게)
        mixer.PlaySpatial(MakeClipSource(clip), pp, sp);
        std::vector<float> out(8 * 2, 0.0f);
        mixer.RenderInto(out.data(), 8);
        return std::abs(out[0]);   // L 채널 진폭
    };

    const float g0 = renderAtDistance(0.5f);   // minDistance 안 → 최대
    const float g1 = renderAtDistance(3.0f);
    const float g2 = renderAtDistance(6.0f);
    const float g3 = renderAtDistance(11.0f);  // maxDistance → minGain(0)

    MYE_EXPECT(g0 > g1);
    MYE_EXPECT(g1 > g2);
    MYE_EXPECT(g2 > g3);
    MYE_EXPECT_NEAR(g3, 0.0f, 1e-4f);
}

// ===========================================================================
// 공간화 패닝: 리스너 우측 소스 → 우 채널 우세
// ===========================================================================
MYE_TEST(MixerSpatialPanning) {
    SoftwareMixer mixer(44100);
    mixer.SetListener(Vec2{0, 0});
    auto clip = MakeConstS16(16, 20000);
    PlayParams pp;
    SpatialParams sp;
    sp.worldPos = Vec2{5, 0};    // 리스너 우측
    sp.minDistance = 100.0f;     // 감쇠 없음(거리<min)
    sp.panScale = 5.0f;          // dx=5 → pan +1
    mixer.PlaySpatial(MakeClipSource(clip), pp, sp);
    std::vector<float> out(8 * 2, 0.0f);
    mixer.RenderInto(out.data(), 8);
    MYE_EXPECT(out[1] > out[0]);           // 우 > 좌
    MYE_EXPECT_NEAR(out[0], 0.0f, 1e-4f);  // 좌 무음(pan=+1)
}

// ===========================================================================
// 크로스페이드 램프: fade-in 단조 증가
// ===========================================================================
MYE_TEST(MixerFadeInMonotonic) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(4096, 30000);
    VoiceHandle h = mixer.Play(MakeClipSource(clip), PlayParams{});
    // 즉시 0 → 2048 프레임에 걸쳐 1.0 램프.
    mixer.SetVoiceFade(h, 0.0f, 0);
    mixer.SetVoiceFade(h, 1.0f, 2048);

    std::vector<float> out(2048 * 2, 0.0f);
    mixer.RenderInto(out.data(), 2048);

    // L 채널 진폭이 프레임에 따라 단조 증가(램프)해야 한다.
    float prev = -1.0f;
    bool monotonic = true;
    for (uint32_t f = 0; f < 2048; f += 64) {
        const float v = std::abs(out[f * 2]);
        if (v < prev - 1e-4f) { monotonic = false; break; }
        prev = v;
    }
    MYE_EXPECT(monotonic);
    MYE_EXPECT(std::abs(out[0]) < std::abs(out[2047 * 2]));  // 시작 < 끝
}

// ===========================================================================
// 크로스페이드 램프: fade-out 단조 감소 + 완료 시 자동 정지
// ===========================================================================
MYE_TEST(MixerFadeOutMonotonicAndStops) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(8192, 30000);
    VoiceHandle h = mixer.Play(MakeClipSource(clip), PlayParams{});
    mixer.FadeOutAndStop(h, 2048);

    std::vector<float> out(2048 * 2, 0.0f);
    mixer.RenderInto(out.data(), 2048);

    float prev = 2.0f;
    bool monotonic = true;
    for (uint32_t f = 0; f < 2048; f += 64) {
        const float v = std::abs(out[f * 2]);
        if (v > prev + 1e-4f) { monotonic = false; break; }
        prev = v;
    }
    MYE_EXPECT(monotonic);
    MYE_EXPECT(std::abs(out[2047 * 2]) < std::abs(out[0]));  // 끝 < 시작
    MYE_EXPECT(!mixer.IsVoiceActive(h));                     // 페이드 완료 → 정지
}

// ===========================================================================
// AudioEngine: 무음 모드(백엔드 없음)에서 큐 재생 + 오프라인 렌더
// ===========================================================================
MYE_TEST(AudioEngineHeadlessCuePlayback) {
    AudioEngine engine;
    auto r = engine.Initialize(nullptr, 44100);   // 무음 모드
    MYE_EXPECT(static_cast<bool>(r));
    MYE_EXPECT(!engine.HasDevice());

    auto clip = MakeConstS16(64, 16384);
    AudioCue cue;
    cue.clips = {&clip};
    cue.bus = BusId::SFX;
    cue.selection = CueSelect::Sequential;

    VoiceHandle h = engine.PostCue(cue);
    MYE_EXPECT(h.IsValid());

    std::vector<float> out(32 * 2, 0.0f);
    engine.RenderOffline(out.data(), 32);
    MYE_EXPECT(PeakAbs(out) > 0.1f);

    engine.Shutdown();   // 크래시 없이 정리
}

// ===========================================================================
// AudioEngine: 폴리포니 제한(maxVoices)
// ===========================================================================
MYE_TEST(AudioEnginePolyphonyLimit) {
    AudioEngine engine;
    (void)engine.Initialize(nullptr, 44100);

    auto clip = MakeConstS16(4096, 10000);
    AudioCue cue;
    cue.clips = {&clip};
    cue.maxVoices = 2;

    VoiceHandle a = engine.PostCue(cue);
    VoiceHandle b = engine.PostCue(cue);
    VoiceHandle c = engine.PostCue(cue);   // 초과 → 거절
    MYE_EXPECT(a.IsValid());
    MYE_EXPECT(b.IsValid());
    MYE_EXPECT(!c.IsValid());

    engine.Shutdown();
}

// ===========================================================================
// AudioEngine: 버스 볼륨 설정·질의
// ===========================================================================
MYE_TEST(AudioEngineBusVolume) {
    AudioEngine engine;
    (void)engine.Initialize(nullptr, 44100);
    engine.SetBusVolume(BusId::BGM, 0.3f);
    MYE_EXPECT_NEAR(engine.GetBusVolume(BusId::BGM), 0.3f, 1e-5f);
    engine.SetMasterVolume(0.7f);
    MYE_EXPECT_NEAR(engine.GetBusVolume(BusId::Master), 0.7f, 1e-5f);
    engine.Shutdown();
}

// ===========================================================================
// MusicPlayer: 크로스페이드로 두 트랙 전환 — 이전 트랙 페이드아웃, 새 트랙 페이드인
// ===========================================================================
MYE_TEST(MusicPlayerCrossfade) {
    AudioEngine engine;
    (void)engine.Initialize(nullptr, 44100);

    auto trackA = MakeConstS16(1u << 20, 20000);   // 충분히 긴 트랙(루프)
    auto trackB = MakeConstS16(1u << 20, 20000);

    engine.Music().Play(trackA, /*fadeSec=*/0.0f);   // 즉시 시작
    VoiceHandle va = engine.Music().ActiveVoice();
    MYE_EXPECT(va.IsValid());

    // 렌더 조금 진행(A 재생 중).
    std::vector<float> out(512 * 2, 0.0f);
    engine.RenderOffline(out.data(), 512);
    const float aLevel = std::abs(out[0]);
    MYE_EXPECT(aLevel > 0.1f);

    // B 로 크로스페이드(0.1초 ≈ 4410 프레임).
    engine.Music().Play(trackB, /*fadeSec=*/0.1f);
    VoiceHandle vb = engine.Music().ActiveVoice();
    MYE_EXPECT(vb.IsValid());
    MYE_EXPECT(!(vb == va));   // 새 보이스

    // 크로스페이드 초반: B 는 낮은 게인에서 시작.
    std::vector<float> early(256 * 2, 0.0f);
    engine.RenderOffline(early.data(), 256);

    // 크로스페이드 완료까지 렌더.
    std::vector<float> mid(8192 * 2, 0.0f);
    engine.RenderOffline(mid.data(), 8192);
    // 이전 트랙 A 는 페이드아웃 후 정지해야 한다.
    MYE_EXPECT(!engine.Mixer().IsVoiceActive(va));
    // 새 트랙 B 는 여전히 활성(루프).
    MYE_EXPECT(engine.Mixer().IsVoiceActive(vb));

    engine.Shutdown();
}

// ===========================================================================
// 리샘플: 소스 22050Hz → 출력 44100Hz (pitch=1) 재생, 크래시 없이 진폭 보존
// ===========================================================================
MYE_TEST(MixerResampleUpsample) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(128, 20000, 1, /*sampleRate=*/22050);
    mixer.Play(MakeClipSource(clip), PlayParams{});
    std::vector<float> out(64 * 2, 0.0f);
    mixer.RenderInto(out.data(), 64);
    // 상수 신호이므로 업샘플해도 진폭 유지.
    const float expected = 20000.0f / 32768.0f;
    MYE_EXPECT_NEAR(out[32 * 2], expected, 1e-2f);
}

// ===========================================================================
// pitch 변조: pitch=2 → 소스를 2배 속도로 소비(상수 신호라 진폭 동일, 소진 빠름)
// ===========================================================================
MYE_TEST(MixerPitchConsumesFaster) {
    SoftwareMixer mixer(44100);
    auto clip = MakeConstS16(64, 20000);
    PlayParams pp; pp.pitch = 2.0f;
    VoiceHandle h = mixer.Play(MakeClipSource(clip), pp);
    // 64 소스 프레임 / 2 = ~32 출력 프레임에 소진.
    std::vector<float> out(48 * 2, 0.0f);
    mixer.RenderInto(out.data(), 48);
    MYE_EXPECT(!mixer.IsVoiceActive(h));   // 빠르게 소진
    MYE_EXPECT(std::abs(out[0]) > 0.1f);   // 초반은 소리 남
}
