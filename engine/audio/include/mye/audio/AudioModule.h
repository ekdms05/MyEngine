// mye/audio/AudioModule.h — 오디오 모듈(코어 Module 라이프사이클 배선) (docs/06 §4, M3-B)
//
// AudioEngine 을 01 IModule 로 감싸 서비스 등록·페이즈 틱·Config 연동·셧다운을 처리한다.
//   OnInitialize: AudioEngine 생성 + MiniaudioBackend 초기화 시도(실패해도 무음 모드로 계속).
//   OnPostInitialize: PostUpdate 페이즈에 Update 틱 등록, Config 버스 섹션 등록·동기.
//   OnShutdown: AudioEngine.Shutdown(장치 스레드 안전 종료 — 데드락 규약 준수).
// AudioEngine 은 EngineContext 서비스로 노출되어 05/06/M3-C 데모가 접근한다.
#pragma once

#include "mye/core/Module.h"

#include <memory>

namespace mye::audio {

class AudioEngine;

class AudioModule final : public IModule {
public:
    MYE_SERVICE(AudioEngine);   // 서비스 이름 = AudioEngine(소비자는 GetService<AudioEngine> 아님 —
                                // 편의상 kServiceId 로 조회. 아래 Engine() 게터 참조).

    AudioModule();
    ~AudioModule() override;

    const char* GetName() const override { return "AudioModule"; }
    std::span<const char* const> GetDependencies() const override;

    void OnLoad(EngineContext& ctx) override;
    void OnInitialize(EngineContext& ctx) override;
    void OnPostInitialize(EngineContext& ctx) override;
    void OnShutdown(EngineContext& ctx) override;

    AudioEngine& Engine();

    // 헤드리스 강제(테스트/CI): 장치 초기화를 건너뛰고 무음 모드로만 동작.
    void SetHeadless(bool headless) { m_headless = headless; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_headless = false;
};

} // namespace mye::audio
