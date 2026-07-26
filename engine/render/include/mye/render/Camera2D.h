// mye/render/Camera2D.h — Screen2D 직교 카메라 (M1-B 구현) (docs/02 §카메라)
//
// 책임: Screen2D 프리셋(직교 정면, XY 평면)의 뷰·프로젝션 행렬 산출과
// Screen↔World 변환(정본). PixelPerfect 모드에서 픽셀 스냅 카메라(카메라 위치를
// 1/PPU 그리드로 스냅, 버린 서브픽셀 잔여는 업스케일 UV 오프셋으로 환원)를 담당한다.
// 좌표 규약(02): PPU 48, 내부 RT 960×540, 월드 XY평면 +Y 위, 스크린 좌상단 +Y 아래,
// NDC 깊이 0~1(LH). 행렬 규약(01): 저장 row-major, 곱셈 row-vector(v*M).
#pragma once

#include "mye/core/Math.h"

namespace mye::render {

// 02 확정 규약 상수(정본은 02). 렌더 모듈이 참조하도록 재노출.
inline constexpr float    kPixelsPerUnit = 48.0f;
inline constexpr uint32_t kInternalWidth  = 960;
inline constexpr uint32_t kInternalHeight = 540;

struct Camera2DDesc {
    Vec2  position{};                 // 월드 중심(unit)
    float orthoHeightUnits = kInternalHeight / kPixelsPerUnit;  // 세로 뷰 크기(unit)
    float zoom = 1.0f;
    bool  pixelSnap = true;           // PixelPerfect 모드 픽셀 스냅 카메라
    // 내부 RT 픽셀 크기(뷰포트). 픽셀 스냅·서브픽셀 잔차·Screen↔World가 이 크기를 기준으로 계산된다.
    uint32_t viewportWidth  = kInternalWidth;
    uint32_t viewportHeight = kInternalHeight;
};

// Screen2D 직교 카메라. 구현 M1-B.
//
// 픽셀 스냅 모델: 논리 카메라 중심(m_desc.position)을 내부 RT 픽셀 격자(1/PPU·1/zoom 단위)에
// 스냅한 위치를 뷰 행렬에 사용한다(크리스프 유지). 스냅으로 버린 잔차(px, 화면 방향)는
// SubpixelResidual()로 노출되어 업스케일 블릿 UV 오프셋으로 환원된다(부드러운 스크롤).
class Camera2D {
public:
    Camera2D() = default;
    explicit Camera2D(const Camera2DDesc& desc);

    void SetPosition(Vec2 worldPos);
    void SetZoom(float zoom);
    void SetViewportSize(uint32_t w, uint32_t h);
    void SetPixelSnap(bool enabled);

    // 타겟 추적: 논리 카메라 중심을 target으로 lerp(smoothing=0 즉시). 매 프레임 호출.
    // smoothing은 프레임률 독립 지수 감쇠 계수(0=즉시, 1에 가까울수록 느림).
    void Follow(Vec2 target, float smoothing = 0.0f, float dtSeconds = 0.0f);

    // ---- 강화 카메라(게임 카메라) ----
    // 데드존 추적: target이 카메라 중심 기준 halfExtents(월드 단위) 박스 밖으로 나갈 때만 그
    //   초과분만큼 중심을 민다(캐릭터가 화면 중앙 사각형 안에선 카메라 고정 → 미세 흔들림 방지).
    void FollowDeadzone(Vec2 target, Vec2 halfExtents);

    // 월드 경계: 카메라 뷰가 이 사각형(월드 unit) 밖을 보지 않도록 중심을 클램프한다.
    //   뷰가 경계보다 크면 해당 축은 경계 중앙에 고정. SetPosition/Follow* 후 자동 적용.
    void SetWorldBounds(const Rect& worldBounds);
    void ClearWorldBounds();
    bool HasWorldBounds() const { return m_hasBounds; }

    // 화면 흔들림: amplitude(월드 unit)로 duration(초) 동안 감쇠 흔들림. 연속 호출 시 더 큰 진폭
    //   채택. TickShake(dt)를 매 프레임 호출해 진행/감쇠. 흔들림은 렌더 중심에만 적용(픽킹 불변).
    void AddShake(float amplitude, float duration);
    void TickShake(float dtSeconds);
    bool IsShaking() const { return m_shakeElapsed < m_shakeDuration; }
    Vec2 ShakeOffset() const;   // 현재 흔들림 오프셋(월드 unit)

    Vec2  Position() const;           // 논리(스냅 전) 중심
    Vec2  SnappedPosition() const;    // 픽셀 격자 스냅된 렌더 중심
    float Zoom() const;

    Mat4 ViewMatrix() const;          // 픽셀 스냅 반영(월드→뷰)
    Mat4 ProjectionMatrix() const;    // OrthoOffCenter(내부 RT 기준, NDC 0~1 LH)
    Mat4 ViewProjection() const;      // View * Projection (row-vector 연쇄)

    // Screen↔World 변환(02 소유·정본). nativePx = 내부 해상도 픽셀(좌상단 원점, +Y 아래).
    // 스냅 전 논리 위치 기준(월드 앵커 UI·픽킹이 지터 없이 일관되도록).
    Vec2 WorldToScreen(Vec2 world) const;
    Vec2 ScreenToWorld(Vec2 nativePx) const;

    // 픽셀 스냅으로 버린 서브픽셀 잔여(업스케일 패스 UV 오프셋 환원용, 내부 RT 픽셀 단위, +Y 아래).
    Vec2 SubpixelResidual() const;

private:
    // 픽셀당 월드 단위(줌 반영). 1픽셀 = worldPerPixel unit.
    float WorldPerPixel() const;

    // 경계 클램프된 위치(경계 없으면 그대로). SetPosition/Follow* 후 적용.
    Vec2 ClampToBounds(Vec2 pos) const;
    // 렌더 중심(논리 위치 + 흔들림, 스냅 전). 스냅·서브픽셀·뷰행렬이 이걸 기준으로 계산.
    Vec2 RenderCenterLogical() const;

    Camera2DDesc m_desc{};

    Rect  m_bounds{};
    bool  m_hasBounds = false;

    // 흔들림 상태(감쇠 사인). 결정론적(시간 기반) — 테스트 가능.
    float m_shakeAmplitude = 0.0f;
    float m_shakeDuration  = 0.0f;
    float m_shakeElapsed   = 0.0f;   // duration 이상이면 흔들림 종료
};

} // namespace mye::render
