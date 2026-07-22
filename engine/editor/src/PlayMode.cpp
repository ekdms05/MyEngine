// PlayMode.cpp — 에디트↔플레이 모드 컨트롤러 (docs/07 §3)
//
// 07 §3: Play=편집 World를 인메모리 스냅샷(04 직렬화)으로 떠서 별도 Play World로 역직렬화,
//   Stop=Play World 파기 + 편집 World 재표시(스냅샷 복원이 아니라 단순 파기). 편집 World는
//   Play 내내 보존된다. 플레이 중 편집은 Play World에만 적용되고 Stop 시 휘발(별도 Undo 스택도
//   통째로 파기). Pause/Resume/StepFrame 상태 전이. ActiveWorld가 표시 대상 라우팅의 정본.
#include "mye/editor/PlayMode.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/SceneSerializer.h"
#include "mye/core/Events.h"
#include "mye/ecs/ComponentPool.h"
#include "mye/ecs/World.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeRegistry.h"

#include <memory>
#include <string>

namespace mye::editor {

struct PlayModeController::Impl {
    std::unique_ptr<ecs::World>   playWorld;      // Playing/Paused 동안만 유효.
    std::unique_ptr<CommandStack> playCommands;   // 플레이 중 Undo(Stop 시 파기).
};

PlayModeController::PlayModeController() : m_impl(std::make_unique<Impl>()) {}
PlayModeController::~PlayModeController() = default;

static void PublishStateChange(EventBus* events, PlayState prev, PlayState cur) {
    if (!events || prev == cur) return;
    PlayStateChangedEvent ev;
    ev.previous = prev;
    ev.current = cur;
    events->Publish(ev);
}

// Play World에 편집 World와 동일한 컴포넌트 풀을 미리 등록한다.
//
//   SceneSerializer::ReadInto는 World::AddDynamic으로 컴포넌트를 붙이는데, AddDynamic은 풀이
//   미등록이면 nullptr을 반환한다(자동 등록 안 함). 새로 만든 Play World에는 풀이 하나도 없으므로,
//   역직렬화 전에 편집 World의 실제 ComponentTypeDesc(생성/파괴/이동 훅 포함)를 그대로 복제
//   등록해 둔다. 열거는 리플렉션 레지스트리의 Struct 타입 중 편집 World에 등록된 것을 기준으로
//   하며(SceneSerializer의 컴포넌트 선별 규약과 동일), 실제 desc는 편집 World 풀에서 가져와
//   훅 무결성을 보존한다.
static void MirrorComponentPools(const ecs::World& src, ecs::World& dst) {
    for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
        if (!t || t->GetKind() != refl::Kind::Struct) continue;
        const auto cid = static_cast<ecs::ComponentTypeId>(refl::TypeIdFromName(t->Name()));
        if (!src.IsRegistered(cid)) continue;
        if (dst.IsRegistered(cid)) continue;
        if (const ecs::ComponentPool* pool = src.Pool(cid))
            dst.RegisterComponent(pool->Desc());   // 실제 훅 포함 desc 복제.
    }
}

void PlayModeController::Play() {
    if (m_state != PlayState::Edit) {
        if (m_state == PlayState::Paused) Resume();
        return;   // 이미 Playing이면 no-op.
    }
    const PlayState prev = m_state;

    // 07 §3: 편집 World를 인메모리 스냅샷으로 직렬화 → 새 World로 역직렬화.
    m_impl->playWorld = std::make_unique<ecs::World>();
    if (m_editWorld) {
        // 역직렬화 전 컴포넌트 풀 미러링(AddDynamic이 풀 없으면 no-op이므로 필수).
        MirrorComponentPools(*m_editWorld, *m_impl->playWorld);
        SceneSerializer ser;
        auto snap = ser.Snapshot(*m_editWorld);
        if (snap) {
            auto r = ser.Restore(*m_impl->playWorld, snap.Value());
            (void)r;   // 실패 시 빈 Play World(엔진 로그는 상위 계층). 상태 전이는 계속.
        }
        // Play World도 편집 World와 동일한 월드-로컬 이벤트 버스 배선을 잇는다(게임플레이 이벤트).
        m_impl->playWorld->SetEventBus(m_editWorld->Events());
    }

    // 플레이 전용 Undo 스택(Stop 시 파기). 문서 스택과 분리(07 §3).
    m_impl->playCommands = std::make_unique<CommandStack>();

    m_state = PlayState::Playing;
    PublishStateChange(m_events, prev, m_state);
}

void PlayModeController::Pause() {
    if (m_state != PlayState::Playing) return;
    const PlayState prev = m_state;
    m_state = PlayState::Paused;
    PublishStateChange(m_events, prev, m_state);
}

void PlayModeController::Resume() {
    if (m_state != PlayState::Paused) return;
    const PlayState prev = m_state;
    m_state = PlayState::Playing;
    PublishStateChange(m_events, prev, m_state);
}

void PlayModeController::StepFrame() {
    // 07: F10 — Paused에서만 1프레임 진행. 실제 Play World tick(스크립트·물리·AI 고정 스텝)은
    //   런타임 시스템 스케줄러 배선(M4-B 뷰포트/게임 루프 통합)에서 이 훅을 소비한다. 여기서는
    //   상태 계약만 지킨다(Paused 유지). tick 게이팅은 EditorModule의 PreRender 틱이 State()를
    //   보고 결정한다.
    if (m_state != PlayState::Paused) return;
    // no-op placeholder: 1프레임 진행 요청 플래그는 상위 루프가 State()==Paused + 요청으로 처리.
}

void PlayModeController::Stop() {
    if (m_state == PlayState::Edit) return;
    const PlayState prev = m_state;

    // 07 §3: Play World 파기 + 플레이 Undo 스택 파기 → 편집 World 재표시. 스냅샷 복원 아님.
    m_impl->playWorld.reset();
    if (m_impl->playCommands) m_impl->playCommands->Clear();
    m_impl->playCommands.reset();

    m_state = PlayState::Edit;
    PublishStateChange(m_events, prev, m_state);
}

ecs::World* PlayModeController::ActiveWorld() const {
    // 07 §3: Edit=편집 World, Playing/Paused=Play World.
    if (m_state == PlayState::Edit) return m_editWorld;
    return m_impl->playWorld ? m_impl->playWorld.get() : m_editWorld;
}

CommandStack* PlayModeController::PlayCommandStack() const {
    return m_impl->playCommands.get();
}

} // namespace mye::editor
