// mye/audio/SoftwareMixer.h — 순수 소프트웨어 믹서(오프라인 렌더 가능) (docs/06 §4, M3-B)
//
// 백엔드(장치)와 무관하게 보이스를 스테레오 F32 로 오프라인 믹싱한다. 이것이 엔진의 "믹서 토폴로지"
// 정본이며, 헤드리스 테스트는 이 클래스에 직접 보이스를 큐잉하고 RenderInto 로 출력 버퍼를 수치 검증한다.
// MiniaudioBackend 는 장치 콜백에서 동일한 RenderInto 를 호출해 실장치로 낸다(단일 믹싱 경로).
//
// 스레딩: 이 클래스 자체는 락을 두지 않는다(단일 소비 스레드 가정). AudioEngine 이 커맨드 큐로
//   메인 스레드 ↔ 오디오 콜백 스레드 경계를 보호한다(docs/06 오픈이슈 #4 — 큐 친화 설계).
#pragma once

#include "mye/audio/AudioTypes.h"
#include "mye/audio/ClipSource.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mye::audio {

// 출력 샘플레이트/채널은 고정(스테레오). 소스 채널·샘플레이트는 보이스가 리샘플·업믹스.
inline constexpr uint32_t kMixerChannels = 2;

// 크로스페이드/버스 볼륨을 프레임 단위로 램프하기 위한 선형 게인 램프.
struct GainRamp {
    float current = 1.0f;
    float target  = 1.0f;
    float perFrame = 0.0f;   // 프레임당 증분(target 도달까지). 0 = 즉시(스텝).

    void SetImmediate(float g) { current = target = g; perFrame = 0.0f; }
    // durationFrames 프레임에 걸쳐 target 으로 이동.
    void RampTo(float g, uint64_t durationFrames);
    // 한 프레임 전진 후 현재 게인 반환.
    float Advance();
    bool AtTarget() const { return perFrame == 0.0f || current == target; }
};

// 믹서 내부 보이스. 소스 + 재생 파라미터 + 공간화 + 페이드.
struct MixerVoice {
    std::unique_ptr<IClipSource> source;
    PlayParams   params{};
    bool         spatial = false;
    SpatialParams spatialParams{};

    // 페이드(크로스페이드/원샷 페이드아웃). 볼륨에 곱해지는 별도 램프.
    GainRamp     fade{};
    bool         stopWhenFadedOut = false;   // fade.target==0 도달 시 자동 정지

    // 상태
    bool         active = false;
    bool         paused = false;
    VoiceHandle  handle{};

    // 리샘플 위상(0..1 소수부) — 블록 간 연속성. 정수부는 소비 프레임 수로 소스를 전진시켜 흡수.
    double       phase = 0.0;
    // 블록 경계를 넘는 선형 보간을 위해 직전 블록의 마지막 소스 프레임(스테레오)을 보관.
    float        histL = 0.0f, histR = 0.0f;
    bool         hasHistory = false;
};

class SoftwareMixer {
public:
    explicit SoftwareMixer(uint32_t outputSampleRate = 44100);

    void SetOutputSampleRate(uint32_t sr) { m_sampleRate = sr; }
    uint32_t OutputSampleRate() const { return m_sampleRate; }

    // ---- 버스 ----
    void  SetBusVolume(BusId bus, float linear);
    float GetBusVolume(BusId bus) const;
    void  SetBusMute(BusId bus, bool mute);
    bool  GetBusMute(BusId bus) const;
    // 버스 볼륨을 durationFrames 에 걸쳐 램프(즉시=0).
    void  RampBusVolume(BusId bus, float linear, uint64_t durationFrames);

    // ---- 보이스 ----
    // 소스를 큐잉하고 핸들 반환. 무효 소스면 무효 핸들.
    VoiceHandle Play(std::unique_ptr<IClipSource> source, const PlayParams& params);
    // 공간화 보이스(worldPos 기반 감쇠·패닝). params.pan 은 무시되고 공간화가 계산.
    VoiceHandle PlaySpatial(std::unique_ptr<IClipSource> source, const PlayParams& params,
                            const SpatialParams& spatial);

    void Stop(VoiceHandle h);                     // 즉시 정지·슬롯 회수
    void FadeOutAndStop(VoiceHandle h, uint64_t durationFrames);  // 페이드 후 정지
    void SetVoiceFade(VoiceHandle h, float target, uint64_t durationFrames); // 크로스페이드 인
    void SetVoiceVolume(VoiceHandle h, float linear);
    void SetVoicePaused(VoiceHandle h, bool paused);
    bool IsVoiceActive(VoiceHandle h) const;
    uint32_t ActiveVoiceCount() const;

    // 특정 큐(버스+식별 태그) 폴리포니 카운트용: 태그별 활성 보이스 수.
    // (여기선 단순화 — AudioEngine 이 큐별 카운트를 자체 관리, 믹서는 버스별만 노출.)
    uint32_t ActiveVoiceCountOnBus(BusId bus) const;

    // ---- 리스너 ----
    void SetListener(Vec2 worldPos) { m_listener = worldPos; }
    Vec2 GetListener() const { return m_listener; }

    // ---- 렌더 ----
    // dst(스테레오 인터리브 F32, frames*2 크기)에 믹스. 기존 내용을 덮어쓴다(가산 아님).
    // 오프라인 테스트·장치 콜백 공통 진입점. 클리핑은 하지 않는다(수치 검증 신뢰성).
    void RenderInto(float* dst, uint64_t frames);

private:
    struct Bus {
        GainRamp gain{};
        bool     mute = false;
    };

    MixerVoice* Resolve(VoiceHandle h);
    const MixerVoice* Resolve(VoiceHandle h) const;
    VoiceHandle Allocate();

    uint32_t m_sampleRate;
    Vec2     m_listener{};
    Bus      m_buses[kBusCount];
    std::vector<MixerVoice> m_voices;
    std::vector<uint32_t>   m_generations;   // 슬롯별 generation
    uint32_t m_generationCounter = 1;
};

} // namespace mye::audio
