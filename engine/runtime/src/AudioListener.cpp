// mye/runtime/AudioListener.cpp — 2.5D 리스너 배선 골격 (M6-A)
//
// 골격 범위: 관심점 소스(엔티티/수동) 선택과 AudioEngine::SetListener 호출 배선. 엔티티 Transform
//   조회는 구현 에이전트가 World 컴포넌트 접근으로 채운다(현재는 manual 경로만 실동작).
#include "mye/runtime/AudioListener.h"

#include "mye/audio/AudioEngine.h"
#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

namespace mye::runtime {

void AudioListenerBridge::Initialize(audio::AudioEngine* audio, const ecs::World* world) {
    m_audio = audio;
    m_world = world;
}

void AudioListenerBridge::SetListenerEntity(ecs::Entity entity) {
    m_entity = entity;
    m_hasEntity = !entity.IsNull();
}

void AudioListenerBridge::SetManual(Vec2 worldPos) {
    m_manual = worldPos;
    m_hasEntity = false;
}

void AudioListenerBridge::Update() {
    if (!m_audio) return;
    Vec2 pos = m_manual;
    if (m_hasEntity && m_world && !m_entity.IsNull() && m_world->Valid(m_entity)) {
        // 관심점 엔티티의 LocalTransform 을 조회해 월드 XY(+Y 업)로 리스너 위치 산출.
        //   AudioListenerBridge 는 const World* 를 보유하므로 const TryGetDynamic 경로를 쓴다.
        //   (부모 없는 플레이어 캐릭터 가정 — 계층 월드행렬은 표현 계층이 별도 소유. 관심점은
        //    보통 루트 엔티티라 LocalTransform 로 충분. 필요 시 WorldTransform 로 승격 가능.)
        const void* tc = m_world->TryGetDynamic(
            m_entity, scene::LocalTransform::kComponentTypeId);
        if (tc) {
            const auto* lt = static_cast<const scene::LocalTransform*>(tc);
            pos = Vec2{lt->position.x, lt->position.y};
        }
    }
    m_current = pos;
    m_audio->SetListener(pos);
}

} // namespace mye::runtime
