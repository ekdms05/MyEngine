#pragma once

#include "mye/core/Base.h"
#include "mye/core/Math.h"
#include <array>
#include <optional>
#include <vector>

namespace mye::phys {
struct Box3D {
    Vec3 center{}, half{.3f, .8f, .3f};
};
enum class Shape3D : uint8_t { Box, Ramp };
struct Solid3D {
    uint64_t id = 0;
    Box3D box;
    Shape3D shape = Shape3D::Box;
    bool trigger = false;
};
struct Hit3D {
    float fraction = 1;
    Vec3 normal{};
    uint64_t id = 0;
};
struct MotionSettings3D {
    Vec3 half{.3f, .8f, .3f}, offset{0, .8f, 0};
    float speed = 3, gravity = 20, jumpSpeed = 7;
    float skin = .001f, floorSnap = .15f, stepHeight = .3f, floorMaxAngle = 45;
    int maxSlides = 6;
};
struct MotionState3D {
    Vec3 position{}, velocity{}, floorNormal{0, 1, 0};
    bool grounded = false, onWall = false, onCeiling = false;
    float facingRadians = 0;
};
Expected<void, Error> ValidateMotionSettings3D(const MotionSettings3D& settings);

// Value-only collision data shared by local play, authority and prediction.
// Boxes and +Z rising ramps are axis aligned; dynamic rigid bodies are not simulated.
class PhysicsWorld3D {
  public:
    void Clear() { m_solids.clear(); }
    Expected<void, Error> Add(Solid3D solid);
    const std::vector<Solid3D>& Solids() const { return m_solids; }
    std::optional<Hit3D> Cast(Box3D box, Vec3 motion, uint64_t ignore = 0) const;
    bool Overlaps(Box3D box, const Solid3D& solid) const;
    Expected<void, Error> Step(MotionState3D& state, Vec2 movement, bool jump, float dt,
                               const MotionSettings3D& settings, uint64_t ignore = 0) const;

  private:
    std::vector<Solid3D> m_solids;
};
} // namespace mye::phys
