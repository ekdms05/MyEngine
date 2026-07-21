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
#include "mye/asset/SpriteSheet.h"
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
        // 우선 직접 슬롯이 있으면 그대로(flip 없음).
        if (!mirrorRight && clips[i]) return { clips[i], false };
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
enum class ParamType : uint8_t { Bool, Float, Trigger };

struct AnimParam {
    std::string name;
    ParamType   type = ParamType::Float;
    float       value = 0.0f;   // bool/trigger: 0/1
};

// 전이 조건 연산자.
enum class CmpOp : uint8_t { Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual, IsTrue, IsFalse };

struct AnimCondition {
    std::string param;          // 참조 파라미터 이름
    CmpOp       op = CmpOp::IsTrue;
    float       threshold = 0.0f;
};

// 전이: from 상태에서 조건들이 모두 참이면 to 상태로. clipFinished=true 면 "현재 클립 종료 시".
struct AnimTransition {
    int  from = -1;             // -1 = any-state
    int  to = 0;
    std::vector<AnimCondition> conditions;
    bool onClipFinished = false;   // 비루프 클립이 끝났을 때만 유효
    std::vector<std::string> consumeTriggers;   // 전이 시 소모할 trigger 파라미터
};

// 상태: 방향세트(8방향) 또는 단일 클립. directional=false 면 singleClip 사용.
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

    // 상태 머신 정의(비소유). nullptr 이면 singleClip 직접 재생 모드로 동작.
    const AnimStateMachine* machine = nullptr;

    // 프레임 테이블(비소유). 샘플링 시 frames[idx].rect/uv/pivot 을 렌더러로 옮긴다.
    const asset::SpriteSheet* sheet = nullptr;

    // machine 없이 단일 클립을 바로 재생하고 싶을 때(테스트·간이 사용).
    const AnimationClipData* directClip = nullptr;

    // 파라미터 블랙보드(전이 조건 평가용). 이름으로 조회.
    std::vector<AnimParam> params;

    // ---- 런타임 상태 ----
    int        currentState = -1;      // machine->states 인덱스. -1 = 미초기화
    Dir8       facing = Dir8::Down;    // 현재 8방향
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

// 현재 애니메이터가 재생 중인 클립을 해석(상태머신/방향세트/단일클립 우선순위).
// 반환 clip 이 null 이면 재생할 것이 없음(샘플링 no-op).
struct ResolvedClip { const AnimationClipData* clip = nullptr; bool flipX = false; };
inline ResolvedClip ResolveActiveClip(const SpriteAnimator& a) {
    if (a.machine && a.currentState >= 0 &&
        a.currentState < static_cast<int>(a.machine->states.size())) {
        const AnimState& st = a.machine->states[static_cast<size_t>(a.currentState)];
        if (st.directional) {
            auto r = st.dirSet.Resolve(a.facing);
            return { r.clip, r.flipX };
        }
        return { st.singleClip, false };
    }
    if (a.directClip) return { a.directClip, false };
    return { nullptr, false };
}

} // namespace mye::anim
