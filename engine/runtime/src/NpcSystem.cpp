// mye/runtime/NpcSystem.cpp — NPC 배회·상호작용 시스템 구현 (M6-B)
//
// 배회 상태기계(고정틱):
//   Idle → (대기타이머 만료) → 다음 웨이포인트 선택 → MoveController.MoveTo → Moving
//   Moving → (도착) → Idle(대기타이머 리셋)
//   임의 상태 → (플레이어 alertRadius 진입) → AlertPaused(이동 취소·플레이어 주시)
//   AlertPaused → (플레이어 멀어짐) → Idle
//   임의 상태 → (상호작용 개시) → Interacting(이동 취소) → (busy 종료) → Idle
//
// 애니: 이동 중이면 facing 을 이동 방향으로, 정지/주시면 마지막 방향 유지(주시는 플레이어 방향).
//   SpriteAnimator.facing 을 직접 갱신하고, 관례 파라미터 "moving"(bool)/"speed"(float) 도 채운다
//   (상태머신이 있으면 walk/idle 전이에 쓰고, 없으면 무해).
#include "mye/runtime/NpcSystem.h"

#include "mye/runtime/CutsceneRuntime.h"   // MoveController

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"
#include "mye/anim/SpriteAnimator.h"
#include "mye/anim/AnimationTypes.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mye::runtime {

namespace {

// 결정론적 경량 난수(테스트 재현성). xorshift32 — std::rand 회피(스레드/전역 상태 없음).
struct Rng {
    uint32_t s = 0x12345678u;
    uint32_t Next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    float Unit() { return static_cast<float>(Next() & 0xFFFFFFu) / 16777215.0f; } // [0,1]
    float Range(float lo, float hi) { return lo + (hi - lo) * Unit(); }
};

} // namespace

// ---------------------------------------------------------------------------
struct NpcSystem::Impl {
    ecs::World*     world = nullptr;
    MoveController* move  = nullptr;

    InteractFn      onBegin;
    InteractBusyFn  onBusy;

    ecs::Entity player = ecs::Entity::Null();
    ecs::Entity interacting = ecs::Entity::Null();

    struct Npc {
        NpcDesc  desc;
        NpcState state = NpcState::Idle;
        float    waitTimer = 0.0f;     // Idle 에서 남은 대기 시간
        int32_t  wpIndex = 0;          // Patrol 현재 웨이포인트 인덱스
        int32_t  wpDir = 1;            // 핑퐁 방향(+1/-1)
        Vec2     target{0, 0};         // 현재 이동 목표
        Vec2     origin{0, 0};         // 등록 시 위치(Random 반경 기준)
        anim::Dir8 facing = anim::Dir8::Down;
        Rng      rng;
    };
    // entity.Packed() → Npc. 안정적 순회(등록 순서 무관, 독립).
    std::unordered_map<uint64_t, Npc> npcs;

    // 엔티티의 현재 월드 XY(LocalTransform). 없으면 false.
    bool PosOf(ecs::Entity e, Vec2& out) const {
        if (!world) return false;
        auto* tf = world->TryGet<scene::LocalTransform>(e);
        if (!tf) return false;
        out = Vec2{tf->position.x, tf->position.y};
        return true;
    }

    // 애니 파라미터·방향 갱신(있으면). moving=이동중 여부, dir=바라볼 방향.
    void ApplyAnim(ecs::Entity e, bool moving, anim::Dir8 dir, float speed) {
        if (!world) return;
        auto* an = world->TryGet<anim::SpriteAnimator>(e);
        if (!an) return;
        an->facing = dir;
        an->SetBool("moving", moving);
        an->SetFloat("speed", speed);
    }

    // 다음 목표를 골라 이동을 시작한다. 목표가 유효하면 Moving 으로.
    void PickNextTarget(Npc& n, ecs::Entity e, Vec2 cur) {
        Vec2 tgt = cur;
        const auto& wps = n.desc.waypoints;
        if (n.desc.mode == WanderMode::Patrol && !wps.empty()) {
            // 다음 인덱스(루프 또는 핑퐁).
            int32_t next = n.wpIndex + n.wpDir;
            if (next >= static_cast<int32_t>(wps.size())) {
                if (n.desc.loop) next = 0;
                else { n.wpDir = -1; next = static_cast<int32_t>(wps.size()) - 2; if (next < 0) next = 0; }
            } else if (next < 0) {
                n.wpDir = 1; next = wps.size() > 1 ? 1 : 0;
            }
            n.wpIndex = next;
            tgt = wps[static_cast<size_t>(next)];
        } else if (n.desc.mode == WanderMode::Random && !wps.empty()) {
            n.wpIndex = static_cast<int32_t>(n.rng.Next() % wps.size());
            tgt = wps[static_cast<size_t>(n.wpIndex)];
        } else if (n.desc.mode == WanderMode::Random) {
            // 웨이포인트 없음 → 원점 주변 무작위.
            float ang = n.rng.Range(0.0f, 6.2831853f);
            float r   = n.rng.Range(0.0f, n.desc.wanderRadius);
            tgt = Vec2{n.origin.x + r * std::cos(ang), n.origin.y + r * std::sin(ang)};
        } else {
            // None 또는 목표 없음 → 이동 안 함.
            n.state = NpcState::Idle;
            n.waitTimer = n.desc.waitMin;
            return;
        }

        n.target = tgt;
        n.state = NpcState::Moving;
        if (move) move->MoveTo(e, tgt, n.desc.moveSpeed);
    }

    // 상호작용을 취소하고 배회를 재개(이동 정지 상태로 Idle).
    void EndInteraction(Npc& n, ecs::Entity e) {
        if (move) move->Cancel(e);
        n.state = NpcState::Idle;
        n.waitTimer = n.desc.waitMin;
    }
};

// ---------------------------------------------------------------------------
NpcSystem::NpcSystem() : m_impl(std::make_unique<Impl>()) {}
NpcSystem::~NpcSystem() = default;

void NpcSystem::Initialize(ecs::World* world, MoveController* move) {
    m_impl->world = world;
    m_impl->move  = move;
}

void NpcSystem::Shutdown() {
    m_impl->npcs.clear();
    m_impl->onBegin = nullptr;
    m_impl->onBusy  = nullptr;
    m_impl->player = ecs::Entity::Null();
    m_impl->interacting = ecs::Entity::Null();
    m_impl->world = nullptr;
    m_impl->move  = nullptr;
}

void NpcSystem::SetInteractHandlers(InteractFn begin, InteractBusyFn busy) {
    m_impl->onBegin = std::move(begin);
    m_impl->onBusy  = std::move(busy);
}

void NpcSystem::SetPlayer(ecs::Entity player) { m_impl->player = player; }
ecs::Entity NpcSystem::Player() const { return m_impl->player; }

bool NpcSystem::Register(const NpcDesc& desc) {
    if (desc.entity.IsNull()) return false;
    Impl::Npc n;
    n.desc = desc;
    // 등록 위치를 origin 으로(Random 배회 기준). Transform 없으면 {0,0}.
    Vec2 pos{0, 0};
    m_impl->PosOf(desc.entity, pos);
    n.origin = pos;
    n.target = pos;
    // 각 NPC 마다 다른 시드(entity.Packed 섞기) — 독립적 무작위.
    n.rng.s = 0x9E3779B9u ^ static_cast<uint32_t>(desc.entity.Packed() * 2654435761u);
    if (n.rng.s == 0) n.rng.s = 1;
    n.waitTimer = desc.waitMin;
    m_impl->npcs[desc.entity.Packed()] = std::move(n);
    return true;
}

void NpcSystem::Unregister(ecs::Entity entity) {
    if (m_impl->interacting == entity) m_impl->interacting = ecs::Entity::Null();
    if (m_impl->move) m_impl->move->Cancel(entity);
    m_impl->npcs.erase(entity.Packed());
}

void NpcSystem::Clear() {
    m_impl->npcs.clear();
    m_impl->interacting = ecs::Entity::Null();
}

size_t NpcSystem::Count() const { return m_impl->npcs.size(); }

ecs::Entity NpcSystem::TryInteractNearest() {
    if (!m_impl->interacting.IsNull()) return ecs::Entity::Null();  // 이미 진행 중
    Vec2 pp{0, 0};
    if (m_impl->player.IsNull() || !m_impl->PosOf(m_impl->player, pp))
        return ecs::Entity::Null();

    ecs::Entity best = ecs::Entity::Null();
    float bestDist = 1e30f;
    for (auto& [key, n] : m_impl->npcs) {
        ecs::Entity e = ecs::Entity::FromPacked(key);
        Vec2 np{0, 0};
        if (!m_impl->PosOf(e, np)) continue;
        float d = (np - pp).Length();
        if (d <= n.desc.interactRadius && d < bestDist) {
            bestDist = d;
            best = e;
        }
    }
    if (best.IsNull()) return ecs::Entity::Null();
    return TryInteract(best) ? best : ecs::Entity::Null();
}

bool NpcSystem::TryInteract(ecs::Entity npc) {
    if (!m_impl->interacting.IsNull()) return false;
    auto it = m_impl->npcs.find(npc.Packed());
    if (it == m_impl->npcs.end()) return false;

    // onBegin(Lua on_interact)은 동기 코루틴 첫 재개를 돌리며, 그 안에서 임의 Lua(mye.npc.unregister/
    //   clear 등)가 이 NPC 를 npcs 에서 제거할 수 있다. 그러면 콜백 이후 it/n 참조가 무효(UAF).
    //   → 콜백 전에 필요한 값(id)만 복사하고, 콜백 후 반드시 재조회(re-lookup)한다.
    const std::string id = it->second.desc.id;   // 콜백 전 값 복사

    // 상호작용 개시 콜백(Lua on_interact). 수락되면 Interacting 으로 잠금·이동 정지.
    bool accepted = m_impl->onBegin ? m_impl->onBegin(id, npc) : false;
    if (!accepted) return false;

    // 콜백이 npcs 를 변형했을 수 있으므로 이터레이터를 재취득한다.
    it = m_impl->npcs.find(npc.Packed());
    if (it == m_impl->npcs.end()) {
        // 콜백이 이 NPC 를 제거함 — 상호작용 개시는 거부 취급(이동 취소만).
        if (m_impl->move) m_impl->move->Cancel(npc);
        return false;
    }
    Impl::Npc& n = it->second;

    if (m_impl->move) m_impl->move->Cancel(npc);
    n.state = NpcState::Interacting;
    m_impl->interacting = npc;

    // 상호작용 시작 순간 플레이어를 바라본다(정지 애니).
    Vec2 np{0, 0}, pp{0, 0};
    if (m_impl->PosOf(npc, np) && !m_impl->player.IsNull() && m_impl->PosOf(m_impl->player, pp)) {
        Vec2 to = pp - np;
        if (to.Length() > 1e-4f) n.facing = anim::Dir8FromVector(to, n.facing);
    }
    m_impl->ApplyAnim(npc, false, n.facing, 0.0f);
    return true;
}

ecs::Entity NpcSystem::InteractingNpc() const { return m_impl->interacting; }

NpcState NpcSystem::State(ecs::Entity npc) const {
    auto it = m_impl->npcs.find(npc.Packed());
    return it == m_impl->npcs.end() ? NpcState::Idle : it->second.state;
}

Vec2 NpcSystem::CurrentWaypoint(ecs::Entity npc) const {
    auto it = m_impl->npcs.find(npc.Packed());
    return it == m_impl->npcs.end() ? Vec2{0, 0} : it->second.target;
}

void NpcSystem::Tick(float dt) {
    Impl& s = *m_impl;

    // 상호작용 종료 폴링(전역 1회) — busy 가 false 면 잠금 해제.
    if (!s.interacting.IsNull()) {
        auto it = s.npcs.find(s.interacting.Packed());
        if (it == s.npcs.end()) {
            s.interacting = ecs::Entity::Null();   // NPC 가 사라짐
        } else {
            const std::string& id = it->second.desc.id;
            bool busy = s.onBusy ? s.onBusy(id, s.interacting) : false;
            if (!busy) {
                s.EndInteraction(it->second, s.interacting);
                s.interacting = ecs::Entity::Null();
            }
        }
    }

    Vec2 pp{0, 0};
    bool hasPlayer = !s.player.IsNull() && s.PosOf(s.player, pp);

    for (auto& [key, n] : s.npcs) {
        ecs::Entity e = ecs::Entity::FromPacked(key);

        // 상호작용 중인 NPC 는 완전 정지(이동/상태전이 없음, 플레이어 주시 유지).
        if (n.state == NpcState::Interacting) {
            s.ApplyAnim(e, false, n.facing, 0.0f);
            continue;
        }

        Vec2 np{0, 0};
        bool hasPos = s.PosOf(e, np);

        // 플레이어 접근 감지 — alertRadius 이내면 정지·주시.
        if (hasPlayer && hasPos) {
            float d = (np - pp).Length();
            if (d <= n.desc.alertRadius) {
                if (n.state == NpcState::Moving && s.move) s.move->Cancel(e);
                n.state = NpcState::AlertPaused;
                if (n.desc.facePlayerOnAlert) {
                    Vec2 to = pp - np;
                    if (to.Length() > 1e-4f) n.facing = anim::Dir8FromVector(to, n.facing);
                }
                s.ApplyAnim(e, false, n.facing, 0.0f);
                continue;
            }
            if (n.state == NpcState::AlertPaused) {
                // 플레이어가 멀어짐 → 배회 재개(대기부터).
                n.state = NpcState::Idle;
                n.waitTimer = n.rng.Range(n.desc.waitMin, n.desc.waitMax);
            }
        } else if (n.state == NpcState::AlertPaused) {
            n.state = NpcState::Idle;
            n.waitTimer = n.desc.waitMin;
        }

        // 배회 상태기계.
        switch (n.state) {
            case NpcState::Idle: {
                s.ApplyAnim(e, false, n.facing, 0.0f);
                n.waitTimer -= dt;
                if (n.waitTimer <= 0.0f && n.desc.mode != WanderMode::None && hasPos) {
                    s.PickNextTarget(n, e, np);
                }
                break;
            }
            case NpcState::Moving: {
                // 이동 방향으로 facing 갱신.
                if (hasPos) {
                    Vec2 to = n.target - np;
                    if (to.Length() > 1e-4f) n.facing = anim::Dir8FromVector(to, n.facing);
                }
                s.ApplyAnim(e, true, n.facing, n.desc.moveSpeed);
                // 도착 판정(MoveController). move 없으면 즉시 도착 취급.
                bool done = s.move ? s.move->IsMoveDone(e) : true;
                if (done) {
                    n.state = NpcState::Idle;
                    n.waitTimer = n.rng.Range(n.desc.waitMin, n.desc.waitMax);
                    s.ApplyAnim(e, false, n.facing, 0.0f);
                }
                break;
            }
            case NpcState::AlertPaused:
                s.ApplyAnim(e, false, n.facing, 0.0f);
                break;
            case NpcState::Interacting:
                break;   // 위에서 처리됨
        }
    }
}

} // namespace mye::runtime
