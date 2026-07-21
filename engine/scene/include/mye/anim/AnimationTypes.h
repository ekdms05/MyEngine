// mye/anim/AnimationTypes.h — 8방향 규약·방향 매핑·애니 이벤트 (docs/03 §6, M3-B)
//
// M3-A(SpriteSheet.h)가 만든 데이터(AnimationClipData·SpriteFrame·AnimEventMarker)를 소비한다.
// 본 헤더는 8방향 enum·방향벡터→인덱스 매핑·flipX 공유 규약·월드 버스 이벤트 타입을 정의한다.
//
// 8방향 규약(작업 확정): 태그 walk_<dir>/idle_<dir>, dir 인덱스 0..7 = down 부터 시계 방향:
//   0 down, 1 down_left, 2 left, 3 up_left, 4 up, 5 up_right, 6 right, 7 down_right.
// 좌표(01): 왼손·+Y 업. 입력 방향벡터(월드, +Y=위)를 가장 가까운 8방향 인덱스로 스냅한다.
#pragma once

#include "mye/core/Base.h"      // MYE_EVENT, EventTypeId
#include "mye/core/Math.h"
#include "mye/ecs/Entity.h"

#include <array>
#include <cstdint>
#include <string>

namespace mye::anim {

// 8방향 인덱스(작업 규약: down=0, 시계 방향). docs/03 Facing8과 동일 집합이나 인덱스 정본은 이쪽.
enum class Dir8 : uint8_t {
    Down = 0, DownLeft = 1, Left = 2, UpLeft = 3,
    Up = 4, UpRight = 5, Right = 6, DownRight = 7,
    Count = 8,
};

// 방향별 접미사("down","down_left",...). 태그 이름 조립(walk_<suffix>)에 사용.
inline const char* Dir8Suffix(Dir8 d) {
    switch (d) {
        case Dir8::Down:      return "down";
        case Dir8::DownLeft:  return "down_left";
        case Dir8::Left:      return "left";
        case Dir8::UpLeft:    return "up_left";
        case Dir8::Up:        return "up";
        case Dir8::UpRight:   return "up_right";
        case Dir8::Right:     return "right";
        case Dir8::DownRight: return "down_right";
        default:              return "down";
    }
}

// 각 방향의 단위 벡터(월드, +Y 업). 대각은 정규화된 성분(1/√2 ≈ 0.70710677).
inline Vec2 Dir8Vector(Dir8 d) {
    constexpr float k = 0.70710677f;
    switch (d) {
        case Dir8::Down:      return { 0.0f, -1.0f };
        case Dir8::DownLeft:  return { -k,   -k   };
        case Dir8::Left:      return { -1.0f, 0.0f };
        case Dir8::UpLeft:    return { -k,    k   };
        case Dir8::Up:        return { 0.0f,  1.0f };
        case Dir8::UpRight:   return {  k,    k   };
        case Dir8::Right:     return { 1.0f,  0.0f };
        case Dir8::DownRight: return {  k,   -k   };
        default:              return { 0.0f, -1.0f };
    }
}

// 입력 방향벡터 → 가장 가까운 8방향 인덱스. 영벡터는 fallback(기본 Down)을 반환.
// atan2 기반: 각도를 45° 섹터로 스냅. +Y=위, +X=오른쪽. down(-Y)=0, 시계 방향으로 증가.
inline Dir8 Dir8FromVector(Vec2 v, Dir8 fallback = Dir8::Down) {
    const float lenSq = v.x * v.x + v.y * v.y;
    if (lenSq < 1e-12f) return fallback;
    // 우리 규약의 각도: down 을 0으로, 시계 방향(down→down_left→left→up_left→up...)으로 증가.
    // +Y=위 좌표에서 이 시계 순서는 -Y(down)에서 -X(left) 쪽으로 도는 것 = atan2(-x, -y):
    //   down(0,-1)→0, down_left(-1,-1)→+45°, left(-1,0)→+90°, up(0,1)→±180°, right(1,0)→-90°(→+270°).
    float ang = std::atan2(-v.x, -v.y);         // [-π, π]
    if (ang < 0.0f) ang += 6.2831853f;          // [0, 2π)
    // 45° 섹터로 반올림(0.5 섹터 오프셋으로 경계 스냅).
    const float sector = ang / 0.7853981634f;   // /45°
    int idx = static_cast<int>(sector + 0.5f) & 7;
    return static_cast<Dir8>(idx);
}

// 좌우 대칭 flipX 공유: 우측 계열(right/up_right/down_right)을 좌측 클립+flipX로 재사용한다.
// 반환 = { 실제로 재생할 방향(좌측 계열로 매핑), flipX }. 대칭 없이 8클립을 다 쓰면 flipX=false.
struct DirResolve {
    Dir8 clipDir = Dir8::Down;   // 실제 클립 태그에 쓸 방향
    bool flipX = false;          // 좌우 반전 필요 여부
};

// mirrorRight=true 이면 우측 3방향을 좌측 클립+flipX로 접는다(아트 절약, 절반 재사용).
inline DirResolve ResolveDir(Dir8 d, bool mirrorRight) {
    if (!mirrorRight) return { d, false };
    switch (d) {
        case Dir8::Right:     return { Dir8::Left,     true };
        case Dir8::UpRight:   return { Dir8::UpLeft,   true };
        case Dir8::DownRight: return { Dir8::DownLeft, true };
        default:              return { d, false };
    }
}

// ---------------------------------------------------------------------------
// 애니메이션 이벤트 — 월드-로컬 EventBus로 발행(docs/03 §2 이원화, §6). 06(오디오)·05(Lua) 구독.
// ---------------------------------------------------------------------------
// footstep(발소리)·attack_hit(타격 판정) 등. name/stringArg/floatArg 는 AnimEventMarker에서 전달.
struct AnimationEvent {
    MYE_EVENT(mye::anim::AnimationEvent);

    ecs::Entity entity;          // 이벤트를 낸 애니메이터 엔티티
    const char* name = nullptr;  // 마커 이름("footstep","attack_hit"). 문자열 수명=클립 데이터.
    const char* stringArg = nullptr;
    float       floatArg = 0.0f;
    uint32_t    frameIndex = 0;  // 클립 내 프레임 인덱스(마커 위치)
};

} // namespace mye::anim
