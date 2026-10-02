// Slide projection adapted from Godot core/math/vector3.h, MIT.
// Copyright (c) 2014-present Godot Engine contributors. See third_party/godot/LICENSE.txt.
#include "mye/phys/PhysicsWorld3D.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mye::phys {
namespace {
bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float Axis(Vec3 v, int axis) {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
Vec3 Basis(int axis) {
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}
Vec3 Slide(Vec3 motion, Vec3 normal) {
    return motion - normal * Vec3::Dot(motion, normal);
}
bool SweepIntervals(float low, float high, float radius, float center, float speed, Vec3 axis, float& enter,
                    float& exit, Vec3& normal) {
    if (std::abs(speed) < 1e-8f) return center + radius > low + 1e-6f && center - radius < high - 1e-6f;
    float a = (low - radius - center) / speed, b = (high + radius - center) / speed;
    if (a > b) std::swap(a, b);
    if (a > enter) {
        enter = a;
        normal = axis * (speed > 0 ? -1.0f : 1.0f);
    }
    exit = std::min(exit, b);
    return enter <= exit;
}
std::optional<Hit3D> SweepBox(Box3D moving, Vec3 motion, const Solid3D& solid) {
    float enter = -std::numeric_limits<float>::infinity(), exit = 1;
    Vec3 normal{};
    for (int axis = 0; axis < 3; ++axis) {
        const float center = Axis(solid.box.center, axis), half = Axis(solid.box.half, axis);
        if (!SweepIntervals(center - half, center + half, Axis(moving.half, axis), Axis(moving.center, axis),
                            Axis(motion, axis), Basis(axis), enter, exit, normal))
            return {};
    }
    if (enter < -1e-6f || enter > 1 || exit <= 0) return {};
    return Hit3D{std::max(0.0f, enter), normal, solid.id};
}
std::optional<Hit3D> SweepRamp(Box3D box, Vec3 motion, const Solid3D& solid) {
    const auto lo = solid.box.center - solid.box.half, hi = solid.box.center + solid.box.half;
    const std::array<Vec3, 6> vertices{{{lo.x, lo.y, lo.z},
                                        {hi.x, lo.y, lo.z},
                                        {hi.x, hi.y, hi.z},
                                        {lo.x, hi.y, hi.z},
                                        {lo.x, lo.y, hi.z},
                                        {hi.x, lo.y, hi.z}}};
    const std::array<Vec3, 4> axes{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, solid.box.half.z, -solid.box.half.y}}};
    float enter = -std::numeric_limits<float>::infinity(), exit = 1;
    Vec3 normal{};
    for (auto axis : axes) {
        axis = axis * (1 / axis.Length());
        float low = Vec3::Dot(vertices[0], axis), high = low;
        for (auto vertex : vertices) {
            const float p = Vec3::Dot(vertex, axis);
            low = std::min(low, p);
            high = std::max(high, p);
        }
        const float radius =
            std::abs(axis.x) * box.half.x + std::abs(axis.y) * box.half.y + std::abs(axis.z) * box.half.z;
        if (!SweepIntervals(low, high, radius, Vec3::Dot(box.center, axis), Vec3::Dot(motion, axis), axis,
                            enter, exit, normal))
            return {};
    }
    if (enter < -1e-6f || enter > 1 || exit <= 0) return {};
    return Hit3D{std::max(0.0f, enter), normal, solid.id};
}
bool BoxOverlap(Box3D a, Box3D b) {
    const auto delta = a.center - b.center, sum = a.half + b.half;
    return std::abs(delta.x) < sum.x - 1e-6f && std::abs(delta.y) < sum.y - 1e-6f &&
           std::abs(delta.z) < sum.z - 1e-6f;
}
} // namespace
Expected<void, Error> ValidateMotionSettings3D(const MotionSettings3D& s) {
    if (!Finite(s.half) || !Finite(s.offset) || s.half.x <= 0 || s.half.y <= 0 || s.half.z <= 0 ||
        s.half.x > 100 || s.half.y > 100 || s.half.z > 100 || !std::isfinite(s.speed) || s.speed < 0 ||
        s.speed > 100 || !std::isfinite(s.gravity) || s.gravity < 0 || s.gravity > 100 ||
        !std::isfinite(s.jumpSpeed) || s.jumpSpeed < 0 || s.jumpSpeed > 100 || !std::isfinite(s.skin) ||
        s.skin < 0 || s.skin > .1f || !std::isfinite(s.floorSnap) || s.floorSnap < 0 || s.floorSnap > 2 ||
        !std::isfinite(s.stepHeight) || s.stepHeight < 0 || s.stepHeight > 2 ||
        !std::isfinite(s.floorMaxAngle) || s.floorMaxAngle < 0 || s.floorMaxAngle >= 90 || s.maxSlides < 1 ||
        s.maxSlides > 16)
        return Error{"Invalid 3D character dimensions, speed, gravity or floor settings", 1};
    return {};
}
std::optional<Hit3D> PhysicsWorld3D::Cast(Box3D box, Vec3 motion, uint64_t ignore) const {
    std::optional<Hit3D> closest;
    // ponytail: linear static collision scan; add a broadphase after measuring target-scene cost.
    for (const auto& solid : m_solids) {
        if (solid.trigger || (ignore != 0 && solid.id == ignore)) continue;
        const auto consider = [&](std::optional<Hit3D> hit) {
            if (hit && (!closest || hit->fraction < closest->fraction)) closest = hit;
        };
        if (solid.shape == Shape3D::Box) consider(SweepBox(box, motion, solid));
        else consider(SweepRamp(box, motion, solid));
    }
    return closest;
}
Expected<void, Error> PhysicsWorld3D::Add(Solid3D solid) {
    const auto& half = solid.box.half;
    if (!Finite(solid.box.center) || !Finite(half) || half.x <= 0 || half.y <= 0 || half.z <= 0 ||
        half.x > 100000 || half.y > 100000 || half.z > 100000 || solid.shape > Shape3D::Ramp)
        return Error{"3D solid needs finite positive dimensions and a supported shape", 1};
    m_solids.push_back(solid);
    return {};
}
bool PhysicsWorld3D::Overlaps(Box3D box, const Solid3D& solid) const {
    if (!BoxOverlap(box, solid.box)) return false;
    if (solid.shape == Shape3D::Box) return true;
    const float z = std::clamp(box.center.z + box.half.z, solid.box.center.z - solid.box.half.z,
                               solid.box.center.z + solid.box.half.z);
    const float top = solid.box.center.y - solid.box.half.y +
                      (z - solid.box.center.z + solid.box.half.z) * solid.box.half.y / solid.box.half.z;
    return box.center.y - box.half.y < top - 1e-6f;
}
Expected<void, Error> PhysicsWorld3D::Step(MotionState3D& state, Vec2 movement, bool jump, float dt,
                                           const MotionSettings3D& s, uint64_t ignore) const {
    if (auto valid = ValidateMotionSettings3D(s); !valid) return valid.GetError();
    if (!Finite(state.position) || !Finite(state.velocity) || !Finite(state.floorNormal) ||
        !std::isfinite(state.facingRadians) || std::abs(state.floorNormal.Length() - 1) > .001f ||
        !std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(movement.x) || !std::isfinite(movement.y))
        return Error{"3D motion requires finite state/input and dt in (0,1]", 1};
    auto next = state;
    const bool wasGrounded = state.grounded;
    const float floorCos = std::cos(s.floorMaxAngle * kPi / 180);
    const float length = movement.Length();
    if (length > 1) movement = movement / length;
    next.velocity.x = movement.x * s.speed;
    next.velocity.z = movement.y * s.speed;
    if (wasGrounded && jump) next.velocity.y = s.jumpSpeed;
    else if (wasGrounded && state.floorNormal.y >= floorCos) {
        const auto tangent = Slide({next.velocity.x, 0, next.velocity.z}, state.floorNormal);
        if (tangent.Length() > 1e-6f) {
            const auto desired = tangent * (movement.Length() * s.speed / tangent.Length());
            next.velocity = desired;
        } else next.velocity.y = 0;
    }
    next.velocity.y = std::max(-100.0f, next.velocity.y - s.gravity * dt);
    next.grounded = next.onWall = next.onCeiling = false;
    Box3D box{next.position + s.offset, s.half};
    for (const auto& solid : m_solids)
        if (!solid.trigger && (ignore == 0 || solid.id != ignore) && solid.shape == Shape3D::Ramp &&
            Overlaps(box, solid)) {
            const float z = std::clamp(box.center.z + box.half.z, solid.box.center.z - solid.box.half.z,
                                       solid.box.center.z + solid.box.half.z);
            const float top =
                solid.box.center.y - solid.box.half.y +
                (z - solid.box.center.z + solid.box.half.z) * solid.box.half.y / solid.box.half.z;
            const float depth = top - (box.center.y - box.half.y);
            if (depth > std::max(s.skin * 4, .02f))
                return Error{"3D character spawn intersects ramp geometry", 1};
            box.center.y += depth + s.skin;
        }
    // Recover only shallow box penetration; malformed/deep spawn intersections fail atomically.
    for (int attempt = 0; attempt < 4; ++attempt) {
        bool recovered = false;
        for (const auto& solid : m_solids) {
            if (solid.trigger || (ignore != 0 && solid.id == ignore) || solid.shape != Shape3D::Box ||
                !BoxOverlap(box, solid.box))
                continue;
            const auto delta = box.center - solid.box.center, sum = box.half + solid.box.half;
            int axis = 0;
            float depth = Axis(sum, 0) - std::abs(Axis(delta, 0));
            for (int i = 1; i < 3; ++i)
                if (Axis(sum, i) - std::abs(Axis(delta, i)) < depth) {
                    axis = i;
                    depth = Axis(sum, i) - std::abs(Axis(delta, i));
                }
            if (depth > std::max(s.skin * 4, .02f))
                return Error{"3D character spawn intersects solid geometry", 1};
            box.center =
                box.center + Basis(axis) * ((Axis(delta, axis) >= 0 ? 1.0f : -1.0f) * (depth + s.skin));
            recovered = true;
        }
        if (!recovered) break;
    }
    Vec3 remaining = next.velocity * dt;
    for (int iteration = 0; iteration < s.maxSlides && remaining.Length() > 1e-7f; ++iteration) {
        const auto hit = Cast(box, remaining, ignore);
        if (!hit) {
            box.center = box.center + remaining;
            break;
        }
        const float travel = std::max(0.0f, hit->fraction - s.skin / remaining.Length());
        box.center = box.center + remaining * travel;
        if (hit->normal.y >= floorCos) {
            next.grounded = true;
            next.floorNormal = hit->normal;
        } else if (hit->normal.y <= -floorCos) next.onCeiling = true;
        else {
            next.onWall = true;
            if (wasGrounded && !jump && s.stepHeight > 0) {
                Box3D raised = box;
                const Vec3 up{0, s.stepHeight + s.skin, 0};
                const Vec3 horizontal{remaining.x * (1 - travel), 0, remaining.z * (1 - travel)};
                if (!Cast(raised, up, ignore)) {
                    raised.center = raised.center + up;
                    if (!Cast(raised, horizontal, ignore)) {
                        raised.center = raised.center + horizontal;
                        const Vec3 down{0, -s.stepHeight - s.floorSnap - s.skin, 0};
                        if (const auto floor = Cast(raised, down, ignore);
                            floor && floor->normal.y >= floorCos) {
                            raised.center = raised.center +
                                            down * std::max(0.0f, floor->fraction - s.skin / down.Length());
                            box = raised;
                            next.grounded = true;
                            next.floorNormal = floor->normal;
                            next.onWall = false;
                            break;
                        }
                    }
                }
            }
        }
        // Idle gravity must not become horizontal drift on a walkable slope.
        if (next.grounded && movement.Length() < 1e-6f && next.velocity.y <= 0) {
            next.velocity = {};
            break;
        }
        remaining = Slide(remaining * (1 - travel), hit->normal);
        next.velocity = Slide(next.velocity, hit->normal);
    }
    if (!jump && wasGrounded && !next.grounded && next.velocity.y <= 0 && s.floorSnap > 0) {
        const Vec3 down{0, -s.floorSnap, 0};
        if (const auto hit = Cast(box, down, ignore); hit && hit->normal.y >= floorCos) {
            box.center = box.center + down * std::max(0.0f, hit->fraction - s.skin / s.floorSnap);
            next.grounded = true;
            next.floorNormal = hit->normal;
        }
    }
    if (next.grounded && next.velocity.y < 0) next.velocity.y = 0;
    next.position = box.center - s.offset;
    const auto moved = next.position - state.position;
    if (moved.x * moved.x + moved.z * moved.z > 1e-8f) next.facingRadians = std::atan2(moved.x, moved.z);
    if (!Finite(next.position) || !Finite(next.velocity)) return Error{"3D motion overflow", 1};
    state = next;
    return {};
}
} // namespace mye::phys
