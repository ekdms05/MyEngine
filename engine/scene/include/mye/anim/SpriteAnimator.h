// mye/anim/SpriteAnimator.h — 스프라이트 애니메이터 컴포넌트·방향세트·상태머신 (docs/03 §6, M3-B)
//
// 데이터 소유:
//   - SpriteAnimator: ECS 컴포넌트. 참조하는 SpriteSheet(프레임 테이블) + 현재 상태/클립/커서 +
//     파라미터 블랙보드 + 현재 방향(Dir8). PhasePostUpdate 의 AnimationSystem 이 샘플링해
//     SpriteRenderer 의 srcUV/pivot/텍스처(sprite ref)를 갱신한다.
//   - DirectionalAnimSet: 논리 클립("walk"/"idle") → 8방향 실제 클립 매핑(+flipX 공유).
//   - AnimStateMachine: 상태(=방향세트 또는 단일 클립) + 전이(파라미터 조건). 데이터 표현.
//
// 소유·수명: SpriteSheet·AnimationClipData 는 AssetManager(04)가 인스턴스화. 본 컴포넌트는
//   비소유 포인터로 참조(M3-C 데모/Lua가 배선). 이렇게 두면 시스템·상태머신을 AssetManager
//   의존 없이 단위 테스트할 수 있다.
#pragma once

#include "mye/anim/AnimationTypes.h"
#include "mye/anim/ClipPlayback.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/asset/AnimationStateAsset.h"
#include "mye/ecs/ComponentType.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::anim {

// ---------------------------------------------------------------------------
// DirectionalAnimSet — 논리 클립 이름 → 8방향 클립. flipX 공유로 우측 계열 접기 가능.
// ---------------------------------------------------------------------------
struct DirectionalAnimSet {
    std::string name;                              // 논리 이름("walk","idle")
    // 방향별 클립 포인터(비소유). Dir8 인덱스 0..7. 대칭 재사용 시 우측 슬롯은 nullptr 허용.
    std::array<const AnimationClipData*, 8> clips{};
    bool mirrorRight = false;                       // true=우측 3방향을 좌측+flipX로 재사용

    // 방향 d 에 대해 실제 재생할 클립과 flipX 를 해석. 우측 슬롯이 비어도 mirrorRight로 폴백.
    struct Resolved { const AnimationClipData* clip = nullptr; bool flipX = false; };
    Resolved Resolve(Dir8 d) const {
        const size_t i = static_cast<size_t>(d);
        if (i >= clips.size()) return {};
        // 우선 직접 슬롯이 있으면 그대로(flip 없음).
        if (clips[i]) return { clips[i], false };
        if (mirrorRight) {
            DirResolve r = ResolveDir(d, true);
            const size_t j = static_cast<size_t>(r.clipDir);
            if (clips[j]) return { clips[j], r.flipX };
        }
        // 폴백: 직접 슬롯.
        return { clips[i], false };
    }
};

// ---------------------------------------------------------------------------
// 상태 머신 — 상태=방향세트(또는 단일 클립), 전이=파라미터 조건. 데이터로 표현.
// ---------------------------------------------------------------------------
// 파라미터 블랙보드: 이름→값. bool/float 통합(bool은 0/1). trigger 는 소모형 bool.
using asset::ParamType;
using asset::AnimParam;
using asset::CmpOp;
using asset::AnimCondition;
using asset::AnimTransition;

// Runtime states borrow directional or single clips from their owner.
struct AnimState {
    std::string        name;
    bool               directional = true;
    DirectionalAnimSet dirSet;                 // directional=true
    const AnimationClipData* singleClip = nullptr;   // directional=false
};

// 상태 머신 정의(데이터). 상태 목록 + 전이 목록 + 초기 상태.
struct AnimStateMachine {
    std::vector<AnimState>      states;
    std::vector<AnimTransition> transitions;
    int                         initialState = 0;
};

// ---------------------------------------------------------------------------
// SpriteAnimator — ECS 컴포넌트. 상태머신 인스턴스 + 파라미터 + 방향 + 재생 커서.
// ---------------------------------------------------------------------------
struct SpriteAnimator {
    MYE_COMPONENT(SpriteAnimator);

    asset::AssetRef animation; // Persistent clip asset; runtime pointers are rebound after loading.
    asset::AssetGuid requestedAnimation; // Runtime entry request; a successor must not restart it.

    // 상태 머신 정의(비소유). nullptr 이면 singleClip 직접 재생 모드로 동작.
    const AnimStateMachine* machine = nullptr;

    // 프레임 테이블(비소유). 샘플링 시 frames[idx].rect/uv/pivot 을 렌더러로 옮긴다.
    const asset::SpriteSheet* sheet = nullptr;

    // machine 없이 단일 클립을 바로 재생하고 싶을 때(테스트·간이 사용).
    const AnimationClipData* directClip = nullptr;
    const asset::AnimationAsset* sourceAnimation = nullptr; // Non-owning saved directional data.

    // 파라미터 블랙보드(전이 조건 평가용). 이름으로 조회.
    std::vector<AnimParam> params;

    // ---- 런타임 상태 ----
    int        currentState = -1;      // machine->states 인덱스. -1 = 미초기화
    Dir8       facing = Dir8::Down;    // 현재 8방향
    Dir8       playbackFacing = Dir8::Down; // Facing whose clip owns the fixed-tick cursor.
    ClipCursor cursor;                 // 현재 클립 재생 커서
    float      speed = 1.0f;           // 재생 속도 배수(dt 스케일)
    bool       flipX = false;          // 방향 대칭 재생 시 렌더러로 전달할 flipX
    bool       playing = true;

    // 현재 프레임 결과(샘플링 산출). 시스템이 SpriteRenderer 로 옮긴다.
    uint32_t   currentFrameIndex = 0;  // clip.frameIndices 가 가리키는 SpriteSheet 프레임 인덱스
    bool       started = false;        // 첫 진입 이벤트 발행 여부

    // ---- 파라미터 헬퍼 ----
    AnimParam* FindParam(const std::string& n) {
        for (auto& p : params) if (p.name == n) return &p;
        return nullptr;
    }
    const AnimParam* FindParam(const std::string& n) const {
        for (auto& p : params) if (p.name == n) return &p;
        return nullptr;
    }
    void SetBool(const std::string& n, bool v)  { EnsureParam(n, ParamType::Bool).value = v ? 1.0f : 0.0f; }
    void SetFloat(const std::string& n, float v){ EnsureParam(n, ParamType::Float).value = v; }
    void SetTrigger(const std::string& n)       { EnsureParam(n, ParamType::Trigger).value = 1.0f; }
    float GetFloat(const std::string& n) const  { auto* p = FindParam(n); return p ? p->value : 0.0f; }
    bool  GetBool(const std::string& n) const   { auto* p = FindParam(n); return p && p->value != 0.0f; }

    AnimParam& EnsureParam(const std::string& n, ParamType t) {
        if (auto* p = FindParam(n)) return *p;
        params.push_back(AnimParam{ n, t, 0.0f });
        return params.back();
    }
};

inline void RequestAnimation(SpriteAnimator& animator, const asset::AssetRef& animation) {
    if (!animation.guid.IsValid() || animator.requestedAnimation == animation.guid) return;
    animator.requestedAnimation = animation.guid;
    animator.animation = animation;
    animator.sheet = nullptr;
    animator.directClip = nullptr;
    animator.sourceAnimation = nullptr;
    animator.cursor = {};
    animator.started = false;
}

// 현재 애니메이터가 재생 중인 클립을 해석(상태머신/방향세트/단일클립 우선순위).
// 반환 clip 이 null 이면 재생할 것이 없음(샘플링 no-op).
struct ResolvedClip { const AnimationClipData* clip = nullptr; bool flipX = false; };
inline ResolvedClip ResolveActiveClip(const SpriteAnimator& a, Dir8 facing) {
    if (a.machine && a.currentState >= 0 &&
        a.currentState < static_cast<int>(a.machine->states.size())) {
        const AnimState& st = a.machine->states[static_cast<size_t>(a.currentState)];
        if (st.directional) {
            auto r = st.dirSet.Resolve(facing);
            return { r.clip, r.flipX };
        }
        return { st.singleClip, false };
    }
    if (a.directClip && a.sourceAnimation) {
        const auto resolved = a.sourceAnimation->Resolve(facing);
        return {resolved.clip, resolved.flipX};
    }
    if (a.directClip) return { a.directClip, false };
    return { nullptr, false };
}

inline ResolvedClip ResolveActiveClip(const SpriteAnimator& animator) {
    return ResolveActiveClip(animator, animator.facing);
}

// A render may show a new facing before a tick commits it; no events or playback state change here.
inline ClipCursor CursorForFacing(const SpriteAnimator& animator, const ResolvedClip& target) {
    if (!animator.started || animator.playbackFacing == animator.facing || !target.clip) return animator.cursor;
    const auto previous = ResolveActiveClip(animator, animator.playbackFacing);
    if (!previous.clip || previous.clip == target.clip) return animator.cursor;
    return RemapClipCursor(*previous.clip, *target.clip, animator.cursor);
}

} // namespace mye::anim
