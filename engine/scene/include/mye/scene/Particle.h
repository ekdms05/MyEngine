// mye/scene/Particle.h — 파티클 에미터 + 시뮬레이션 (docs/03, M7 렌더 잔여)
//
// 에미터가 초당 emissionRate 개 파티클을 스폰하고, 각 파티클은 속도+중력으로 전진하며 수명 동안
// 크기·색을 보간하다 소멸한다. UpdateEmitter 는 순수·결정론(시드 RNG) — GPU 무관·단위 테스트 대상.
// 렌더는 RenderExtract 가 활성 파티클을 Transparent RenderItem 으로 발행(후속 배선).
#pragma once

#include "mye/core/Math.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <vector>

namespace mye::scene {

// 파티클 하나(에미터 로컬 좌표). age/lifetime 로 수명 정규화.
struct Particle {
    Vec2  pos{0.0f, 0.0f};    // 에미터 기준 로컬 오프셋
    Vec2  vel{0.0f, 0.0f};
    float age = 0.0f;         // 경과(초)
    float lifetime = 1.0f;    // 총 수명(초)

    bool  Alive() const { return age < lifetime; }
    float T() const { return lifetime > 0.0f ? (age / lifetime) : 1.0f; }   // 0..1
};

// 파티클 에미터 컴포넌트. 설정 + 활성 파티클 버퍼 + 결정론 RNG 상태.
struct ParticleEmitter {
    MYE_COMPONENT(ParticleEmitter);

    // ---- 설정 ----
    float emissionRate     = 10.0f;   // 초당 스폰 수
    float particleLifetime = 1.0f;    // 파티클 수명(초)
    Vec2  initialVelocity{0.0f, 1.0f};
    float velocityVariance = 0.0f;    // 각 축 초기속도 무작위 스프레드(±)
    Vec2  gravity{0.0f, 0.0f};
    float startScale = 1.0f, endScale = 0.0f;    // 수명에 따른 크기 보간
    Color startColor = Color::White();
    Color endColor   = {1.0f, 1.0f, 1.0f, 0.0f}; // 기본: 알파 페이드아웃
    int   maxParticles = 256;
    bool  emitting = true;

    // ---- 상태 ----
    std::vector<Particle> particles;
    float    spawnAccumulator = 0.0f;
    uint64_t rngState = 0x9E3779B97F4A7C15ull;   // 결정론 시드(에미터별 다르게 설정 권장)

    size_t AliveCount() const { return particles.size(); }
};

// 에미터 한 스텝 전진: 스폰 + 속도/중력 적분 + 나이 + 소멸 제거. 순수·결정론.
void UpdateEmitter(ParticleEmitter& e, float dt);

// 파티클의 현재 보간 크기/색(수명 T 기준). 렌더가 사용.
float ParticleScale(const ParticleEmitter& e, const Particle& p);
Color ParticleColor(const ParticleEmitter& e, const Particle& p);

} // namespace mye::scene
