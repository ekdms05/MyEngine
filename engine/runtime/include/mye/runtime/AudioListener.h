// Audio listener interest point, normally a root player entity or manual XY.
// AudioEngine owns attenuation and panning; this bridge updates its position.
// Parented entities currently use LocalTransform XY rather than world matrices.
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
