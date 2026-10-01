#pragma once
#include "mye/core/Math.h"
#include "mye/scene/Renderable.h"
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

inline std::array<Vec3, 4> BillboardCorners(const Mat4& matrix, const Mat4& view,
    BillboardMode mode, Vec2 sourcePixels, Vec2 pivotPixels, float pixelsPerUnit) {
    if (mode == BillboardMode::None) return SpriteCorners3D(matrix, sourcePixels, pivotPixels, pixelsPerUnit);
    Vec3 right{view.m[0][0], view.m[1][0], view.m[2][0]};
    Vec3 up{view.m[0][1], view.m[1][1], view.m[2][1]};
    if (mode == BillboardMode::YAxis) {
        right.y = 0;
        if (Vec3::Dot(right, right) < 1e-8f) right = {1, 0, 0};
        right = right.Normalized(); up = {0, 1, 0};
    }
    const Vec3 x{matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]};
    const Vec3 y{matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]};
    const Vec3 z{matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]};
    // Facing belongs to presentation; retain scale/reflection and the foot anchor.
    Mat4 facing = Mat4::Identity();
    right = right * (x.Length() * (Vec3::Dot(Vec3::Cross(x, y), z) < 0 ? -1.0f : 1.0f));
    up = up * y.Length();
    for (unsigned i = 0; i < 3; ++i) facing.m[3][i] = matrix.m[3][i];
    facing.m[0][0] = right.x; facing.m[0][1] = right.y; facing.m[0][2] = right.z;
    facing.m[1][0] = up.x; facing.m[1][1] = up.y; facing.m[1][2] = up.z;
    return SpriteCorners3D(facing, sourcePixels, pivotPixels, pixelsPerUnit);
}
} // namespace mye::scene
