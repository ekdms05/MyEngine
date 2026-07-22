// mye/runtime/AudioListener.h — 2.5D 리스너 배선(플레이어 위치) (docs/06 §4 공간화, M6-A)
//
// 소유: 06 (M6-A 배선). docs/06 §4: "리스너는 카메라가 아니라 관심점(보통 플레이어 캐릭터의 월드
//   위치)". 감쇠는 월드 XY 거리 커브, 패닝은 화면 X 좌우만. 오디오 감쇠·패닝 구현은 mye_audio
//   가 이미 소유(AudioEngine::SetListener/PostCue worldPos) — 여기선 "플레이어 엔티티 위치를 매
//   시뮬 틱 리스너로 밀어주는 배선"만 담당한다(소비, 재구현 아님).
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

namespace mye::audio { class AudioEngine; }
namespace mye::ecs   { class World; }

namespace mye::runtime {

// 플레이어(관심점) 엔티티의 Transform 위치를 AudioEngine 리스너로 배선한다.
//   시뮬레이션 틱(고정틱)에서 Update — 리스너 위치는 게임 상태(플레이어)에서 파생.
class AudioListenerBridge {
public:
    // audio/world 비소유(수명 > bridge). 부분 nullptr 이면 무동작.
    void Initialize(audio::AudioEngine* audio, const ecs::World* world);

    // 관심점 엔티티 설정(보통 플레이어). Null 이면 수동 SetManual 만 반영.
    void SetListenerEntity(ecs::Entity entity);

    // 엔티티 없이 직접 지정(컷신 등 임의 관심점).
    void SetManual(Vec2 worldPos);

    // 매 시뮬 틱 — 관심점 엔티티 Transform → AudioEngine::SetListener.
    void Update();

    Vec2 CurrentListener() const { return m_current; }

private:
    audio::AudioEngine* m_audio = nullptr;
    const ecs::World*   m_world = nullptr;
    ecs::Entity         m_entity{};
    Vec2                m_manual{};
    bool                m_hasEntity = false;
    Vec2                m_current{};
};

} // namespace mye::runtime
