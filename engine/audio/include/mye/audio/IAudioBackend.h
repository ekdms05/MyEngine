// mye/audio/IAudioBackend.h — 오디오 출력 백엔드 추상화 (docs/06 §4, M3-B)
//
// RHI 와 동일 철학: 장치·리샘플러·콜백 스레드를 추상화 뒤에 숨긴다. 1차 구현 = MiniaudioBackend,
//   이후 XAudio2·널 백엔드 추가 가능(docs/06 확장 포인트 #4).
// 백엔드는 "SoftwareMixer 를 장치 콜백에서 펌프"하는 얇은 어댑터다. 믹싱 정책(버스·보이스·공간화)은
//   전부 SoftwareMixer 소유 — 백엔드는 프레임을 요청받아 장치로 흘려보낼 뿐이다.
#pragma once

#include "mye/core/Base.h"

#include <cstdint>

namespace mye::audio {

class SoftwareMixer;

struct AudioDeviceConfig {
    uint32_t sampleRate = 44100;
    uint32_t channels   = 2;     // 스테레오 고정(믹서와 정합)
    uint32_t framesPerBuffer = 0; // 0 = 백엔드 기본
};

// 콜백에서 믹서를 펌프하기 위한 소스 인터페이스. 백엔드는 이걸 통해서만 오디오를 얻는다.
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    // dst(스테레오 인터리브 F32)에 frames 프레임을 낸다. 오디오 스레드에서 호출됨.
    virtual void PullAudio(float* dst, uint64_t frames) = 0;
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual const char* Name() const = 0;

    // 장치 초기화. 실장치 실패(헤드리스 CI 등)는 Error 로 우아하게 반환 — 크래시 금지.
    // 성공 후에는 source->PullAudio 가 오디오 스레드에서 주기 호출된다.
    virtual Expected<void, Error> Initialize(const AudioDeviceConfig& config,
                                             IAudioSource* source) = 0;

    // 장치 정지·스레드 조인. 셧다운 데드락 규약 준수(스레드 있으면 안전 종료).
    virtual void Shutdown() = 0;

    virtual bool IsRunning() const = 0;
    virtual uint32_t SampleRate() const = 0;
};

} // namespace mye::audio
