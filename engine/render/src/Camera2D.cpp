// mye/render/Camera2D.cpp — Screen2D 직교 카메라 구현 (docs/02 §카메라)
#include "mye/render/Camera2D.h"

#include <cmath>

namespace mye::render {

Camera2D::Camera2D(const Camera2DDesc& desc) : m_desc(desc) {
    if (m_desc.zoom <= 0.0f) m_desc.zoom = 1.0f;
    if (m_desc.viewportWidth == 0)  m_desc.viewportWidth  = kInternalWidth;
    if (m_desc.viewportHeight == 0) m_desc.viewportHeight = kInternalHeight;
}

void Camera2D::SetPosition(Vec2 worldPos) { m_desc.position = worldPos; }

void Camera2D::SetZoom(float zoom) { m_desc.zoom = zoom > 0.0f ? zoom : m_desc.zoom; }

void Camera2D::SetViewportSize(uint32_t w, uint32_t h) {
    if (w > 0) m_desc.viewportWidth = w;
    if (h > 0) m_desc.viewportHeight = h;
}

void Camera2D::SetPixelSnap(bool enabled) { m_desc.pixelSnap = enabled; }

void Camera2D::Follow(Vec2 target, float smoothing, float dtSeconds) {
    if (smoothing <= 0.0f || dtSeconds <= 0.0f) {
        m_desc.position = target;
        return;
    }
    // 프레임률 독립 지수 감쇠: t = 1 - smoothing^dt. smoothing∈(0,1), 작을수록 빠름.
    const float clamped = Clamp(smoothing, 0.0f, 0.9999f);
    const float t = 1.0f - std::pow(clamped, dtSeconds);
    m_desc.position = Vec2::Lerp(m_desc.position, target, Saturate(t));
}

Vec2  Camera2D::Position() const { return m_desc.position; }
float Camera2D::Zoom() const { return m_desc.zoom; }

float Camera2D::WorldPerPixel() const {
    // 내부 RT 1픽셀에 대응하는 월드 단위(줌 반영).
    return 1.0f / (kPixelsPerUnit * m_desc.zoom);
}

Vec2 Camera2D::SnappedPosition() const {
    if (!m_desc.pixelSnap) return m_desc.position;
    const float wpp = WorldPerPixel();
    // 카메라 중심을 렌더 픽셀 격자에 스냅.
    return {std::round(m_desc.position.x / wpp) * wpp,
            std::round(m_desc.position.y / wpp) * wpp};
}

Vec2 Camera2D::SubpixelResidual() const {
    if (!m_desc.pixelSnap) return {};
    const Vec2 snapped = SnappedPosition();
    // 논리 위치 - 스냅 위치 = 아직 반영 못한 월드 잔차. 픽셀 단위로 환산.
    // 화면 방향(+Y 아래)으로 부호 변환: 월드 +Y(위) = 화면 -Y.
    const Vec2 residualWorld = m_desc.position - snapped;   // 월드 단위
    const float pxPerWorld = kPixelsPerUnit * m_desc.zoom;
    return {residualWorld.x * pxPerWorld, -residualWorld.y * pxPerWorld};
}

Mat4 Camera2D::ViewMatrix() const {
    const Vec2 c = SnappedPosition();
    // 월드→뷰: 카메라 중심을 원점으로. Screen2D는 회전 없음.
    return Mat4::Translation({-c.x, -c.y, 0.0f});
}

Mat4 Camera2D::ProjectionMatrix() const {
    const float w = static_cast<float>(m_desc.viewportWidth)  * WorldPerPixel();
    const float h = static_cast<float>(m_desc.viewportHeight) * WorldPerPixel();
    // 중심 정렬 직교. LH, NDC 깊이 0~1. z 범위는 스프라이트 깊이 정책 여유분(0..1 unit).
    return Mat4::OrthoLH(w, h, 0.0f, 1.0f);
}

Mat4 Camera2D::ViewProjection() const {
    // row-vector 연쇄: v * View * Proj.
    return ViewMatrix() * ProjectionMatrix();
}

Vec2 Camera2D::WorldToScreen(Vec2 world) const {
    // 논리(스냅 전) 중심 기준. 월드 오프셋(unit) → 내부 RT 픽셀(좌상단 원점, +Y 아래).
    const float pxPerWorld = kPixelsPerUnit * m_desc.zoom;
    const Vec2 rel = world - m_desc.position;         // 카메라 기준(월드)
    const float halfW = static_cast<float>(m_desc.viewportWidth)  * 0.5f;
    const float halfH = static_cast<float>(m_desc.viewportHeight) * 0.5f;
    return {halfW + rel.x * pxPerWorld,
            halfH - rel.y * pxPerWorld};              // +Y월드(위) → -Y화면
}

Vec2 Camera2D::ScreenToWorld(Vec2 nativePx) const {
    const float worldPerPx = WorldPerPixel();
    const float halfW = static_cast<float>(m_desc.viewportWidth)  * 0.5f;
    const float halfH = static_cast<float>(m_desc.viewportHeight) * 0.5f;
    const float relX = (nativePx.x - halfW) * worldPerPx;
    const float relY = (halfH - nativePx.y) * worldPerPx;   // 화면 -Y → 월드 +Y
    return {m_desc.position.x + relX, m_desc.position.y + relY};
}

} // namespace mye::render
