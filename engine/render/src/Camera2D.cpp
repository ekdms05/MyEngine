// mye/render/Camera2D.cpp — Screen2D 직교 카메라 구현 (docs/02 §카메라)
#include "mye/render/Camera2D.h"

#include <cmath>

namespace mye::render {

Camera2D::Camera2D(const Camera2DDesc& desc) : m_desc(desc) {
    if (m_desc.zoom <= 0.0f) m_desc.zoom = 1.0f;
    if (m_desc.viewportWidth == 0)  m_desc.viewportWidth  = kInternalWidth;
    if (m_desc.viewportHeight == 0) m_desc.viewportHeight = kInternalHeight;
}

void Camera2D::SetPosition(Vec2 worldPos) { m_desc.position = ClampToBounds(worldPos); }

void Camera2D::SetZoom(float zoom) { m_desc.zoom = zoom > 0.0f ? zoom : m_desc.zoom; }

void Camera2D::SetViewportSize(uint32_t w, uint32_t h) {
    if (w > 0) m_desc.viewportWidth = w;
    if (h > 0) m_desc.viewportHeight = h;
}

void Camera2D::SetPixelSnap(bool enabled) { m_desc.pixelSnap = enabled; }

void Camera2D::Follow(Vec2 target, float smoothing, float dtSeconds) {
    if (smoothing <= 0.0f || dtSeconds <= 0.0f) {
        m_desc.position = ClampToBounds(target);
        return;
    }
    // 프레임률 독립 지수 감쇠: t = 1 - smoothing^dt. smoothing∈(0,1), 작을수록 빠름.
    const float clamped = Clamp(smoothing, 0.0f, 0.9999f);
    const float t = 1.0f - std::pow(clamped, dtSeconds);
    m_desc.position = ClampToBounds(Vec2::Lerp(m_desc.position, target, Saturate(t)));
}

void Camera2D::FollowDeadzone(Vec2 target, Vec2 half) {
    Vec2 c = m_desc.position;
    const Vec2 d = target - c;
    if (d.x >  half.x) c.x += d.x - half.x;
    else if (d.x < -half.x) c.x += d.x + half.x;
    if (d.y >  half.y) c.y += d.y - half.y;
    else if (d.y < -half.y) c.y += d.y + half.y;
    m_desc.position = ClampToBounds(c);
}

void Camera2D::SetWorldBounds(const Rect& worldBounds) {
    m_bounds = worldBounds;
    m_hasBounds = true;
    m_desc.position = ClampToBounds(m_desc.position);
}
void Camera2D::ClearWorldBounds() { m_hasBounds = false; }

Vec2 Camera2D::ClampToBounds(Vec2 pos) const {
    if (!m_hasBounds) return pos;
    const float halfW = static_cast<float>(m_desc.viewportWidth)  * 0.5f * WorldPerPixel();
    const float halfH = static_cast<float>(m_desc.viewportHeight) * 0.5f * WorldPerPixel();
    const float minX = m_bounds.x + halfW, maxX = m_bounds.x + m_bounds.w - halfW;
    const float minY = m_bounds.y + halfH, maxY = m_bounds.y + m_bounds.h - halfH;
    Vec2 c = pos;
    c.x = (minX > maxX) ? (m_bounds.x + m_bounds.w * 0.5f) : Clamp(c.x, minX, maxX);
    c.y = (minY > maxY) ? (m_bounds.y + m_bounds.h * 0.5f) : Clamp(c.y, minY, maxY);
    return c;
}

void Camera2D::AddShake(float amplitude, float duration) {
    if (amplitude <= 0.0f || duration <= 0.0f) return;
    // 진행 중이면 더 큰 진폭을 채택(작은 흔들림이 큰 흔들림을 덮지 않게), 끝났으면 새로 시작.
    if (m_shakeElapsed >= m_shakeDuration || amplitude >= m_shakeAmplitude) {
        m_shakeAmplitude = amplitude;
        m_shakeDuration  = duration;
        m_shakeElapsed   = 0.0f;
    }
}
void Camera2D::TickShake(float dtSeconds) {
    if (dtSeconds > 0.0f && m_shakeElapsed < m_shakeDuration)
        m_shakeElapsed += dtSeconds;
}
Vec2 Camera2D::ShakeOffset() const {
    if (m_shakeDuration <= 0.0f || m_shakeElapsed >= m_shakeDuration) return {};
    const float decay = 1.0f - (m_shakeElapsed / m_shakeDuration);   // 선형 감쇠
    const float a = m_shakeAmplitude * decay;
    // 결정론적 감쇠 사인(두 주파수·위상차) — 랜덤 없이 재현 가능.
    return {std::sin(m_shakeElapsed * 62.83f) * a,
            std::sin(m_shakeElapsed * 47.12f + 1.7f) * a};
}
Vec2 Camera2D::RenderCenterLogical() const { return m_desc.position + ShakeOffset(); }

Vec2  Camera2D::Position() const { return m_desc.position; }
float Camera2D::Zoom() const { return m_desc.zoom; }

float Camera2D::WorldPerPixel() const {
    // 내부 RT 1픽셀에 대응하는 월드 단위(줌 반영).
    return 1.0f / (kPixelsPerUnit * m_desc.zoom);
}

Vec2 Camera2D::SnappedPosition() const {
    const Vec2 center = RenderCenterLogical();   // 논리 중심 + 흔들림(픽킹엔 미반영)
    if (!m_desc.pixelSnap) return center;
    const float wpp = WorldPerPixel();
    // 렌더 중심을 픽셀 격자에 스냅.
    return {std::round(center.x / wpp) * wpp,
            std::round(center.y / wpp) * wpp};
}

Vec2 Camera2D::SubpixelResidual() const {
    if (!m_desc.pixelSnap) return {};
    const Vec2 snapped = SnappedPosition();
    // 렌더 중심 - 스냅 위치 = 아직 반영 못한 월드 잔차. 픽셀 단위로 환산.
    // 화면 방향(+Y 아래)으로 부호 변환: 월드 +Y(위) = 화면 -Y.
    const Vec2 residualWorld = RenderCenterLogical() - snapped;   // 월드 단위
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
