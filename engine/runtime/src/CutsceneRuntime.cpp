// mye/runtime/CutsceneRuntime.cpp — 컷신 프리미티브 백엔드 골격 (M6-A)
//
// 골격 범위: MoveController(직선 이동 완료 판정)·CameraFocusController(lerp 완료 판정) 최소 동작
//   + CutsceneRuntime 허브 배선. nav 경로 추종·Transform 실쓰기는 구현 에이전트가 World/nav 로.
#include "mye/runtime/CutsceneRuntime.h"

#include "mye/runtime/DialogueSystem.h"

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

#include <unordered_map>

namespace mye::runtime {

// ---- MoveController --------------------------------------------------------
struct MoveController::Impl {
    ecs::World*      world = nullptr;
    nav::NavSystem*  nav = nullptr;

    struct Cmd { Vec2 target; float speed; MoveStatus status; };
    std::unordered_map<uint64_t, Cmd> cmds;   // entity.Packed() → 명령
};

MoveController::MoveController() : m_impl(std::make_unique<Impl>()) {}
MoveController::~MoveController() = default;

void MoveController::Initialize(ecs::World* world, nav::NavSystem* nav) {
    m_impl->world = world;
    m_impl->nav = nav;
}
void MoveController::Shutdown() { m_impl->cmds.clear(); m_impl->world = nullptr; m_impl->nav = nullptr; }

void MoveController::MoveTo(ecs::Entity entity, Vec2 targetWorld, float speed) {
    // nav 경로 추종은 후속(NavSource·grid 변환 배선 필요) — M6-A 는 직선 이동으로 Transform 전진.
    //   speed<=0 이면 즉시 도착 처리(연출 스냅).
    m_impl->cmds[entity.Packed()] = Impl::Cmd{targetWorld, speed, MoveStatus::Moving};
}

void MoveController::Cancel(ecs::Entity entity) {
    auto it = m_impl->cmds.find(entity.Packed());
    if (it != m_impl->cmds.end()) it->second.status = MoveStatus::Failed;
}

MoveStatus MoveController::Status(ecs::Entity entity) const {
    auto it = m_impl->cmds.find(entity.Packed());
    return it == m_impl->cmds.end() ? MoveStatus::Idle : it->second.status;
}

bool MoveController::IsMoveDone(ecs::Entity entity) const {
    MoveStatus s = Status(entity);
    return s == MoveStatus::Arrived || s == MoveStatus::Failed || s == MoveStatus::Idle;
}

void MoveController::Update(float dt) {
    // 각 활성 명령의 LocalTransform.position(XY)을 목표로 speed*dt 전진. 도달 시 Arrived.
    //   좌표: 월드 +Y 업(Vec2 는 XY 평면), Transform 은 3D 이므로 Z 는 보존한다.
    constexpr float kArriveEps = 1e-3f;
    for (auto& [key, cmd] : m_impl->cmds) {
        if (cmd.status != MoveStatus::Moving) continue;

        ecs::Entity e = ecs::Entity::FromPacked(key);
        scene::LocalTransform* tf =
            m_impl->world ? m_impl->world->TryGet<scene::LocalTransform>(e) : nullptr;
        if (!tf) {
            // Transform 없으면 이동 불가 — 완료(연출 진행은 막지 않는다).
            cmd.status = MoveStatus::Failed;
            continue;
        }

        Vec2 cur{tf->position.x, tf->position.y};
        Vec2 delta = cmd.target - cur;
        float dist = delta.Length();
        float step = (cmd.speed <= 0.0f) ? dist : cmd.speed * dt;   // speed<=0 → 즉시 스냅

        if (dist <= step || dist < kArriveEps) {
            tf->position.x = cmd.target.x;
            tf->position.y = cmd.target.y;
            cmd.status = MoveStatus::Arrived;
        } else {
            Vec2 next = cur + delta * (step / dist);
            tf->position.x = next.x;
            tf->position.y = next.y;
        }
        tf->dirty = true;   // TransformSystem 증분 갱신 트리거.
    }
}

// ---- CameraFocusController -------------------------------------------------
struct CameraFocusController::Impl {
    ApplyFocusFn apply;
    Vec2         current{};
    Vec2         target{};
    float        lerpSpeed = 0.0f;
    ecs::Entity  follow{};
    bool         hasFollow = false;
    bool         done = true;
};

CameraFocusController::CameraFocusController() : m_impl(std::make_unique<Impl>()) {}
CameraFocusController::~CameraFocusController() = default;

void CameraFocusController::Initialize(ApplyFocusFn apply, Vec2 initialFocus) {
    m_impl->apply = std::move(apply);
    m_impl->current = initialFocus;
    m_impl->target = initialFocus;
    m_impl->done = true;
}

void CameraFocusController::FocusWorld(Vec2 worldPos, float lerpSpeed) {
    m_impl->target = worldPos;
    m_impl->lerpSpeed = lerpSpeed;
    m_impl->hasFollow = false;
    m_impl->done = false;
}

void CameraFocusController::FocusEntity(ecs::Entity target, float lerpSpeed) {
    m_impl->follow = target;
    m_impl->hasFollow = true;
    m_impl->lerpSpeed = lerpSpeed;
    m_impl->done = false;
}

void CameraFocusController::ClearFollow() { m_impl->hasFollow = false; }

Vec2 CameraFocusController::CurrentFocus() const { return m_impl->current; }
bool CameraFocusController::IsFocusDone() const { return m_impl->done; }

void CameraFocusController::Update(float dt, const ecs::World* world) {
    // 엔티티 추종 시 world 에서 follow 의 Transform XY 를 목표로 갱신(const World → Dynamic 경로).
    if (m_impl->hasFollow && world) {
        const void* p = world->TryGetDynamic(m_impl->follow,
                                             scene::LocalTransform::kComponentTypeId);
        if (p) {
            const auto* tf = static_cast<const scene::LocalTransform*>(p);
            m_impl->target = Vec2{tf->position.x, tf->position.y};
        }
    }
    Vec2 to = m_impl->target;
    if (m_impl->lerpSpeed <= 0.0f) {
        m_impl->current = to;
        m_impl->done = true;
    } else {
        Vec2 delta = to - m_impl->current;
        float dist = delta.Length();
        float step = m_impl->lerpSpeed * dt;
        if (dist <= step || dist < 1e-3f) {
            m_impl->current = to;
            m_impl->done = !m_impl->hasFollow;   // 추종이면 계속, 고정 포커스면 완료
        } else {
            m_impl->current += delta * (step / dist);
        }
    }
    if (m_impl->apply) m_impl->apply(m_impl->current);
}

// ---- CutsceneRuntime -------------------------------------------------------
struct CutsceneRuntime::Impl {
    DialogueSystem*        dialogue = nullptr;
    MoveController*        move = nullptr;
    CameraFocusController* camera = nullptr;
};

CutsceneRuntime::CutsceneRuntime() : m_impl(std::make_unique<Impl>()) {}
CutsceneRuntime::~CutsceneRuntime() = default;

void CutsceneRuntime::Initialize(DialogueSystem* dialogue, MoveController* move,
                                 CameraFocusController* camera) {
    m_impl->dialogue = dialogue;
    m_impl->move = move;
    m_impl->camera = camera;
}
void CutsceneRuntime::Shutdown() {
    m_impl->dialogue = nullptr;
    m_impl->move = nullptr;
    m_impl->camera = nullptr;
}

DialogueSystem*        CutsceneRuntime::Dialogue() const { return m_impl->dialogue; }
MoveController*        CutsceneRuntime::Move() const { return m_impl->move; }
CameraFocusController* CutsceneRuntime::Camera() const { return m_impl->camera; }

void CutsceneRuntime::UpdateSim(float dtSim) {
    if (m_impl->move) m_impl->move->Update(dtSim);
}
void CutsceneRuntime::UpdateView(float dtVar, const ecs::World* world) {
    if (m_impl->camera) m_impl->camera->Update(dtVar, world);
}

} // namespace mye::runtime
