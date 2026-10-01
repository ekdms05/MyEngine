#pragma once
#include "mye/core/Math.h"
#include <array>

namespace mye::scene {
// The render and editor picking paths share the source-pixel / foot-pivot contract.
inline std::array<Vec3, 4> SpriteCorners3D(const Mat4& matrix, Vec2 sourcePixels, Vec2 pivotPixels, float pixelsPerUnit) {
    if (pivotPixels.x == 0 && pivotPixels.y == 0) pivotPixels = {sourcePixels.x * 0.5f, sourcePixels.y};
    const float left = -pivotPixels.x / pixelsPerUnit;
    const float top = pivotPixels.y / pixelsPerUnit;
    const float right = left + sourcePixels.x / pixelsPerUnit;
    const float bottom = top - sourcePixels.y / pixelsPerUnit;
    std::array<Vec3, 4> corners{{{left, top, 0}, {left, bottom, 0}, {right, top, 0}, {right, bottom, 0}}};
    for (auto& corner : corners) {
        const auto world = TransformPoint(Vec3{corner.x, corner.y, 0}, matrix);
        corner = world;
    }
    return corners;
}
inline std::array<Vec2, 4> SpriteCorners(const Mat4& matrix, Vec2 sourcePixels, Vec2 pivotPixels, float pixelsPerUnit) {
    const auto world = SpriteCorners3D(matrix, sourcePixels, pivotPixels, pixelsPerUnit);
    std::array<Vec2, 4> corners;
    for (std::size_t i = 0; i < corners.size(); ++i) corners[i] = {world[i].x, world[i].y};
    return corners;
}
} // namespace mye::scene
