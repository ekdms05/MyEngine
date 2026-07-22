// mye/script/src/bindings/AudioBindings.cpp — 오디오 재생 바인딩 (docs/05·06, M3-C)
//
// mye.audio.play_cue(name, [x, y])   — 큐 이름으로 SFX 재생. x,y 주면 공간화(월드 위치).
// mye.audio.play_music(name, [fade]) — BGM 크로스페이드 재생(초). 기본 0.5s.
// mye.audio.stop_music([fade])       — BGM 페이드아웃.
// mye.audio.set_bus_volume(bus, v)   — 버스 볼륨(선형). bus 는 mye.Bus.* 상수.
// mye.audio.set_listener(x, y)       — 리스너(플레이어) 월드 위치 갱신(감쇠·패닝 기준).
//
// 큐/클립 이름 해석은 앱/데모가 SetCueResolver/SetClipResolver 로 주입한 리졸버가 담당한다
//   (엔진은 큐 레지스트리 비소유). AudioEngine·리졸버 미주입 시 모든 호출은 안전 no-op.
#include "mye/script/bindings/EngineBindings.h"

#include "mye/asset/AudioClip.h"
#include "mye/audio/AudioCue.h"
#include "mye/audio/AudioEngine.h"
#include "mye/audio/AudioTypes.h"
#include "mye/core/Math.h"

#include <sol/sol.hpp>

#include <optional>
#include <string>

namespace mye::script {

void AudioBindingModule::Register(sol::state& lua) {
    audio::AudioEngine* engine = m_engine;   // 비소유 캡처.
    sol::table mye = lua["mye"].get_or_create<sol::table>();
    sol::table au = mye["audio"].get_or_create<sol::table>();

    // 큐 재생: 위치 인자(x,y)는 선택. sol::optional 로 공간화 여부 판정.
    au.set_function("play_cue",
                    [this, engine](const std::string& name,
                                   sol::optional<float> x, sol::optional<float> y) {
        if (!engine || !m_cueResolver) return;
        const audio::AudioCue* cue = m_cueResolver(name);
        if (!cue) return;
        if (x && y) engine->PostCue(*cue, std::optional<Vec2>(Vec2{*x, *y}));
        else        engine->PostCue(*cue);
    });

    // BGM 재생(크로스페이드). fade 기본 0.5초.
    au.set_function("play_music",
                    [this, engine](const std::string& name, sol::optional<float> fade) {
        if (!engine || !m_clipResolver) return;
        const asset::AudioClip* clip = m_clipResolver(name);
        if (!clip) return;
        engine->Music().Play(*clip, fade.value_or(0.5f));
    });

    au.set_function("stop_music", [engine](sol::optional<float> fade) {
        if (engine) engine->Music().Stop(fade.value_or(0.0f));
    });

    // 버스 볼륨(선형 [0,1] 관행). bus 는 정수(mye.Bus.*).
    au.set_function("set_bus_volume", [engine](int bus, float linear) {
        if (!engine) return;
        if (bus < 0 || bus >= static_cast<int>(audio::BusId::Count)) return;
        engine->SetBusVolume(static_cast<audio::BusId>(bus), linear);
    });
    au.set_function("get_bus_volume", [engine](int bus) -> float {
        if (!engine) return 0.0f;
        if (bus < 0 || bus >= static_cast<int>(audio::BusId::Count)) return 0.0f;
        return engine->GetBusVolume(static_cast<audio::BusId>(bus));
    });

    // 리스너(플레이어) 위치.
    au.set_function("set_listener", [engine](float x, float y) {
        if (engine) engine->SetListener(Vec2{x, y});
    });

    // ---- mye.Bus 상수 --------------------------------------------------
    sol::table bus = mye["Bus"].get_or_create<sol::table>();
    bus["MASTER"] = static_cast<int>(audio::BusId::Master);
    bus["BGM"]    = static_cast<int>(audio::BusId::BGM);
    bus["SFX"]    = static_cast<int>(audio::BusId::SFX);
    bus["UI"]     = static_cast<int>(audio::BusId::UI);
    bus["VOICE"]  = static_cast<int>(audio::BusId::Voice);
}

} // namespace mye::script
