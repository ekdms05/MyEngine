// Shape overlap, separation and continuous 2D casts (docs/03-scene-world.md).
#include "mye/phys/Motion2D.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <tuple>

namespace mye::phys {

namespace {

// 두 AABB(중심·half) 겹침 + MTV.
std::optional<Vec2> AabbMtv(Vec2 ca, Vec2 ha, Vec2 cb, Vec2 hb) {
    // Finite float shapes can overflow their sum/distance; widen intermediates only.
    const double dx = static_cast<double>(cb.x) - ca.x;
    const double px = (static_cast<double>(ha.x) + hb.x) - std::fabs(dx);   // x축 침투량
    if (px <= 0.0f) return std::nullopt;
    const double dy = static_cast<double>(cb.y) - ca.y;
    const double py = (static_cast<double>(ha.y) + hb.y) - std::fabs(dy);   // y축 침투량
    if (py <= 0.0f) return std::nullopt;

    // 최소 침투 축으로 분리(b를 a에서 밀어내는 방향).
    if (px < py) {
        const float sign = (dx < 0.0f) ? -1.0f : 1.0f;
        return Vec2{static_cast<float>(px * sign), 0.0f};
    }
    const float sign = (dy < 0.0f) ? -1.0f : 1.0f;
    return Vec2{0.0f, static_cast<float>(py * sign)};
}

// 원-원 겹침 + MTV.
std::optional<Vec2> CircleMtv(Vec2 ca, float ra, Vec2 cb, float rb) {
    const double dx = static_cast<double>(cb.x) - ca.x;
    const double dy = static_cast<double>(cb.y) - ca.y;
    const double dist = std::hypot(dx, dy);
    const double rsum = static_cast<double>(ra) + rb;
    if (dist >= rsum) return std::nullopt;
    if (dist == 0) {
        // 완전 중첩: 임의 축(+x)으로 분리.
        return Vec2{static_cast<float>(rsum), 0.0f};
    }
    const double pen = rsum - dist;
    return Vec2{static_cast<float>((dx / dist) * pen),
                static_cast<float>((dy / dist) * pen)};
}

// 원-AABB 겹침 + MTV(원 중심을 AABB에 클램프한 최근접점 기준).
std::optional<Vec2> CircleAabbMtv(Vec2 cc, float r, Vec2 cb, Vec2 hb) {
    // AABB 로컬로 원 중심.
    const Vec2 d = cc - cb;
    const Vec2 closest{Clamp(d.x, -hb.x, hb.x), Clamp(d.y, -hb.y, hb.y)};
    const Vec2 delta = d - closest;            // 최근접점 → 원 중심
    const double dist = std::hypot(static_cast<double>(delta.x), delta.y);
    if (dist > r) return std::nullopt;

    if (dist > 0) {
        const double pen = r - dist;
        // 원을 밀어내는 방향(원 기준). b(AABB) 기준 MTV는 반대 부호로 반환(호출 규약: a→b 미는 방향).
        // 여기 반환은 "b가 a로부터 빠져나오는" 벡터를 통일하려 아래 Dispatch에서 부호를 맞춘다.
        return Vec2{static_cast<float>((delta.x / dist) * pen),
                    static_cast<float>((delta.y / dist) * pen)};
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

std::optional<ShapeCastHit2D> SweepBox(double x, double y, Vec2 motion, double halfX, double halfY) {
    double enter = -std::numeric_limits<double>::infinity();
    double exit = std::numeric_limits<double>::infinity();
    Vec2 normal{};
    const auto axis = [&](double center, double speed, double half, Vec2 direction) {
        // A tangent on the boundary is free travel, rather than an interior intersection.
        if (speed == 0) return center > -half && center < half;
        double near = (-half - center) / speed, far = (half - center) / speed;
        if (near > far) std::swap(near, far);
        if (near > enter) {
            enter = near;
            normal = direction * (speed > 0 ? -1.0f : 1.0f);
        }
        exit = std::min(exit, far);
        return enter <= exit;
    };
    if (!axis(x, motion.x, halfX, {1, 0}) || !axis(y, motion.y, halfY, {0, 1}) ||
        enter < 0 || enter > 1 || exit <= 0 || enter >= exit) return std::nullopt;
    return ShapeCastHit2D{enter, normal};
}

std::optional<ShapeCastHit2D> SweepCircle(double x, double y, Vec2 motion, double radius) {
    const double length = std::hypot(static_cast<double>(motion.x), motion.y);
    if (length == 0) return std::nullopt;
    const double ux = motion.x / length, uy = motion.y / length;
    const double parallel = x * ux + y * uy, perpendicular = x * uy - y * ux;
    if (std::abs(perpendicular) >= radius) return std::nullopt;
    const double chord = std::sqrt(radius * radius - perpendicular * perpendicular);
    if (chord == 0) return std::nullopt;
    const double fraction = (-parallel - chord) / length;
    if (fraction < 0 || fraction > 1) return std::nullopt;
    // Construct the contact normal in the travel basis; a far-away start need not
    // subtract two almost equal positions to recover the small circle's surface.
    return ShapeCastHit2D{fraction, {static_cast<float>((-chord * ux + perpendicular * uy) / radius),
                                     static_cast<float>((-chord * uy - perpendicular * ux) / radius)}};
}

void Consider(std::optional<ShapeCastHit2D>& closest, std::optional<ShapeCastHit2D> candidate) {
    if (!candidate) return;
    const auto& a = *candidate;
    if (!closest || std::tie(a.fraction, a.normal.x, a.normal.y, a.colliderId) <
                        std::tie(closest->fraction, closest->normal.x, closest->normal.y, closest->colliderId))
        closest = candidate;
}

std::optional<ShapeCastHit2D> SweepRoundedBox(double x, double y, Vec2 motion, Vec2 half, double radius) {
    // The exact Minkowski shape is two strips and four corner disks. A plain
    // expanded AABB would invent blocking corners for circular characters.
    std::optional<ShapeCastHit2D> closest;
    Consider(closest, SweepBox(x, y, motion, half.x + radius, half.y));
    Consider(closest, SweepBox(x, y, motion, half.x, half.y + radius));
    for (int sx : {-1, 1})
        for (int sy : {-1, 1})
            Consider(closest, SweepCircle(x - sx * static_cast<double>(half.x),
                                           y - sy * static_cast<double>(half.y), motion, radius));
    return closest;
}

std::optional<ShapeCastHit2D> SweepShapes(const CollisionBody2D& body, const CollisionBody2D& obstacle,
                                       Vec2 motion) {
    const double x = static_cast<double>(body.pos.x) - obstacle.pos.x;
    const double y = static_cast<double>(body.pos.y) - obstacle.pos.y;
    const bool circle = body.shape.kind == ShapeKind::Circle;
    const bool otherCircle = obstacle.shape.kind == ShapeKind::Circle;
    if (!circle && !otherCircle)
        return SweepBox(x, y, motion, static_cast<double>(body.shape.half.x) + obstacle.shape.half.x,
                         static_cast<double>(body.shape.half.y) + obstacle.shape.half.y);
    if (circle && otherCircle)
        return SweepCircle(x, y, motion, static_cast<double>(body.shape.Radius()) + obstacle.shape.Radius());
    if (circle) return SweepRoundedBox(x, y, motion, obstacle.shape.half, body.shape.Radius());
    auto hit = SweepRoundedBox(-x, -y, {-motion.x, -motion.y}, body.shape.half, obstacle.shape.Radius());
    if (hit) hit->normal = {-hit->normal.x, -hit->normal.y};
    return hit;
}

} // namespace

Expected<std::optional<ShapeCastHit2D>, Error> CastMotion2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles, Vec2 displacement) {
    if (auto valid = ValidateCollisionBodies2D(std::span(&body, 1)); !valid) return valid.GetError();
    if (auto valid = ValidateCollisionBodies2D(obstacles); !valid) return valid.GetError();
    if (body.isTrigger || !std::isfinite(displacement.x) || !std::isfinite(displacement.y))
        return Error{"2D motion cast needs a solid body and finite displacement", 1};
    std::optional<ShapeCastHit2D> closest;
    // ponytail: linear solid scan per cast; reuse a broadphase when target-scene measurements justify it.
    for (const auto& obstacle : obstacles) {
        if (!CanBlock2D(body, obstacle)) continue;
        const auto mtv = ResolveMTV(obstacle.shape, obstacle.pos, body.shape, body.pos);
        if (mtv && (mtv->x != 0 || mtv->y != 0)) {
            const double length = std::hypot(static_cast<double>(mtv->x), mtv->y);
            if (!std::isfinite(length)) return Error{"2D motion cast overlap normal overflow", 1};
            Consider(closest, ShapeCastHit2D{0, {static_cast<float>(mtv->x / length),
                                                 static_cast<float>(mtv->y / length)}, obstacle.id});
            continue;
        }
        auto hit = SweepShapes(body, obstacle, displacement);
        if (hit) {
            hit->colliderId = obstacle.id;
            Consider(closest, hit);
        }
    }
    return closest;
}

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
