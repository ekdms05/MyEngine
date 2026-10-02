#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"

#include <cstdint>
#include <optional>
#include <span>

namespace mye::phys {

enum class ShapeKind : uint8_t { AABB, Circle };

struct Shape2D {
    ShapeKind kind = ShapeKind::AABB;
    Vec2 half{.5f, .5f}; // Circle uses half.x as radius; half.y is unused.

    static constexpr Shape2D MakeBox(float halfW, float halfH) {
        return {ShapeKind::AABB, {halfW, halfH}};
    }
    static constexpr Shape2D MakeCircle(float radius) {
        return {ShapeKind::Circle, {radius, radius}};
    }
    constexpr float Radius() const { return half.x; }
};

constexpr uint8_t FloorBit(int8_t level) {
    return level >= 0 && level < 8 ? static_cast<uint8_t>(1u << level) : 0x01u;
}
constexpr bool FloorsOverlap(uint8_t a, uint8_t b) { return (a & b) != 0; }

bool Overlap(const Shape2D& a, Vec2 posA, const Shape2D& b, Vec2 posB);
// Adds the minimum separation to b, leaving contact with a.
std::optional<Vec2> ResolveMTV(const Shape2D& a, Vec2 posA, const Shape2D& b, Vec2 posB);

struct CollisionBody2D {
    uint64_t id = 0; // Opaque identity; only a nonzero matching id is ignored during motion.
    Shape2D shape;
    Vec2 pos{}; // World collider center, including its offset.
    bool isTrigger = false;
    uint32_t layerMask = 0xFFFFFFFFu;
    uint32_t collidesWith = 0xFFFFFFFFu;
    uint8_t floorMask = 1;
    int8_t floorLevel = 0;
    uint32_t triggerId = 0;
    bool kinematic = false;
};

bool CanInteract2D(const CollisionBody2D& a, const CollisionBody2D& b);
bool CanBlock2D(const CollisionBody2D& moving, const CollisionBody2D& obstacle);
Expected<void, Error> ValidateCollisionBodies2D(std::span<const CollisionBody2D> bodies);

struct ShapeCastHit2D {
    double fraction = 0; // Fraction of displacement in [0,1], not elapsed seconds.
    Vec2 normal{};      // Unit normal pointing out of the obstacle.
    uint64_t colliderId = 0;
};

// Reports the first blocking contact, or fraction zero for an initial overlap.
Expected<std::optional<ShapeCastHit2D>, Error> CastMotion2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles, Vec2 displacement);

// Accepts contact, but never repairs an invalid authored or persisted spawn position.
Expected<void, Error> ValidateSpawn2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles);

struct MotionResult2D {
    Vec2 position{}, lastMove{};
    bool hitWall = false;
};

// Value-only calculation: neither body nor obstacles are changed, including on failure.
Expected<MotionResult2D, Error> MoveAndSlide2D(
    const CollisionBody2D& body, std::span<const CollisionBody2D> obstacles,
    Vec2 velocity, float dt, int maxSlideIters);

} // namespace mye::phys
