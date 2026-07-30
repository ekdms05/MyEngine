// mye/scene/src/scene/Particle.cpp — 파티클 시뮬레이션 구현 (Particle.h 참조)
#include "mye/scene/Particle.h"

namespace mye::scene {

namespace {
// xorshift64* — 결정론 RNG(전투/가챠와 동일 계열). 0 은 호출부 시드로 회피.
uint64_t NextRng(uint64_t& s) {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
}
float RngUnit(uint64_t& s) { return static_cast<float>(NextRng(s) >> 40) / 16777216.0f; }   // [0,1)
float RngSpread(uint64_t& s, float amp) { return (RngUnit(s) * 2.0f - 1.0f) * amp; }         // [-amp,amp)
} // namespace

void UpdateEmitter(ParticleEmitter& e, float dt) {
    if (dt < 0.0f) return;
    if (e.rngState == 0) e.rngState = 0x9E3779B97F4A7C15ull;

    // 스폰: emissionRate 를 누산해 정수 개수만 스폰(용량 상한 준수).
    if (e.emitting && e.emissionRate > 0.0f) {
        e.spawnAccumulator += e.emissionRate * dt;
        while (e.spawnAccumulator >= 1.0f && static_cast<int>(e.particles.size()) < e.maxParticles) {
            e.spawnAccumulator -= 1.0f;
            Particle p;
            p.lifetime = e.particleLifetime;
            p.age = 0.0f;
            p.pos = {0.0f, 0.0f};
            p.vel = { e.initialVelocity.x + RngSpread(e.rngState, e.velocityVariance),
                      e.initialVelocity.y + RngSpread(e.rngState, e.velocityVariance) };
            e.particles.push_back(p);
        }
        // 용량 초과로 남은 누산은 버린다(무한 성장 방지).
        if (static_cast<int>(e.particles.size()) >= e.maxParticles) e.spawnAccumulator = 0.0f;
    }

    // 적분 + 나이.
    for (Particle& p : e.particles) {
        p.vel = p.vel + e.gravity * dt;
        p.pos = p.pos + p.vel * dt;
        p.age += dt;
    }

    // 소멸 제거(swap-erase 로 압축).
    for (size_t i = 0; i < e.particles.size();) {
        if (!e.particles[i].Alive()) {
            e.particles[i] = e.particles.back();
            e.particles.pop_back();
        } else {
            ++i;
        }
    }
}

float ParticleScale(const ParticleEmitter& e, const Particle& p) {
    return Lerp(e.startScale, e.endScale, p.T());
}

Color ParticleColor(const ParticleEmitter& e, const Particle& p) {
    const float t = p.T();
    return Color{
        Lerp(e.startColor.r, e.endColor.r, t),
        Lerp(e.startColor.g, e.endColor.g, t),
        Lerp(e.startColor.b, e.endColor.b, t),
        Lerp(e.startColor.a, e.endColor.a, t),
    };
}

} // namespace mye::scene
