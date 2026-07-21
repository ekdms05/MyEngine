// MiniaudioBackend.cpp — miniaudio ma_device 기반 IAudioBackend (docs/06 §4, M3-B)
//
// ma_device 를 F32 스테레오 출력 전용으로 초기화하고, 데이터 콜백에서 IAudioSource::PullAudio 로
//   믹서 출력을 장치 버퍼에 채운다. 내장 디코더/리소스 매니저는 impl TU 에서 꺼져 있다(04 디코드 +
//   SoftwareMixer 가 정본). 장치 초기화 실패(헤드리스)는 Error 로 우아 반환 — 크래시 없음.
#include "mye/audio/MiniaudioBackend.h"

#include "miniaudio.h"

namespace mye::audio {

struct MiniaudioBackend::Impl {
    ma_device device{};
    bool      deviceInited = false;
    bool      running = false;
    uint32_t  sampleRate = 44100;
    IAudioSource* source = nullptr;
};

namespace {
// 오디오 스레드 콜백. pOutput 는 F32 인터리브 스테레오. 입력 없음(재생 전용).
// pUserData 는 IAudioSource* (믹서 펌프). 장치 수명 동안 유효.
void DataCallback(ma_device* dev, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) {
    auto* source = static_cast<IAudioSource*>(dev->pUserData);
    float* out = static_cast<float*>(pOutput);
    if (source) {
        source->PullAudio(out, static_cast<uint64_t>(frameCount));
    } else {
        for (ma_uint32 i = 0; i < frameCount * 2; ++i) out[i] = 0.0f;
    }
}
} // namespace

MiniaudioBackend::MiniaudioBackend() : m_impl(std::make_unique<Impl>()) {}
MiniaudioBackend::~MiniaudioBackend() { Shutdown(); }

Expected<void, Error> MiniaudioBackend::Initialize(const AudioDeviceConfig& config,
                                                   IAudioSource* source) {
    if (m_impl->deviceInited) Shutdown();
    m_impl->source = source;
    m_impl->sampleRate = config.sampleRate ? config.sampleRate : 44100;

    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.format   = ma_format_f32;
    dc.playback.channels = 2;                 // 스테레오 고정(믹서 정합)
    dc.sampleRate        = m_impl->sampleRate;
    dc.dataCallback      = &DataCallback;
    dc.pUserData         = source;   // IAudioSource* — 콜백이 직접 펌프
    if (config.framesPerBuffer) dc.periodSizeInFrames = config.framesPerBuffer;

    if (ma_device_init(nullptr, &dc, &m_impl->device) != MA_SUCCESS) {
        m_impl->source = nullptr;
        return Error{"MiniaudioBackend: ma_device_init failed (no audio device?)", -1};
    }
    m_impl->deviceInited = true;
    // 장치의 실제 샘플레이트로 갱신(요청과 다를 수 있음).
    m_impl->sampleRate = m_impl->device.sampleRate;

    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInited = false;
        m_impl->source = nullptr;
        return Error{"MiniaudioBackend: ma_device_start failed", -1};
    }
    m_impl->running = true;
    return {};
}

void MiniaudioBackend::Shutdown() {
    if (m_impl->deviceInited) {
        // ma_device_uninit 이 콜백 스레드를 안전하게 정지·조인한다(내부 처리).
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInited = false;
    }
    m_impl->running = false;
    m_impl->source = nullptr;
}

bool MiniaudioBackend::IsRunning() const { return m_impl->running; }
uint32_t MiniaudioBackend::SampleRate() const { return m_impl->sampleRate; }

} // namespace mye::audio
