#include "mye/editor/Viewport.h"
#include "mye/render/Camera2D.h"
#include <algorithm>
#include <cmath>

namespace mye::editor {
render::HybridViewInfo BuildViewportView(const ViewportCamera& camera, uint32_t width, uint32_t height) {
    width = std::max(width, 1u); height = std::max(height, 1u);
    render::Camera2DDesc desc;
    desc.position = camera.center; desc.zoom = camera.zoom; desc.pixelSnap = true;
    desc.viewportWidth = width; desc.viewportHeight = height;
    auto view = render::HybridRenderer::MakeViewInfo(render::Camera2D(desc));
    if (!camera.perspective) return view;
    const float distance = 16.0f / std::clamp(camera.zoom, .1f, 16.0f);
    const float yaw = std::clamp(camera.yaw, -1.2f, 1.2f), pitch = std::clamp(camera.pitch, -1.2f, 1.2f);
    const Vec3 target{camera.center.x, camera.center.y, 0};
    const Vec3 eye = target + Vec3{std::sin(yaw) * std::cos(pitch) * distance, std::sin(pitch) * distance, -std::cos(yaw) * std::cos(pitch) * distance};
    view.geometryDepth = true;
    view.viewportWidth = width; view.viewportHeight = height;
    view.view = Mat4::LookAtLH(eye, target, {0,1,0});
    view.proj = Mat4::PerspectiveLH(.75f, static_cast<float>(width) / height, .05f, 1000.0f);
    view.viewProj = view.view * view.proj;
    return view;
}
Vec2 ProjectViewportPoint(const ViewportCamera& camera, Vec3 world, uint32_t width, uint32_t height) {
    const auto view = BuildViewportView(camera, width, height);
    const auto clip = Vec4{world.x, world.y, world.z, 1} * view.viewProj;
    if (std::abs(clip.w) < 1e-6f) return {};
    return {(clip.x / clip.w + 1) * width * .5f, (1 - clip.y / clip.w) * height * .5f};
}
Vec2 ViewportPointOnPlane(const ViewportCamera& camera, Vec2 pixel, uint32_t width, uint32_t height) {
    const auto view = BuildViewportView(camera, width, height);
    const auto inverse = view.viewProj.Inverse();
    const float x = pixel.x / std::max(width, 1u) * 2 - 1, y = 1 - pixel.y / std::max(height, 1u) * 2;
    auto nearPoint = Vec4{x,y,0,1} * inverse, farPoint = Vec4{x,y,1,1} * inverse;
    if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f) return camera.center;
    const Vec3 a{nearPoint.x / nearPoint.w, nearPoint.y / nearPoint.w, nearPoint.z / nearPoint.w};
    const Vec3 b{farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w};
    const auto ray = b - a;
    if (std::abs(ray.z) < 1e-6f) return camera.center;
    const auto point = a + ray * (-a.z / ray.z);
    return {point.x, point.y};
}
} // namespace mye::editor
