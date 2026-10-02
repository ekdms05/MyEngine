#include "mye/phys/Motion2D.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

Vec2 Advance(Vec2 position, Vec2 displacement, double fraction) {
    return {static_cast<float>(position.x + static_cast<double>(displacement.x) * fraction),
            static_cast<float>(position.y + static_cast<double>(displacement.y) * fraction)};
}

Vec2 RoundOutward(Vec2 position, Vec2 normal) {
    // One representable step avoids rounding a contact back into the solid.
    const float infinity = std::numeric_limits<float>::infinity();
    if (normal.x != 0) position.x = std::nextafter(position.x, normal.x < 0 ? -infinity : infinity);
    if (normal.y != 0) position.y = std::nextafter(position.y, normal.y < 0 ? -infinity : infinity);
    return position;
}

bool HasBlockingOverlap(const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles,
                        Vec2 position) {
    for (const auto& obstacle : obstacles) {
        if (!CanBlock2D(body, obstacle)) continue;
        const auto mtv = ResolveMTV(obstacle.shape, obstacle.pos, body.shape, position);
        if (mtv && (mtv->x != 0 || mtv->y != 0)) return true;
    }
    return false;
}

} // namespace

bool CanInteract2D(const CollisionBody2D& a, const CollisionBody2D& b) {
    return FloorsOverlap(a.floorMask, b.floorMask) &&
           ((a.collidesWith & b.layerMask) != 0 || (b.collidesWith & a.layerMask) != 0);
}

bool CanBlock2D(const CollisionBody2D& moving, const CollisionBody2D& obstacle) {
    return !obstacle.isTrigger && (moving.id == 0 || moving.id != obstacle.id) &&
           CanInteract2D(moving, obstacle);
}

Expected<void, Error> ValidateCollisionBodies2D(std::span<const CollisionBody2D> bodies) {
    for (const auto& body : bodies)
        if (!ValidBody(body))
            return Error{"2D collider needs a valid shape, positive size, finite bounds and floor 0..7", 1};
    return {};
}

Expected<void, Error> ValidateSpawn2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles) {
    auto cast = CastMotion2D(body, obstacles, {});
    if (!cast) return cast.GetError();
    if (cast.Value()) return Error{"2D spawn overlaps a blocking collider", 1};
    return {};
}

Expected<void, Error> ValidateMotionSettings2D(const MotionSettings2D& settings) {
    if (!ValidBody(settings.body) || settings.body.isTrigger || !Finite(settings.offset) ||
        !std::isfinite(settings.speed) || settings.speed < 0 || settings.speed > 100 ||
        settings.maxSlideIters < 1 || settings.maxSlideIters > 16)
        return Error{"2D character requires solid bounds, finite offset, speed 0..100 and 1..16 slides", 1};
    return {};
}

Expected<void, Error> StepMotion2D(MotionState2D& state, Vec2 movement, float dt,
    const MotionSettings2D& settings, std::span<const CollisionBody2D> obstacles) {
    if (auto valid = ValidateMotionSettings2D(settings); !valid) return valid.GetError();
    if (!Finite(state.position) || !Finite(state.lastMove) || !std::isfinite(state.facingRadians) ||
        std::abs(state.facingRadians) > kPi || state.floorLevel != settings.body.floorLevel ||
        !Finite(movement))
        return Error{"2D character state/input must be finite and match the authored floor", 1};
    movement.x = std::clamp(movement.x, -1.0f, 1.0f);
    movement.y = std::clamp(movement.y, -1.0f, 1.0f);
    const float length = std::hypot(movement.x, movement.y);
    if (length > 1) movement = movement / length;
    auto body = settings.body;
    body.pos = state.position + settings.offset;
    auto moved = MoveAndSlide2D(body, obstacles, movement * settings.speed, dt, settings.maxSlideIters);
    if (!moved) return moved.GetError();
    const auto origin = moved.Value().position - settings.offset;
    if (!Finite(origin)) return Error{"2D character origin is outside float bounds", 1};
    state.position = origin;
    state.lastMove = moved.Value().lastMove;
    state.onWall = moved.Value().hitWall;
    if (length > 0) state.facingRadians = std::atan2(movement.x, movement.y);
    return {};
}

Expected<MotionResult2D, Error> MoveAndSlide2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles,
    Vec2 velocity, float dt, int maxSlideIters) {
    if (!ValidBody(body) || body.isTrigger || !Finite(velocity) || !std::isfinite(dt) ||
        dt <= 0 || dt > 1 || maxSlideIters < 1 || maxSlideIters > 16)
        return Error{"2D motion requires a solid body, finite velocity, dt in (0,1] and 1..16 slides", 1};
    if (auto valid = ValidateCollisionBodies2D(obstacles); !valid) return valid.GetError();

    MotionResult2D result;
    result.position = body.pos;

    // Recover even a stationary body. An unresolved gap must fail without a world commit.
    for (int pass = 0; pass < maxSlideIters; ++pass) {
        bool corrected = false;
        for (const auto& obstacle : obstacles) {
            if (!CanBlock2D(body, obstacle)) continue;
            const auto mtv = ResolveMTV(obstacle.shape, obstacle.pos, body.shape, result.position);
            if (!mtv || (mtv->x == 0 && mtv->y == 0)) continue;
            if (!Finite(*mtv)) return Error{"2D overlap recovery overflow", 1};
            result.position = RoundOutward(Advance(result.position, *mtv, 1), *mtv);
            if (!Finite(result.position)) return Error{"2D overlap recovery position overflow", 1};
            corrected = result.hitWall = true;
        }
        if (!corrected) break;
    }
    if (HasBlockingOverlap(body, obstacles, result.position))
        return Error{"2D initial overlap cannot be resolved within the slide limit", 1};

    Vec2 remaining = velocity * dt;
    for (int slide = 0; slide <= maxSlideIters; ++slide) {
        if (remaining.x == 0 && remaining.y == 0) break;
        CollisionBody2D moving = body;
        moving.pos = result.position;
        auto cast = CastMotion2D(moving, obstacles, remaining);
        if (!cast) return cast.GetError();
        if (!cast.Value()) {
            result.position = Advance(result.position, remaining, 1);
            break;
        }
        const auto& hit = *cast.Value();
        result.position = RoundOutward(Advance(result.position, remaining, hit.fraction), hit.normal);
        if (!Finite(result.position)) return Error{"2D contact position overflow", 1};
        result.hitWall = true;
        // A final cast allows the last tangent travel, but never another unchecked direction change.
        if (slide == maxSlideIters) break;
        remaining = {static_cast<float>(remaining.x * (1 - hit.fraction)),
                     static_cast<float>(remaining.y * (1 - hit.fraction))};
        if (remaining.x == 0 && remaining.y == 0) break;
        const double inward = static_cast<double>(remaining.x) * hit.normal.x +
                              static_cast<double>(remaining.y) * hit.normal.y;
        if (inward >= 0) return Error{"2D motion cast returned a non-blocking contact", 1};
        remaining = {static_cast<float>(remaining.x - inward * hit.normal.x),
                     static_cast<float>(remaining.y - inward * hit.normal.y)};
        if (!Finite(remaining)) return Error{"2D slide displacement overflow", 1};
    }
    result.lastMove = result.position - body.pos;
    if (!Finite(result.lastMove)) return Error{"2D motion displacement overflow", 1};
    CollisionBody2D finalBody = body;
    finalBody.pos = result.position;
    if (!ValidBody(finalBody)) return Error{"2D motion bounds overflow", 1};
    if (HasBlockingOverlap(body, obstacles, result.position))
        return Error{"2D contact loses precision at this coordinate or motion scale", 1};
    return result;
}

} // namespace mye::phys
