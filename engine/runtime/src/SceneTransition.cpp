// mye/runtime/SceneTransition.cpp — 씬 전환 상태기계 골격 (M6-A)
//
// 골격 범위: 페이드아웃→로딩→액티베이트→페이드인 상태기계와 페이드 알파·진행률 관리는 동작한다.
//   실제 로드는 SceneLoaderFn 콜백 위임(구현 에이전트 배선). 페이드 렌더·로딩 UiDocument 표시는
//   표현 계층(UiRenderer)에서 FadeAlpha/LoadProgress 를 읽어 그린다.
#include "mye/runtime/SceneTransition.h"

#include "mye/core/Events.h"
#include "mye/core/Math.h"

namespace mye::runtime {

struct SceneTransitionManager::Impl {
    ui::UiSystem* ui = nullptr;
    ui::UiCanvas* overlay = nullptr;
    SceneLoaderFn loader;
    EventBus*     events = nullptr;

    TransitionPhase phase = TransitionPhase::Idle;
    TransitionDesc  desc;
    SceneRef        scene;
    SceneLoadTicket ticket;
    float           fadeAlpha = 0.0f;
    float           progress = 0.0f;
    float           timer = 0.0f;

    void Publish(bool ok) {
        if (events) { SceneChangedEvent e{ok}; events->Publish(e); }
    }
    void Finish(bool ok) {
        phase = TransitionPhase::Idle;
        fadeAlpha = 0.0f;
        progress = 0.0f;
        Publish(ok);
    }
};

SceneTransitionManager::SceneTransitionManager() : m_impl(std::make_unique<Impl>()) {}
SceneTransitionManager::~SceneTransitionManager() = default;

void SceneTransitionManager::Initialize(ui::UiSystem* ui, ui::UiCanvas* overlayCanvas,
                                        SceneLoaderFn loader, EventBus* events) {
    m_impl->ui = ui;
    m_impl->overlay = overlayCanvas;
    m_impl->loader = std::move(loader);
    m_impl->events = events;
}
void SceneTransitionManager::Shutdown() {
    m_impl->phase = TransitionPhase::Idle;
    m_impl->ui = nullptr;
    m_impl->overlay = nullptr;
    m_impl->events = nullptr;
}

Expected<void, Error> SceneTransitionManager::ChangeScene(const SceneRef& scene,
                                                          const TransitionDesc& desc) {
    if (m_impl->phase != TransitionPhase::Idle)
        return MakeError(RuntimeError::Busy, "씬 전환이 이미 진행 중");
    if (!scene.IsValid())
        return MakeError(RuntimeError::InvalidArg, "빈 SceneRef");

    m_impl->scene = scene;
    m_impl->desc = desc;
    m_impl->progress = 0.0f;
    m_impl->timer = 0.0f;
    m_impl->fadeAlpha = 0.0f;
    m_impl->phase = TransitionPhase::FadeOut;
    return Expected<void, Error>{};
}

TransitionPhase SceneTransitionManager::Phase() const { return m_impl->phase; }
bool  SceneTransitionManager::IsTransitioning() const { return m_impl->phase != TransitionPhase::Idle; }
float SceneTransitionManager::FadeAlpha() const { return m_impl->fadeAlpha; }
float SceneTransitionManager::LoadProgress() const { return m_impl->progress; }
bool  SceneTransitionManager::ConsumesInput() const { return m_impl->phase != TransitionPhase::Idle; }

void SceneTransitionManager::Update(float dt) {
    Impl& s = *m_impl;
    switch (s.phase) {
        case TransitionPhase::Idle:
            return;

        case TransitionPhase::FadeOut: {
            s.timer += dt;
            float d = s.desc.fadeOutSec > 0.0f ? s.desc.fadeOutSec : 0.0001f;
            s.fadeAlpha = Saturate(s.timer / d);
            if (s.fadeAlpha >= 1.0f) {
                s.fadeAlpha = 1.0f;
                s.timer = 0.0f;
                // 로드 시작.
                if (s.loader.IsBound()) {
                    s.ticket = s.loader.begin(s.scene);
                    s.phase = TransitionPhase::Loading;
                } else {
                    // 로더 미배선: 즉시 액티베이트 단계로(골격 검증 경로).
                    s.phase = TransitionPhase::Activating;
                }
            }
            break;
        }

        case TransitionPhase::Loading: {
            bool done = false;
            s.progress = Saturate(s.loader.poll(s.ticket, done));
            if (done) {
                s.progress = 1.0f;
                s.phase = TransitionPhase::Activating;
            }
            break;
        }

        case TransitionPhase::Activating: {
            bool ok = true;
            if (s.loader.IsBound()) {
                auto r = s.loader.activate(s.ticket);
                ok = static_cast<bool>(r);
            }
            if (!ok) { s.Finish(false); return; }
            s.timer = 0.0f;
            s.phase = TransitionPhase::FadeIn;
            break;
        }

        case TransitionPhase::FadeIn: {
            s.timer += dt;
            float d = s.desc.fadeInSec > 0.0f ? s.desc.fadeInSec : 0.0001f;
            s.fadeAlpha = 1.0f - Saturate(s.timer / d);
            if (s.fadeAlpha <= 0.0f) {
                s.fadeAlpha = 0.0f;
                s.Finish(true);
            }
            break;
        }
    }
}

} // namespace mye::runtime
