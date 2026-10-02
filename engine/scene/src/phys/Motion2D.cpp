#include "mye/phys/Motion2D.h"

#include <algorithm>
#include <cmath>

namespace mye::phys {
namespace {

bool Finite(Vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool ValidBody(const CollisionBody2D& body) {
    const auto& shape = body.shape;
    const Vec2 half = shape.kind == ShapeKind::Circle
                          ? Vec2{shape.Radius(), shape.Radius()} : shape.half;
    return body.floorLevel >= 0 && body.floorLevel < 8 &&
           Finite(body.pos) && Finite(shape.half) && shape.half.x > 0 &&
           ((shape.kind == ShapeKind::AABB && shape.half.y > 0) ||
            shape.kind == ShapeKind::Circle) &&
           Finite(body.pos - half) && Finite(body.pos + half);
}

} // namespace

bool CanInteract2D(const CollisionBody2D& a, const CollisionBody2D& b) {
    return FloorsOverlap(a.floorMask, b.floorMask) &&
           ((a.collidesWith & b.layerMask) != 0 || (b.collidesWith & a.layerMask) != 0);
}

Expected<void, Error> ValidateCollisionBodies2D(std::span<const CollisionBody2D> bodies) {
    for (const auto& body : bodies)
        if (!ValidBody(body))
            return Error{"2D collider needs a valid shape, positive size, finite bounds and floor 0..7", 1};
    return {};
}

Expected<MotionResult2D, Error> MoveAndSlide2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles,
    Vec2 velocity, float dt, int maxSlideIters, const ITileCollision* tiles) {
    if (!ValidBody(body) || body.isTrigger || !Finite(velocity) || !std::isfinite(dt) ||
        dt <= 0 || dt > 1 || maxSlideIters < 1 || maxSlideIters > 16)
        return Error{"2D motion requires a solid body, finite velocity, dt in (0,1] and 1..16 slides", 1};
    if (auto valid = ValidateCollisionBodies2D(obstacles); !valid) return valid.GetError();

    const Vec2 desired = velocity * dt;
    const Vec2 half = body.shape.kind == ShapeKind::Circle
                          ? Vec2{body.shape.Radius(), body.shape.Radius()}
                          : body.shape.half;
    const float minHalf = std::max(.001f, std::min(half.x, half.y));
    const float ratio = std::hypot(desired.x, desired.y) / minHalf;
    // Clamp before integer conversion: finite velocity can still overflow this ratio.
    // ponytail: 16 discrete substeps; shape sweeps are required before thin-wall online acceptance.
    const int substeps = ratio >= 15 ? 16 : 1 + static_cast<int>(ratio);
    const Vec2 subDesired = desired / static_cast<float>(substeps);
    MotionResult2D result;
    result.position = body.pos;

    for (int step = 0; step < substeps; ++step) {
        Vec2 remaining = subDesired;
        for (int iter = 0; iter < maxSlideIters; ++iter) {
            if (std::abs(remaining.x) < 1e-7f && std::abs(remaining.y) < 1e-7f) break;
            Vec2 candidate = result.position + remaining;
            if (!Finite(candidate)) return Error{"2D motion position overflow", 1};
            Vec2 correction{};
            bool collided = false;
            for (const auto& obstacle : obstacles) {
                if ((body.id != 0 && obstacle.id == body.id) || obstacle.isTrigger ||
                    !CanInteract2D(body, obstacle)) continue;
                if (auto mtv = ResolveMTV(obstacle.shape, obstacle.pos, body.shape, candidate)) {
                    candidate += *mtv;
                    correction += *mtv;
                    collided = true;
                }
            }
            if (tiles) {
                if (auto mtv = tiles->ResolveSolid(body.shape, candidate, body.floorLevel)) {
                    candidate += *mtv;
                    correction += *mtv;
                    collided = true;
                }
            }
            if (!Finite(candidate) || !Finite(correction))
                return Error{"2D collision returned a non-finite correction", 1};
            remaining = remaining - (candidate - result.position);
            result.position = candidate;
            result.hitWall |= collided;
            if (!collided) break;

            const float length = std::hypot(correction.x, correction.y);
            if (!std::isfinite(length)) return Error{"2D collision correction length overflow", 1};
            if (length <= 1e-7f) break;
            const Vec2 normal = correction / length;
            const float inward = Vec2::Dot(remaining, normal);
            if (!std::isfinite(inward)) return Error{"2D slide projection overflow", 1};
            remaining = inward < 0 ? remaining - normal * inward : Vec2{};
            if (!Finite(remaining)) return Error{"2D slide displacement overflow", 1};
        }
    }
    result.lastMove = result.position - body.pos;
    if (!Finite(result.lastMove)) return Error{"2D motion displacement overflow", 1};
    CollisionBody2D finalBody = body;
    finalBody.pos = result.position;
    if (!ValidBody(finalBody)) return Error{"2D motion bounds overflow", 1};
    return result;
}

} // namespace mye::phys
