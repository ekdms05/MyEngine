// mye/audio/MiniaudioBackend.h — miniaudio 기반 IAudioBackend 1차 구현 (docs/06 §4, M3-B)
//
// ma_device 를 raw 출력 모드로 초기화하고, 데이터 콜백에서 IAudioSource::PullAudio 를 호출해
//   믹서 출력을 장치로 흘린다(내장 디코더·리소스 매니저 미사용 — 04 디코드 + SoftwareMixer 소유).
// 헤드리스: ma_device_init 실패(장치 없음)면 Initialize 가 Error 를 반환하고 IsRunning()=false.
//   상위(AudioEngine)는 이 경우 무음 모드로 계속 동작한다(크래시 없음).
//
// 셧다운: ma_device_uninit 가 콜백 스레드를 안전 조인한다(miniaudio 내부 처리). 우리 쪽 추가
//   뮤텍스는 없다 — PullAudio 는 믹서 커맨드 큐를 통해 메인 스레드와 통신하므로 데드락 위험 없음.
#pragma once

#include "mye/audio/IAudioBackend.h"

#include <memory>

namespace mye::audio {

class MiniaudioBackend final : public IAudioBackend {
public:
    MiniaudioBackend();
    ~MiniaudioBackend() override;

    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;

    const char* Name() const override { return "MiniaudioBackend"; }

    Expected<void, Error> Initialize(const AudioDeviceConfig& config,
                                     IAudioSource* source) override;
    void Shutdown() override;
    bool IsRunning() const override;
    uint32_t SampleRate() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::audio
