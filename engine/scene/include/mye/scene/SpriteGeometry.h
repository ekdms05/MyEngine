#pragma once
#include "mye/core/Math.h"
#include <array>

namespace mye::scene {
// The render and editor picking paths share the source-pixel / foot-pivot contract.
inline std::array<Vec2, 4> SpriteCorners(const Mat4& matrix, Vec2 sourcePixels, Vec2 pivotPixels, float pixelsPerUnit) {
    if (pivotPixels.x == 0 && pivotPixels.y == 0) pivotPixels = {sourcePixels.x * 0.5f, sourcePixels.y};
    const float left = -pivotPixels.x / pixelsPerUnit;
    const float top = pivotPixels.y / pixelsPerUnit;
    const float right = left + sourcePixels.x / pixelsPerUnit;
    const float bottom = top - sourcePixels.y / pixelsPerUnit;
    std::array<Vec2, 4> corners{{{left, top}, {left, bottom}, {right, top}, {right, bottom}}};
    for (auto& corner : corners) {
        const auto world = TransformPoint(Vec3{corner.x, corner.y, 0}, matrix);
        corner = {world.x, world.y};
    }
    return corners;
}
} // namespace mye::scene
