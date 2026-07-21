// mye/phys/Collision.cpp — 셰이프 겹침·MTV 내로우페이즈 (docs/03 §8)
#include "mye/phys/Collision.h"

#include <cmath>

namespace mye::phys {

namespace {

// AABB half-extents 접근(원은 반지름을 양축 half로 취급).
inline Vec2 HalfExtents(const Shape2D& s) {
    if (s.kind == ShapeKind::Circle) return Vec2{s.Radius(), s.Radius()};
    return s.half;
}

// 두 AABB(중심·half) 겹침 + MTV.
std::optional<Vec2> AabbMtv(Vec2 ca, Vec2 ha, Vec2 cb, Vec2 hb) {
    const float dx = cb.x - ca.x;
    const float px = (ha.x + hb.x) - std::fabs(dx);   // x축 침투량
    if (px <= 0.0f) return std::nullopt;
    const float dy = cb.y - ca.y;
    const float py = (ha.y + hb.y) - std::fabs(dy);   // y축 침투량
    if (py <= 0.0f) return std::nullopt;

    // 최소 침투 축으로 분리(b를 a에서 밀어내는 방향).
    if (px < py) {
        const float sign = (dx < 0.0f) ? -1.0f : 1.0f;
        return Vec2{px * sign, 0.0f};
    }
    const float sign = (dy < 0.0f) ? -1.0f : 1.0f;
    return Vec2{0.0f, py * sign};
}

// 원-원 겹침 + MTV.
std::optional<Vec2> CircleMtv(Vec2 ca, float ra, Vec2 cb, float rb) {
    const Vec2 d = cb - ca;
    const float distSq = d.x * d.x + d.y * d.y;
    const float rsum = ra + rb;
    if (distSq >= rsum * rsum) return std::nullopt;
    const float dist = std::sqrt(distSq);
    if (dist < 1e-6f) {
        // 완전 중첩: 임의 축(+x)으로 분리.
        return Vec2{rsum, 0.0f};
    }
    const float pen = rsum - dist;
    return Vec2{(d.x / dist) * pen, (d.y / dist) * pen};
}

// 원-AABB 겹침 + MTV(원 중심을 AABB에 클램프한 최근접점 기준).
std::optional<Vec2> CircleAabbMtv(Vec2 cc, float r, Vec2 cb, Vec2 hb) {
    // AABB 로컬로 원 중심.
    const Vec2 d = cc - cb;
    const Vec2 closest{Clamp(d.x, -hb.x, hb.x), Clamp(d.y, -hb.y, hb.y)};
    const Vec2 delta = d - closest;            // 최근접점 → 원 중심
    const float distSq = delta.x * delta.x + delta.y * delta.y;
    if (distSq > r * r) return std::nullopt;

    if (distSq > 1e-12f) {
        const float dist = std::sqrt(distSq);
        const float pen = r - dist;
        // 원을 밀어내는 방향(원 기준). b(AABB) 기준 MTV는 반대 부호로 반환(호출 규약: a→b 미는 방향).
        // 여기 반환은 "b가 a로부터 빠져나오는" 벡터를 통일하려 아래 Dispatch에서 부호를 맞춘다.
        return Vec2{(delta.x / dist) * pen, (delta.y / dist) * pen};
    }
    // 원 중심이 AABB 내부: 가장 가까운 면으로 밀어냄.
    const float ox = hb.x - std::fabs(d.x);
    const float oy = hb.y - std::fabs(d.y);
    if (ox < oy) {
        const float sign = (d.x < 0.0f) ? -1.0f : 1.0f;
        return Vec2{(ox + r) * sign, 0.0f};
    }
    const float sign = (d.y < 0.0f) ? -1.0f : 1.0f;
    return Vec2{0.0f, (oy + r) * sign};
}

} // namespace

bool Overlap(const Shape2D& a, Vec2 posA, const Shape2D& b, Vec2 posB) {
    return ResolveMTV(a, posA, b, posB).has_value();
}

// 반환: b를 a로부터 분리하는 최소 이동 벡터(b에 더하면 접촉만 남음).
std::optional<Vec2> ResolveMTV(const Shape2D& a, Vec2 posA, const Shape2D& b, Vec2 posB) {
    const bool ca = a.kind == ShapeKind::Circle;
    const bool cb = b.kind == ShapeKind::Circle;

    if (!ca && !cb) {
        return AabbMtv(posA, a.half, posB, b.half);
    }
    if (ca && cb) {
        return CircleMtv(posA, a.Radius(), posB, b.Radius());
    }
    // 혼합: 원-AABB. CircleAabbMtv는 "원(첫 인자)을 밀어내는" 벡터를 준다.
    if (ca && !cb) {
        // a=원, b=AABB. 원을 밀어내는 벡터 → b를 밀어내려면 부호 반전.
        auto mtv = CircleAabbMtv(posA, a.Radius(), posB, b.half);
        if (!mtv) return std::nullopt;
        return Vec2{-mtv->x, -mtv->y};
    }
    // a=AABB, b=원. 원(b)을 밀어내는 벡터가 곧 b→분리.
    auto mtv = CircleAabbMtv(posB, b.Radius(), posA, a.half);
    return mtv;  // b를 밀어내는 방향(=원을 밀어냄)
}

} // namespace mye::phys
