// mye/audio/AudioEngine.h — 고수준 오디오 엔진(믹서·버스·큐·BGM 크로스페이드) (docs/06 §4, M3-B)
//
// 06 API 표면(docs/06 §핵심 타입 AudioSystem)의 구현체. 소유:
//   - SoftwareMixer(믹싱 정본) + IAudioBackend(장치 출력, 없으면 무음 모드).
//   - 커맨드 큐: 메인 스레드가 큐잉, 오디오 스레드가 PullAudio 시점에 적용(락 최소, 오픈이슈 #4).
//   - AudioCue 재생(PostCue), 정지, 폴리포니 제한.
//   - MusicPlayer(2 보이스 BGM 크로스페이드).
//   - 리스너·거리 감쇠·패닝(worldPos 주어진 재생).
//   - 버스/마스터 볼륨 Config 연동(SyncFromConfig).
//
// 애니메이션 이벤트→AudioCue 배선은 M3-C(데모). 여기선 재생 API 제공만.
#pragma once

#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioTypes.h"
#include "mye/audio/IAudioBackend.h"
#include "mye/audio/SoftwareMixer.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace mye { class ConfigSystem; }

namespace mye::audio {

// BGM 2 보이스 크로스페이드 재생기. AudioEngine 내부 소유, music() 로 노출.
class MusicPlayer {
public:
    explicit MusicPlayer(SoftwareMixer& mixer) : m_mixer(&mixer) {}

    // 새 트랙으로 fadeSec 에 걸쳐 크로스페이드. 이전 트랙은 페이드아웃 후 자동 정지.
    // fadeSec<=0 이면 즉시 전환. clip 수명은 호출자가 보장(핸들 유지 등).
    void Play(const asset::AudioClip& clip, float fadeSec, float volume = 1.0f);
    void Stop(float fadeSec = 0.0f);   // 현재 트랙 페이드아웃

    bool IsPlaying() const { return m_active.IsValid(); }
    VoiceHandle ActiveVoice() const { return m_active; }

private:
    SoftwareMixer* m_mixer;
    VoiceHandle    m_active{};      // 현재(페이드 인 중이거나 재생 중)
    VoiceHandle    m_previous{};    // 페이드 아웃 중인 이전 트랙
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // backend=nullptr 면 무음(오프라인) 모드 — 믹서는 살아있고 테스트가 RenderInto 가능.
    // 장치 초기화 실패는 Error 를 반환하되 엔진은 무음 모드로 계속 동작(크래시 없음).
    Expected<void, Error> Initialize(std::unique_ptr<IAudioBackend> backend,
                                     uint32_t sampleRate = 44100);
    void Shutdown();

    // ---- 큐 재생 ----
    // AudioCue 를 재생하고 인스턴스 핸들 반환. worldPos 있으면 공간화(감쇠·패닝).
    VoiceHandle PostCue(const AudioCue& cue, std::optional<Vec2> worldPos = std::nullopt);
    void Stop(VoiceHandle h);

    // ---- 버스 ----
    void  SetBusVolume(BusId bus, float linear);
    float GetBusVolume(BusId bus) const;
    void  SetBusMute(BusId bus, bool mute);
    void  SetMasterVolume(float linear) { SetBusVolume(BusId::Master, linear); }

    // Config("audio.master","audio.bgm","audio.sfx","audio.ui","audio.voice", "audio.<bus>.mute")
    // 에서 버스 볼륨/뮤트를 읽어 적용. 미설정 키는 유지. RegisterSection 은 호출자 몫.
    void SyncFromConfig(const ConfigSystem& config);

    // ---- 리스너 ----
    void SetListener(Vec2 worldPos);
    Vec2 GetListener() const;

    // ---- BGM ----
    MusicPlayer& Music() { return *m_music; }

    // ---- 프레임 갱신(메인 스레드) ----
    // 커맨드 적용 타이밍·엔진 상태 정리. 백엔드 유무와 무관하게 호출 가능.
    void Update(float dtSeconds);

    // ---- 테스트/오프라인 훅 ----
    // 믹서 직접 접근(헤드리스 테스트가 RenderInto 로 출력 검증). 프로덕션 경로는 백엔드가 펌프.
    SoftwareMixer& Mixer() { return *m_mixer; }
    // 무음 모드(백엔드 없음/실패)에서 dst 로 오프라인 렌더. 백엔드 모드에선 장치가 이미 소비.
    void RenderOffline(float* dst, uint64_t frames);

    bool HasDevice() const;

private:
    class PullAdapter;   // IAudioSource → 믹서 펌프(락으로 커맨드 경계 보호)

    // 큐별 폴리포니 카운트. 큐 식별은 포인터(같은 AudioCue 인스턴스) 기준.
    struct CueVoiceRec { const AudioCue* cue; VoiceHandle handle; };

    void PruneInactive();
    uint32_t CountCueVoices(const AudioCue* cue) const;

    std::unique_ptr<SoftwareMixer>     m_mixer;
    std::unique_ptr<IAudioBackend>     m_backend;
    std::unique_ptr<PullAdapter>       m_pull;
    std::unique_ptr<MusicPlayer>       m_music;

    std::vector<CueVoiceRec>           m_cueVoices;
    uint32_t                           m_randState = 0x1234567u;  // 큐 랜덤 선택용

    // 믹서 접근 직렬화: 메인 스레드 커맨드 vs 오디오 콜백 렌더.
    // (SoftwareMixer 는 락이 없으므로 여기서 보호. 짧은 크리티컬 섹션.)
    mutable std::mutex m_mixerMutex;

    uint32_t NextRandom();
    float    RandRange(float lo, float hi);
};

} // namespace mye::audio
