// AudioEngine.cpp — 고수준 오디오 엔진 구현 (docs/06 §4, M3-B)
#include "mye/audio/AudioEngine.h"

#include "mye/audio/ClipSource.h"
#include "mye/core/Config.h"

#include <algorithm>

namespace mye::audio {

// ---- MusicPlayer ----
void MusicPlayer::Play(const asset::AudioClip& clip, float fadeSec, float volume) {
    auto source = MakeClipSource(clip);
    if (!source) return;

    PlayParams pp;
    pp.bus = BusId::BGM;
    pp.loop = true;
    pp.volume = std::max(0.0f, volume);

    const uint32_t sr = m_mixer->OutputSampleRate();
    const uint64_t fadeFrames = fadeSec > 0.0f
        ? static_cast<uint64_t>(static_cast<double>(fadeSec) * sr) : 0;

    // 이전 트랙(있으면) 페이드아웃 후 정지.
    if (m_active.IsValid() && m_mixer->IsVoiceActive(m_active)) {
        // 직전 페이드아웃 중이던 트랙이 남아있으면 즉시 정지(2 보이스만 유지).
        if (m_previous.IsValid()) m_mixer->Stop(m_previous);
        m_previous = m_active;
        m_mixer->FadeOutAndStop(m_previous, fadeFrames);
    }

    // 새 트랙: 0 게인에서 시작해 target 볼륨으로 페이드 인(크로스페이드).
    m_active = m_mixer->Play(std::move(source), pp);
    if (fadeFrames > 0) {
        m_mixer->SetVoiceFade(m_active, 0.0f, 0);          // 즉시 0
        m_mixer->SetVoiceFade(m_active, 1.0f, fadeFrames); // 램프 인
    }
}

void MusicPlayer::Stop(float fadeSec) {
    const uint32_t sr = m_mixer->OutputSampleRate();
    const uint64_t fadeFrames = fadeSec > 0.0f
        ? static_cast<uint64_t>(static_cast<double>(fadeSec) * sr) : 0;
    if (m_active.IsValid()) m_mixer->FadeOutAndStop(m_active, fadeFrames);
    if (m_previous.IsValid()) m_mixer->Stop(m_previous);
    m_active = {};
    m_previous = {};
}

// ---- PullAdapter: 오디오 콜백 → 믹서 렌더(락으로 커맨드 경계 보호) ----
class AudioEngine::PullAdapter final : public IAudioSource {
public:
    PullAdapter(SoftwareMixer& mixer, std::mutex& mtx) : m_mixer(&mixer), m_mtx(&mtx) {}
    void PullAudio(float* dst, uint64_t frames) override {
        std::lock_guard<std::mutex> lock(*m_mtx);
        m_mixer->RenderInto(dst, frames);
    }
private:
    SoftwareMixer* m_mixer;
    std::mutex*    m_mtx;
};

// ---- AudioEngine ----
AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { Shutdown(); }

Expected<void, Error> AudioEngine::Initialize(std::unique_ptr<IAudioBackend> backend,
                                              uint32_t sampleRate) {
    m_mixer = std::make_unique<SoftwareMixer>(sampleRate);
    m_music = std::make_unique<MusicPlayer>(*m_mixer);
    m_pull = std::make_unique<PullAdapter>(*m_mixer, m_mixerMutex);

    if (!backend) {
        // 무음(오프라인) 모드 — 정상. 테스트가 RenderOffline 로 검증.
        return {};
    }

    AudioDeviceConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.channels = 2;
    auto r = backend->Initialize(cfg, m_pull.get());
    if (!r) {
        // 장치 실패 → 무음 모드로 계속(크래시 없음). 백엔드는 보관하지 않는다.
        return r.GetError();
    }
    // 장치 실제 샘플레이트로 믹서 동기.
    m_mixer->SetOutputSampleRate(backend->SampleRate());
    m_backend = std::move(backend);
    return {};
}

void AudioEngine::Shutdown() {
    // 백엔드 먼저 정지 → 콜백 스레드 조인(더 이상 믹서 접근 없음). 셧다운 데드락 규약 준수:
    //   여기서 뮤텍스를 잡지 않는다(백엔드 Shutdown 이 콜백을 조인하므로 렌더 스레드 소멸 후 안전).
    if (m_backend) { m_backend->Shutdown(); m_backend.reset(); }
    m_music.reset();
    m_pull.reset();
    m_mixer.reset();
    m_cueVoices.clear();
}

uint32_t AudioEngine::NextRandom() {
    // xorshift32(결정적, 테스트 재현성).
    m_randState ^= m_randState << 13;
    m_randState ^= m_randState >> 17;
    m_randState ^= m_randState << 5;
    return m_randState;
}
float AudioEngine::RandRange(float lo, float hi) {
    if (hi <= lo) return lo;
    const float t = (NextRandom() & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
    return Lerp(lo, hi, t);
}

uint32_t AudioEngine::CountCueVoices(const AudioCue* cue) const {
    uint32_t n = 0;
    for (const auto& rec : m_cueVoices)
        if (rec.cue == cue && m_mixer->IsVoiceActive(rec.handle)) ++n;
    return n;
}

void AudioEngine::PruneInactive() {
    m_cueVoices.erase(
        std::remove_if(m_cueVoices.begin(), m_cueVoices.end(),
                       [&](const CueVoiceRec& r) { return !m_mixer->IsVoiceActive(r.handle); }),
        m_cueVoices.end());
}

VoiceHandle AudioEngine::PostCue(const AudioCue& cue, std::optional<Vec2> worldPos) {
    if (!m_mixer || !cue.IsValid()) return {};
    std::lock_guard<std::mutex> lock(m_mixerMutex);

    PruneInactive();

    // 폴리포니 제한.
    if (cue.maxVoices != 0 && CountCueVoices(&cue) >= cue.maxVoices) {
        return {};   // 초과 → 거절(스틸 정책은 후속)
    }

    // 클립 선택.
    const asset::AudioClip* clip = nullptr;
    const uint32_t count = static_cast<uint32_t>(cue.clips.size());
    if (cue.selection == CueSelect::Sequential) {
        clip = cue.clips[cue.sequentialCursor % count];
        cue.sequentialCursor = (cue.sequentialCursor + 1) % count;
    } else {
        clip = cue.clips[NextRandom() % count];
    }
    if (!clip) return {};

    auto source = MakeClipSource(*clip);
    if (!source) return {};

    PlayParams pp;
    pp.bus = cue.bus;
    pp.loop = cue.loop;
    pp.volume = RandRange(cue.volumeMin, cue.volumeMax);
    pp.pitch  = RandRange(cue.pitchMin, cue.pitchMax);

    VoiceHandle h;
    if (worldPos) {
        SpatialParams sp;
        sp.worldPos = *worldPos;
        h = m_mixer->PlaySpatial(std::move(source), pp, sp);
    } else {
        h = m_mixer->Play(std::move(source), pp);
    }
    if (h.IsValid()) m_cueVoices.push_back({&cue, h});
    return h;
}

void AudioEngine::Stop(VoiceHandle h) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    m_mixer->Stop(h);
}

void AudioEngine::SetBusVolume(BusId bus, float linear) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    m_mixer->SetBusVolume(bus, linear);
}
float AudioEngine::GetBusVolume(BusId bus) const {
    if (!m_mixer) return 0.0f;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    return m_mixer->GetBusVolume(bus);
}
void AudioEngine::SetBusMute(BusId bus, bool mute) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    m_mixer->SetBusMute(bus, mute);
}

void AudioEngine::SyncFromConfig(const ConfigSystem& config) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    struct { const char* key; BusId bus; } vols[] = {
        {"audio.master", BusId::Master}, {"audio.bgm", BusId::BGM},
        {"audio.sfx", BusId::SFX}, {"audio.ui", BusId::UI}, {"audio.voice", BusId::Voice},
    };
    for (auto& e : vols) {
        const double cur = m_mixer->GetBusVolume(e.bus);
        const double v = config.GetFloat(e.key, cur);
        m_mixer->SetBusVolume(e.bus, static_cast<float>(v));
    }
    struct { const char* key; BusId bus; } mutes[] = {
        {"audio.master.mute", BusId::Master}, {"audio.bgm.mute", BusId::BGM},
        {"audio.sfx.mute", BusId::SFX}, {"audio.ui.mute", BusId::UI}, {"audio.voice.mute", BusId::Voice},
    };
    for (auto& e : mutes) {
        m_mixer->SetBusMute(e.bus, config.GetBool(e.key, m_mixer->GetBusMute(e.bus)));
    }
}

void AudioEngine::SetListener(Vec2 worldPos) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    m_mixer->SetListener(worldPos);
}
Vec2 AudioEngine::GetListener() const {
    if (!m_mixer) return {};
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    return m_mixer->GetListener();
}

void AudioEngine::Update(float /*dtSeconds*/) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    PruneInactive();
}

void AudioEngine::RenderOffline(float* dst, uint64_t frames) {
    if (!m_mixer) return;
    std::lock_guard<std::mutex> lock(m_mixerMutex);
    m_mixer->RenderInto(dst, frames);
}

bool AudioEngine::HasDevice() const { return m_backend != nullptr && m_backend->IsRunning(); }

} // namespace mye::audio
