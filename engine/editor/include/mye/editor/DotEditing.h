#pragma once

#include "mye/editor/Command.h"
#include "mye/asset/AnimationAsset.h"
#include "mye/core/Json.h"

#include <filesystem>
#include <memory>

namespace mye::editor {

// Straight RGBA bytes packed R | G<<8 | B<<16 | A<<24; independent of ImGui.
struct DotImage {
    int width = 32, height = 32;
    std::vector<uint32_t> pixels = std::vector<uint32_t>(32 * 32);
};
struct DotFrame { DotImage image; float seconds = .15f; };
struct DotPose { Vec2 position{}; float degrees = 0; };
struct DotBone {
    std::string name;
    int parent = -1; // Parents precede children; cycles cannot be represented.
    RectInt region{};
    Vec2 pivot{}; // Pixel coordinates within the source region.
    DotPose rest;
};
struct DotPoseKey { int frame = 0, bone = 0; DotPose pose; };
struct DotMotion {
    std::string name = "idle";
    int first = 0, last = 0;
    bool loop = true;
    asset::AnimationClipData::Direction direction = asset::AnimationClipData::Direction::Forward;
    std::string next;
    std::vector<DotPoseKey> keys;
};
struct DotDocument {
    std::vector<DotFrame> frames{DotFrame{}};
    // Immutable imported pixels are shared by Undo snapshots, rather than copied each stroke.
    std::shared_ptr<const DotImage> reference;
    std::vector<DotBone> bones;
    std::vector<DotMotion> motions{DotMotion{}};

    Expected<void, Error> Validate() const;
    json::Value ToJson() const;
    static Expected<DotDocument, Error> FromJson(const json::Value& value);
};

class DotEditCommand final : public IEditorCommand {
public:
    DotEditCommand(DotDocument& target, DotDocument before, DotDocument after, std::string label)
        : m_target(target), m_before(std::move(before)), m_after(std::move(after)), m_label(std::move(label)) {}
    void Execute(EditorContext&) override { m_target = m_after; }
    void Undo(EditorContext&) override { m_target = m_before; }
    std::string_view Label() const override { return m_label; }
private:
    // ponytail: bounded documents use snapshots; switch to pixel diffs if stroke history becomes a measured bottleneck.
    DotDocument& m_target;
    DotDocument m_before, m_after;
    std::string m_label;
};

struct DotCanvasView {
    Vec2 origin{};
    float scale = 8;
    Vec2 PixelAt(Vec2 screen) const;
    void ZoomAt(Vec2 screen, float scaleAfter);
    void Fit(Vec2 available, Vec2i imageSize);
};

Expected<DotImage, Error> LoadDotImage(const std::filesystem::path& path);
DotImage ResampleDotImage(const DotImage& image, int width, int height);
void PaintDotLine(DotImage& image, Vec2i from, Vec2i to, uint32_t color);
void FillDotRegion(DotImage& image, Vec2i pixel, uint32_t color);
DotPose EvaluateDotPose(const DotDocument& data, int motion, int bone, float frame);
std::vector<DotPose> DotWorldPoses(const DotDocument& data, int motion, float frame, bool rest);
DotImage RenderDotFrame(const DotDocument& data, int motion, int frame, bool rest = false);
asset::AnimationClipData DotPlaybackClip(const DotDocument& data, int motion);
Expected<void, Error> RemoveDotFrame(DotDocument& data, int frame);
Expected<void, Error> AddDotBone(DotDocument& data, RectInt region, int parent);

struct DotExport {
    std::filesystem::path directory;
    std::vector<asset::AssetRef> animations; // Same order as source motions.
};
// A fresh GUID directory avoids overwriting any existing/user-authored assets.
Expected<DotExport, Error> ExportDotMotions(const DotDocument& data, const std::filesystem::path& projectRoot);

} // namespace mye::editor
