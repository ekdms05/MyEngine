// AudioModule.cpp — 오디오 모듈 라이프사이클 (docs/06 §4, M3-B)
#include "mye/audio/AudioModule.h"

#include "mye/audio/AudioEngine.h"
#include "mye/audio/MiniaudioBackend.h"
#include "mye/core/Config.h"
#include "mye/core/Log.h"

namespace mye::audio {

struct AudioModule::Impl {
    std::unique_ptr<AudioEngine> engine;
    ModuleRegistry* modules = nullptr;
};

AudioModule::AudioModule() : m_impl(std::make_unique<Impl>()) {}
AudioModule::~AudioModule() = default;

std::span<const char* const> AudioModule::GetDependencies() const {
    // 코어 서비스(Config/EventBus)만 의존 — 모듈 이름 의존 없음.
    return {};
}

void AudioModule::OnLoad(EngineContext& ctx) {
    m_impl->engine = std::make_unique<AudioEngine>();
    // 서비스 등록: kServiceId("AudioEngine") 로 조회 가능.
    ctx.RegisterServiceRaw(kServiceId, m_impl->engine.get());
}

void AudioModule::OnInitialize(EngineContext& /*ctx*/) {
    // 백엔드 선택: 헤드리스 강제면 무음 모드, 아니면 miniaudio 시도(실패해도 무음 모드로 계속).
    std::unique_ptr<IAudioBackend> backend;
    if (!m_headless) backend = std::make_unique<MiniaudioBackend>();

    auto r = m_impl->engine->Initialize(std::move(backend), /*sampleRate=*/44100);
    if (!r) {
        MYE_LOG_WARN("Audio", "AudioModule: device init failed ({}) — 무음 모드로 계속",
                     r.GetError().message);
    } else if (m_impl->engine->HasDevice()) {
        MYE_LOG_INFO("Audio", "AudioModule: audio device 초기화 완료");
    } else {
        MYE_LOG_INFO("Audio", "AudioModule: 헤드리스(무음) 모드");
    }
}

void AudioModule::OnPostInitialize(EngineContext& ctx) {
    // Config 버스 섹션 등록 + 초기 볼륨 동기(User 스코프에 저장 가능).
    ConfigSystem& cfg = ctx.Config();
    cfg.RegisterSection("audio", ConfigScope::User);
    m_impl->engine->SyncFromConfig(cfg);

    // PostUpdate 페이즈에 Update 틱 등록(정리·커맨드 적용).
    m_impl->modules = &ctx.Modules();
    AudioEngine* engine = m_impl->engine.get();
    m_impl->modules->AddTick(this, UpdatePhase::PostUpdate,
        [engine](const TimeStep& t) { engine->Update(static_cast<float>(t.deltaSeconds)); },
        /*orderKey*/ 500);
}

void AudioModule::OnShutdown(EngineContext& ctx) {
    ctx.UnregisterServiceRaw(kServiceId);
    // 셧다운 데드락 규약: AudioEngine.Shutdown 이 백엔드 콜백 스레드를 먼저 조인한 뒤 정리한다.
    if (m_impl->engine) m_impl->engine->Shutdown();
    m_impl->engine.reset();
}

AudioEngine& AudioModule::Engine() { return *m_impl->engine; }

} // namespace mye::audio
