// ParticleTests.cpp — 파티클 에미터 시뮬레이션(순수·결정론) (docs/03, M7 렌더 잔여)
#include "TestFramework.h"

#include "mye/scene/Particle.h"

#include <cmath>

using namespace mye;
using namespace mye::scene;

namespace { bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; } }

MYE_TEST(ParticleEmissionRate) {
    ParticleEmitter e;
    e.emissionRate = 10.0f;      // 10/sec
    e.particleLifetime = 100.0f; // 오래 살아 소멸 영향 배제
    e.initialVelocity = {0, 0};

    // 1초 동안 60틱(1/60) → 약 10개 스폰.
    for (int i = 0; i < 60; ++i) UpdateEmitter(e, 1.0f / 60.0f);
    MYE_EXPECT(e.AliveCount() == 10);

    // emitting=false 면 더 스폰 안 함.
    e.emitting = false;
    for (int i = 0; i < 60; ++i) UpdateEmitter(e, 1.0f / 60.0f);
    MYE_EXPECT(e.AliveCount() == 10);
}

MYE_TEST(ParticleLifetimeCulling) {
    ParticleEmitter e;
    e.emissionRate = 0.0f;      // 자동 스폰 없음(수동 주입)
    e.particleLifetime = 0.5f;

    // 수동으로 파티클 1개 주입.
    Particle p; p.lifetime = 0.5f; p.age = 0.0f; p.vel = {1, 0};
    e.particles.push_back(p);
    MYE_EXPECT(e.AliveCount() == 1);

    // 0.4초 경과 → 아직 생존.
    UpdateEmitter(e, 0.4f);
    MYE_EXPECT(e.AliveCount() == 1);
    // 위치 전진(vx=1, 0.4초 → x≈0.4).
    MYE_EXPECT(Near(e.particles[0].pos.x, 0.4f));

    // 추가 0.2초(총 0.6 > 0.5) → 소멸.
    UpdateEmitter(e, 0.2f);
    MYE_EXPECT(e.AliveCount() == 0);
}

MYE_TEST(ParticleGravityIntegration) {
    ParticleEmitter e;
    e.emissionRate = 0.0f;
    Particle p; p.lifetime = 100.0f; p.vel = {0, 0};
    e.particles.push_back(p);
    e.gravity = {0, -10.0f};   // 아래로

    // 반암시적 오일러: vel += g*dt; pos += vel*dt. dt=0.1 한 스텝 → vel.y=-1, pos.y=-0.1.
    UpdateEmitter(e, 0.1f);
    MYE_EXPECT(Near(e.particles[0].vel.y, -1.0f));
    MYE_EXPECT(Near(e.particles[0].pos.y, -0.1f));
}

MYE_TEST(ParticleMaxCap) {
    ParticleEmitter e;
    e.emissionRate = 1000.0f;   // 폭발적
    e.particleLifetime = 100.0f;
    e.maxParticles = 50;

    for (int i = 0; i < 10; ++i) UpdateEmitter(e, 1.0f / 60.0f);
    MYE_EXPECT(e.AliveCount() == 50);   // 상한 초과 안 함
}

MYE_TEST(ParticleDeterminism) {
    // 같은 시드 → 같은 파티클 궤적(재현성).
    ParticleEmitter a, b;
    a.velocityVariance = b.velocityVariance = 5.0f;
    a.rngState = b.rngState = 12345;
    a.particleLifetime = b.particleLifetime = 100.0f;

    for (int i = 0; i < 30; ++i) { UpdateEmitter(a, 1.0f / 60.0f); UpdateEmitter(b, 1.0f / 60.0f); }
    MYE_EXPECT(a.AliveCount() == b.AliveCount() && a.AliveCount() > 0);
    for (size_t i = 0; i < a.AliveCount(); ++i) {
        MYE_EXPECT(Near(a.particles[i].pos.x, b.particles[i].pos.x));
        MYE_EXPECT(Near(a.particles[i].vel.x, b.particles[i].vel.x));
    }
}

MYE_TEST(ParticleScaleColorInterpolation) {
    ParticleEmitter e;
    e.startScale = 2.0f; e.endScale = 0.0f;
    e.startColor = {1, 1, 1, 1};
    e.endColor   = {0, 0, 0, 0};

    Particle p; p.lifetime = 1.0f;
    // t=0 → start.
    p.age = 0.0f;
    MYE_EXPECT(Near(ParticleScale(e, p), 2.0f));
    MYE_EXPECT(Near(ParticleColor(e, p).a, 1.0f));
    // t=0.5 → 중간.
    p.age = 0.5f;
    MYE_EXPECT(Near(ParticleScale(e, p), 1.0f));
    MYE_EXPECT(Near(ParticleColor(e, p).a, 0.5f));
    // t=1 → end(알파 페이드아웃).
    p.age = 1.0f;
    MYE_EXPECT(Near(ParticleScale(e, p), 0.0f));
    MYE_EXPECT(Near(ParticleColor(e, p).a, 0.0f));
}
