#include "mye/editor/DotEditing.h"
#include "mye/asset/Importer.h"
#include "mye/asset/AssetMeta.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Log.h"
#include "stb/stb_image.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace mye::editor {
namespace {
constexpr size_t kPixelLimit = 4 * 1024 * 1024;
constexpr float kRadians = 3.14159265358979323846f / 180;
bool ValidImage(const DotImage& image, int maxSide) {
    return image.width > 0 && image.height > 0 && image.width <= maxSide && image.height <= maxSide &&
        size_t(image.width) * image.height <= kPixelLimit && image.pixels.size() == size_t(image.width) * image.height;
}
bool ValidPose(const DotPose& pose) {
    return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) && std::isfinite(pose.degrees) &&
        std::abs(pose.position.x) <= 100000 && std::abs(pose.position.y) <= 100000 && std::abs(pose.degrees) <= 36000;
}
Vec2 Rotate(Vec2 p, float degrees) {
    const float c = std::cos(degrees * kRadians), s = std::sin(degrees * kRadians);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}
bool Number(const json::Value* v, double low, double high, bool integer = false) {
    return v && v->IsNumber() && std::isfinite(v->AsDouble()) && v->AsDouble() >= low && v->AsDouble() <= high &&
        (!integer || std::trunc(v->AsDouble()) == v->AsDouble());
}
json::Value PoseJson(const DotPose& p) {
    return json::Value(json::Value::Array{json::Value(double(p.position.x)), json::Value(double(p.position.y)), json::Value(double(p.degrees))});
}
Expected<DotPose, Error> ParsePose(const json::Value* v) {
    if (!v || !v->IsArray() || v->AsArray().size() != 3) return Error{"Invalid dot pose", 1};
    const auto& a = v->AsArray();
    if (!Number(&a[0], -100000, 100000) || !Number(&a[1], -100000, 100000) || !Number(&a[2], -36000, 36000))
        return Error{"Dot pose is outside the supported range", 1};
    return DotPose{{float(a[0].AsDouble()), float(a[1].AsDouble())}, float(a[2].AsDouble())};
}
json::Value ImageJson(const DotImage& image) {
    constexpr char hex[] = "0123456789abcdef";
    std::string pixels(image.pixels.size() * 8, '0');
    for (size_t i = 0; i < image.pixels.size(); ++i)
        for (int channel = 0; channel < 4; ++channel) {
            const auto byte = (image.pixels[i] >> (channel * 8)) & 255;
            pixels[i * 8 + channel * 2] = hex[byte >> 4];
            pixels[i * 8 + channel * 2 + 1] = hex[byte & 15];
        }
    return json::Value(json::Value::Object{{"width", json::Value(int64_t(image.width))},
        {"height", json::Value(int64_t(image.height))}, {"rgba", json::Value(std::move(pixels))}});
}
Expected<DotImage, Error> ParseImage(const json::Value* v, int maxSide) {
    if (!v || !v->IsObject() || !Number(v->Find("width"), 1, maxSide, true) || !Number(v->Find("height"), 1, maxSide, true))
        return Error{"Invalid dot image dimensions", 1};
    DotImage image; image.width = int(v->Find("width")->AsInt()); image.height = int(v->Find("height")->AsInt());
    const size_t count = size_t(image.width) * image.height;
    const auto* rgba = v->Find("rgba");
    if (count > kPixelLimit || !rgba || !rgba->IsString() || rgba->AsString().size() != count * 8)
        return Error{"Invalid dot pixel buffer", 1};
    auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    image.pixels.assign(count, 0);
    for (size_t i = 0; i < count; ++i) for (int channel = 0; channel < 4; ++channel) {
        const int hi = digit(rgba->AsString()[i * 8 + channel * 2]), lo = digit(rgba->AsString()[i * 8 + channel * 2 + 1]);
        if (hi < 0 || lo < 0) return Error{"Invalid dot RGBA hex", 1};
        image.pixels[i] |= uint32_t(hi * 16 + lo) << (channel * 8);
    }
    return image;
}
uint32_t Over(uint32_t src, uint32_t dst) {
    const uint32_t sa = src >> 24, da = dst >> 24, alpha = sa + (da * (255 - sa) + 127) / 255;
    if (!alpha) return 0;
    uint32_t out = alpha << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t color = (((src >> shift) & 255) * sa + (((dst >> shift) & 255) * da * (255 - sa) + 127) / 255 + alpha / 2) / alpha;
        out |= std::min(color, 255u) << shift;
    }
    return out;
}
Expected<void, Error> WritePng(const std::filesystem::path& path, const DotImage& image) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return Error{"Cannot create PNG: " + Utf8String(path), 1};
    std::vector<uint8_t> bytes(image.pixels.size() * 4);
    for (size_t i = 0; i < image.pixels.size(); ++i)
        for (int c = 0; c < 4; ++c) bytes[i * 4 + c] = uint8_t(image.pixels[i] >> (c * 8));
    const int ok = stbi_write_png_to_func([](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    }, &out, image.width, image.height, 4, bytes.data(), image.width * 4);
    out.flush();
    if (!ok || !out) return Error{"PNG encoding/write failed: " + Utf8String(path), 1};
    out.close();
    if (!out) return Error{"PNG close failed: " + Utf8String(path), 1};
    return {};
}
}

Expected<void, Error> DotDocument::Validate() const {
    if (frames.empty() || frames.size() > 64 || motions.empty() || motions.size() > 32 || bones.size() > 64)
        return Error{"Dot documents support 1–64 frames, 1–32 motions and up to 64 bones", 1};
    size_t pixels = 0;
    for (const auto& frame : frames) {
        if (!ValidImage(frame.image, 512) || frame.image.width != frames[0].image.width || frame.image.height != frames[0].image.height ||
            !std::isfinite(frame.seconds) || frame.seconds < .01f || frame.seconds > 10)
            return Error{"Frames must share a 1–512 px canvas and a 0.01–10 second duration", 1};
        pixels += frame.image.pixels.size();
    }
    if (pixels > kPixelLimit || (reference && !ValidImage(*reference, 4096)) ||
        pixels + (reference ? reference->pixels.size() : 0) > 6 * 1024 * 1024)
        return Error{"Dot image pixel limit exceeded (4M frame pixels, 6M including reference)", 1};
    std::set<std::string> names;
    for (size_t i = 0; i < bones.size(); ++i) {
        const auto& b = bones[i]; const auto& r = b.region;
        if (b.name.empty() || b.name.size() > 128 || !names.insert(b.name).second || b.parent < -1 || b.parent >= int(i) ||
            !ValidPose(b.rest) || !std::isfinite(b.pivot.x) || !std::isfinite(b.pivot.y) || b.pivot.x < 0 || b.pivot.y < 0 ||
            b.pivot.x > r.w || b.pivot.y > r.h || r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 ||
            int64_t(r.x) + r.w > frames[0].image.width || int64_t(r.y) + r.h > frames[0].image.height)
            return Error{"Invalid bone hierarchy, pivot or part region", 1};
    }
    names.clear();
    for (const auto& m : motions) if (m.name.empty() || m.name.size() > 128 || !names.insert(m.name).second)
        return Error{"Motion names must be nonempty and unique", 1};
    for (const auto& m : motions) {
        if (m.first < 0 || m.last < m.first || m.last >= int(frames.size()) || int(m.direction) > 2 ||
            (!m.next.empty() && !names.contains(m.next)) || m.keys.size() > frames.size() * bones.size())
            return Error{"Invalid motion range, next motion or pose keys", 1};
        std::set<std::pair<int, int>> slots;
        for (const auto& key : m.keys) if (key.frame < m.first || key.frame > m.last || key.bone < 0 || key.bone >= int(bones.size()) ||
            !ValidPose(key.pose) || !slots.emplace(key.frame, key.bone).second) return Error{"Invalid/duplicate pose key", 1};
    }
    return {};
}

json::Value DotDocument::ToJson() const {
    json::Value::Array frameValues, boneValues, motionValues;
    for (const auto& frame : frames) frameValues.emplace_back(json::Value::Object{
        {"image", ImageJson(frame.image)}, {"seconds", json::Value(double(frame.seconds))}});
    for (const auto& b : bones) boneValues.emplace_back(json::Value::Object{
        {"name", json::Value(b.name)}, {"parent", json::Value(int64_t(b.parent))}, {"rest", PoseJson(b.rest)},
        {"region", json::Value(json::Value::Array{json::Value(int64_t(b.region.x)), json::Value(int64_t(b.region.y)), json::Value(int64_t(b.region.w)), json::Value(int64_t(b.region.h))})},
        {"pivot", json::Value(json::Value::Array{json::Value(double(b.pivot.x)), json::Value(double(b.pivot.y))})}});
    for (const auto& m : motions) {
        json::Value::Array keys;
        for (const auto& k : m.keys) keys.emplace_back(json::Value::Object{{"frame", json::Value(int64_t(k.frame))}, {"bone", json::Value(int64_t(k.bone))}, {"pose", PoseJson(k.pose)}});
        motionValues.emplace_back(json::Value::Object{{"name", json::Value(m.name)}, {"first", json::Value(int64_t(m.first))}, {"last", json::Value(int64_t(m.last))},
            {"loop", json::Value(m.loop)}, {"direction", json::Value(int64_t(m.direction))}, {"next", json::Value(m.next)}, {"keys", json::Value(std::move(keys))}});
    }
    return json::Value(json::Value::Object{{"version", json::Value(int64_t{1})}, {"frames", json::Value(std::move(frameValues))},
        {"reference", reference ? ImageJson(*reference) : json::Value{}}, {"bones", json::Value(std::move(boneValues))}, {"motions", json::Value(std::move(motionValues))}});
}

Expected<DotDocument, Error> DotDocument::FromJson(const json::Value& v) {
    const auto* frames = v.Find("frames"); const auto* bones = v.Find("bones"); const auto* motions = v.Find("motions");
    if (!v.IsObject() || !Number(v.Find("version"), 1, 1, true) || !frames || !frames->IsArray() || frames->AsArray().empty() || frames->AsArray().size() > 64 ||
        !bones || !bones->IsArray() || bones->AsArray().size() > 64 || !motions || !motions->IsArray() || motions->AsArray().empty() || motions->AsArray().size() > 32)
        return Error{"Invalid dot document version or collections", 1};
    DotDocument out; out.frames.clear(); out.motions.clear();
    size_t count = 0;
    for (const auto& f : frames->AsArray()) {
        if (!Number(f.Find("seconds"), .009999, 10)) return Error{"Invalid frame duration", 1};
        auto image = ParseImage(f.Find("image"), 512); if (!image) return image.GetError();
        count += image.Value().pixels.size(); if (count > kPixelLimit) return Error{"Dot frame budget exceeded", 1};
        out.frames.push_back({std::move(image).Value(), float(f.Find("seconds")->AsDouble())});
    }
    const auto* reference = v.Find("reference");
    if (!reference) return Error{"Missing dot reference field", 1};
    if (!reference->IsNull()) {
        auto image = ParseImage(reference, 4096); if (!image) return image.GetError();
        out.reference = std::make_shared<const DotImage>(std::move(image).Value());
    }
    for (const auto& b : bones->AsArray()) {
        const auto* name = b.Find("name"); const auto* region = b.Find("region"); const auto* pivot = b.Find("pivot");
        auto pose = ParsePose(b.Find("rest"));
        if (!name || !name->IsString() || !Number(b.Find("parent"), -1, 63, true) || !pose || !region || !region->IsArray() || region->AsArray().size() != 4 ||
            !pivot || !pivot->IsArray() || pivot->AsArray().size() != 2) return Error{"Invalid dot bone", 1};
        for (const auto& n : region->AsArray()) if (!Number(&n, 0, 512, true)) return Error{"Invalid bone region", 1};
        for (const auto& n : pivot->AsArray()) if (!Number(&n, 0, 512)) return Error{"Invalid bone pivot", 1};
        const auto& r = region->AsArray(); const auto& p = pivot->AsArray();
        out.bones.push_back({std::string(name->AsString()), int(b.Find("parent")->AsInt()), {int(r[0].AsInt()), int(r[1].AsInt()), int(r[2].AsInt()), int(r[3].AsInt())},
            {float(p[0].AsDouble()), float(p[1].AsDouble())}, pose.Value()});
    }
    for (const auto& m : motions->AsArray()) {
        const auto* name = m.Find("name"); const auto* next = m.Find("next"); const auto* keys = m.Find("keys"); const auto* loop = m.Find("loop");
        if (!name || !name->IsString() || !next || !next->IsString() || !loop || !loop->IsBool() ||
            !Number(m.Find("first"), 0, 63, true) || !Number(m.Find("last"), 0, 63, true) || !Number(m.Find("direction"), 0, 2, true) ||
            !keys || !keys->IsArray() || keys->AsArray().size() > out.frames.size() * out.bones.size()) return Error{"Invalid dot motion", 1};
        DotMotion motion{std::string(name->AsString()), int(m.Find("first")->AsInt()), int(m.Find("last")->AsInt()), loop->AsBool(),
            static_cast<asset::AnimationClipData::Direction>(m.Find("direction")->AsInt()), std::string(next->AsString()), {}};
        for (const auto& k : keys->AsArray()) {
            auto pose = ParsePose(k.Find("pose"));
            if (!Number(k.Find("frame"), 0, 63, true) || !Number(k.Find("bone"), 0, 63, true) || !pose) return Error{"Invalid dot key", 1};
            motion.keys.push_back({int(k.Find("frame")->AsInt()), int(k.Find("bone")->AsInt()), pose.Value()});
        }
        out.motions.push_back(std::move(motion));
    }
    auto valid = out.Validate(); if (!valid) return valid.GetError();
    return out;
}

Vec2 DotCanvasView::PixelAt(Vec2 screen) const { return (screen - origin) / scale; }
void DotCanvasView::ZoomAt(Vec2 screen, float scaleAfter) {
    const auto pixel = PixelAt(screen);
    scale = std::clamp(scaleAfter, .25f, 64.f);
    origin = screen - pixel * scale;
}

Expected<DotImage, Error> LoadDotImage(const std::filesystem::path& path) {
    std::error_code ec; const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > 64 * 1024 * 1024 || path.extension() != ".png") return Error{"Choose a readable PNG under 64 MiB", 1};
    std::ifstream file(path, std::ios::binary); std::vector<std::byte> bytes(size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return Error{"Cannot read PNG", 1};
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h, &channels) ||
        w <= 0 || h <= 0 || w > 4096 || h > 4096 || size_t(w) * h > kPixelLimit) return Error{"Reference supports up to 4096 px per side and 4,194,304 pixels", 1};
    auto decoded = asset::TextureImporter::DecodePng(bytes, false); if (!decoded) return decoded.GetError();
    DotImage image; image.width = w; image.height = h; image.pixels.assign(size_t(w) * h, 0);
    for (size_t i = 0; i < image.pixels.size(); ++i) for (int c = 0; c < 4; ++c)
        image.pixels[i] |= uint32_t(decoded.Value().pixels[i * 4 + c]) << (c * 8);
    return image;
}
DotImage ResampleDotImage(const DotImage& image, int width, int height) {
    DotImage out; out.width = width; out.height = height; out.pixels.assign(size_t(width) * height, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        out.pixels[size_t(y) * width + x] = image.pixels[size_t(y * image.height / height) * image.width + x * image.width / width];
    return out;
}
void PaintDotLine(DotImage& image, Vec2i from, Vec2i to, uint32_t color) {
    int x = from.x, y = from.y, dx = std::abs(to.x - x), dy = -std::abs(to.y - y);
    const int sx = x < to.x ? 1 : -1, sy = y < to.y ? 1 : -1; int error = dx + dy;
    while (true) {
        if (x >= 0 && y >= 0 && x < image.width && y < image.height) image.pixels[size_t(y) * image.width + x] = color;
        if (x == to.x && y == to.y) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
void DotCanvasView::Fit(Vec2 available, Vec2i imageSize) {
    const float fit = std::min(available.x / imageSize.x, available.y / imageSize.y);
    scale = std::clamp(fit >= 1 ? std::floor(fit) : fit, .25f, 64.f);
    origin = {(available.x - imageSize.x * scale) * .5f, (available.y - imageSize.y * scale) * .5f};
}

void FillDotRegion(DotImage& image, Vec2i p, uint32_t color) {
    if (p.x < 0 || p.y < 0 || p.x >= image.width || p.y >= image.height) return;
    const auto old = image.pixels[size_t(p.y) * image.width + p.x]; if (old == color) return;
    std::vector<Vec2i> pending{p}; image.pixels[size_t(p.y) * image.width + p.x] = color;
    while (!pending.empty()) {
        const auto pixel = pending.back(); pending.pop_back();
        const Vec2i neighbors[] = {{pixel.x - 1, pixel.y}, {pixel.x + 1, pixel.y}, {pixel.x, pixel.y - 1}, {pixel.x, pixel.y + 1}};
        for (const auto q : neighbors) if (q.x >= 0 && q.y >= 0 && q.x < image.width && q.y < image.height) {
            auto& target = image.pixels[size_t(q.y) * image.width + q.x];
            if (target == old) { target = color; pending.push_back(q); }
        }
    }
}
DotPose EvaluateDotPose(const DotDocument& data, int motion, int bone, float frame) {
    const auto& rest = data.bones[bone].rest;
    const DotPoseKey* before = nullptr; const DotPoseKey* after = nullptr;
    for (const auto& key : data.motions[motion].keys) if (key.bone == bone) {
        if (key.frame <= frame && (!before || key.frame > before->frame)) before = &key;
        if (key.frame >= frame && (!after || key.frame < after->frame)) after = &key;
    }
    if (!before) return after ? after->pose : rest;
    if (!after || before == after) return before->pose;
    const float t = (frame - before->frame) / (after->frame - before->frame);
    return {before->pose.position + (after->pose.position - before->pose.position) * t,
        before->pose.degrees + std::remainder(after->pose.degrees - before->pose.degrees, 360.f) * t};
}
std::vector<DotPose> DotWorldPoses(const DotDocument& data, int motion, float frame, bool rest) {
    std::vector<DotPose> out; out.reserve(data.bones.size());
    for (size_t i = 0; i < data.bones.size(); ++i) {
        auto pose = rest ? data.bones[i].rest : EvaluateDotPose(data, motion, int(i), frame);
        if (data.bones[i].parent >= 0) {
            const auto& parent = out[data.bones[i].parent];
            pose.position = parent.position + Rotate(pose.position, parent.degrees); pose.degrees += parent.degrees;
        }
        out.push_back(pose);
    }
    return out;
}
DotImage RenderDotFrame(const DotDocument& data, int motion, int frame, bool rest) {
    const auto& source = data.frames[frame].image; auto out = source;
    const auto poses = DotWorldPoses(data, motion, float(frame), rest);
    for (const auto& bone : data.bones) for (int y = bone.region.y; y < bone.region.y + bone.region.h; ++y)
        std::fill_n(out.pixels.begin() + size_t(y) * out.width + bone.region.x, bone.region.w, 0u);
    for (size_t i = 0; i < data.bones.size(); ++i) {
        const auto& bone = data.bones[i]; const auto& pose = poses[i]; const auto& r = bone.region;
        if (r.w == 0 || r.h == 0) continue;
        Vec2 low{1000000, 1000000}, high{-1000000, -1000000};
        const Vec2 corners[] = {{0,0}, {float(r.w),0}, {0,float(r.h)}, {float(r.w),float(r.h)}};
        for (auto corner : corners) {
            const auto point = pose.position + Rotate(corner - bone.pivot, pose.degrees);
            low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y);
        }
        const int x0 = std::clamp(int(std::floor(low.x)), 0, out.width), y0 = std::clamp(int(std::floor(low.y)), 0, out.height);
        const int x1 = std::clamp(int(std::ceil(high.x)), 0, out.width), y1 = std::clamp(int(std::ceil(high.y)), 0, out.height);
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const auto p = Rotate(Vec2{x + .5f, y + .5f} - pose.position, -pose.degrees) + bone.pivot;
            const int sx = int(std::floor(p.x)), sy = int(std::floor(p.y));
            if (sx >= 0 && sy >= 0 && sx < r.w && sy < r.h) {
                auto& pixel = out.pixels[size_t(y) * out.width + x]; pixel = Over(source.pixels[size_t(r.y + sy) * source.width + r.x + sx], pixel);
            }
        }
    }
    return out;
}
asset::AnimationClipData DotPlaybackClip(const DotDocument& data, int motion) {
    const auto& m = data.motions[motion]; asset::AnimationClipData clip;
    clip.name = m.name; clip.loop = m.loop && m.next.empty(); clip.direction = m.direction;
    for (int i = m.first; i <= m.last; ++i) { clip.frameIndices.push_back(uint32_t(i)); clip.frameDurations.push_back(data.frames[i].seconds); }
    return clip;
}
Expected<void, Error> RemoveDotFrame(DotDocument& data, int frame) {
    if (frame < 0 || frame >= int(data.frames.size()) || data.frames.size() == 1) return Error{"Keep at least one frame", 1};
    data.frames.erase(data.frames.begin() + frame);
    for (auto& m : data.motions) {
        m.first = std::min(m.first - (m.first > frame), int(data.frames.size()) - 1);
        m.last = std::max(m.first, std::min(m.last - (m.last >= frame), int(data.frames.size()) - 1));
        std::erase_if(m.keys, [&](const auto& k) { return k.frame == frame; });
        for (auto& k : m.keys) if (k.frame > frame) --k.frame;
    }
    return {};
}
Expected<void, Error> AddDotBone(DotDocument& data, RectInt region, int parent) {
    if (data.bones.size() >= 64 || parent < -1 || parent >= int(data.bones.size()) || region.x < 0 || region.y < 0 || region.w <= 0 || region.h <= 0 ||
        int64_t(region.x) + region.w > data.frames[0].image.width || int64_t(region.y) + region.h > data.frames[0].image.height)
        return Error{"Select a part inside the canvas", 1};
    DotBone bone; bone.name = "bone_" + std::to_string(data.bones.size()); bone.parent = parent; bone.region = region;
    while (std::any_of(data.bones.begin(), data.bones.end(), [&](const auto& b) { return b.name == bone.name; })) bone.name += "_";
    bone.pivot = {region.w * .5f, region.h * .5f}; bone.rest.position = Vec2{float(region.x), float(region.y)} + bone.pivot;
    if (parent >= 0) {
        const auto pose = DotWorldPoses(data, 0, 0, true)[parent];
        bone.rest.position = Rotate(bone.rest.position - pose.position, -pose.degrees); bone.rest.degrees = -pose.degrees;
    }
    data.bones.push_back(std::move(bone)); return {};
}

Expected<DotExport, Error> ExportDotMotions(const DotDocument& data, const std::filesystem::path& projectRoot) {
    auto valid = data.Validate(); if (!valid) return valid.GetError();
    std::error_code ec; const auto root = std::filesystem::weakly_canonical(projectRoot / "assets", ec);
    if (ec || !std::filesystem::is_directory(root, ec)) return Error{"Open a project with an assets directory first", 1};
    const auto directory = root / ("dot-" + asset::AssetGuid::Generate().ToString());
    if (!std::filesystem::create_directory(directory, ec) || ec) return Error{"Cannot create a fresh dot export directory", 1};
    struct ExportFiles {
        std::filesystem::path directory;
        std::vector<std::filesystem::path> paths;
        bool committed = false;
        ~ExportFiles() {
            if (committed) return;
            std::error_code cleanupError;
            for (const auto& path : paths) {
                std::filesystem::remove(path, cleanupError);
                if (cleanupError) MYE_LOG_WARN("Editor", "Could not remove failed export file {}: {}", Utf8String(path), cleanupError.message());
            }
            std::filesystem::remove(directory, cleanupError); // Removes only an empty, freshly created directory.
            if (cleanupError) MYE_LOG_WARN("Editor", "Could not remove failed export directory: {}", cleanupError.message());
        }
    } files{directory, {}, false};
    DotExport result; result.directory = directory;
    std::vector<asset::AssetMeta> animationMetas;
    for (size_t i = 0; i < data.motions.size(); ++i) {
        animationMetas.push_back(asset::AssetMeta::CreateFor("AnimationAsset", 1));
        result.animations.push_back({animationMetas.back().guid, asset::AnimationClipData::kAssetTypeId});
    }
    for (size_t m = 0; m < data.motions.size(); ++m) {
        const auto& motion = data.motions[m]; const int count = motion.last - motion.first + 1;
        const int columns = int(std::ceil(std::sqrt(float(count)))), rows = (count + columns - 1) / columns;
        DotImage sheet; sheet.width = data.frames[0].image.width * columns; sheet.height = data.frames[0].image.height * rows;
        sheet.pixels.assign(size_t(sheet.width) * sheet.height, 0);
        auto meta = asset::AssetMeta::CreateFor("TextureImporter", 1);
        asset::AnimationAsset animation; animation.imageSize = {sheet.width, sheet.height}; animation.sheet.texture.guid = meta.guid;
        animation.sheet.frameSize = {data.frames[0].image.width, data.frames[0].image.height}; animation.clip = DotPlaybackClip(data, int(m));
        if (!motion.next.empty()) {
            const auto next = std::find_if(data.motions.begin(), data.motions.end(), [&](const auto& clip) { return clip.name == motion.next; });
            animation.nextAnimation = result.animations[static_cast<size_t>(next - data.motions.begin())];
        }
        for (int i = 0; i < count; ++i) {
            const auto image = RenderDotFrame(data, int(m), motion.first + i);
            const int x = i % columns * image.width, y = i / columns * image.height;
            for (int row = 0; row < image.height; ++row) std::copy_n(image.pixels.begin() + size_t(row) * image.width, image.width, sheet.pixels.begin() + size_t(y + row) * sheet.width + x);
            asset::SpriteFrame sprite; sprite.rect = {x, y, image.width, image.height}; sprite.pivot = {.5f, 1.f};
            sprite.uv = {float(x) / sheet.width, float(y) / sheet.height, float(image.width) / sheet.width, float(image.height) / sheet.height};
            animation.sheet.frames.push_back(sprite); animation.clip.frameIndices[i] = uint32_t(i);
        }
        auto clipValid = animation.Validate(); if (!clipValid) return clipValid.GetError();
        const auto png = directory / ("motion-" + std::to_string(m) + ".png");
        const auto pngMeta = Utf8Path(asset::MetaPathFor(Utf8String(png)));
        files.paths.push_back(png); files.paths.push_back(pngMeta);
        auto written = WritePng(png, sheet); if (!written) return written.GetError();
        auto jsonMeta = json::Parse(meta.Stringify()); if (!jsonMeta) return jsonMeta.GetError();
        written = WriteJsonFile(pngMeta, jsonMeta.Value()); if (!written) return written.GetError();
        const auto anim = directory / ("motion-" + std::to_string(m) + ".anim");
        const auto animMetaPath = Utf8Path(asset::MetaPathFor(Utf8String(anim)));
        files.paths.push_back(anim); files.paths.push_back(animMetaPath);
        written = WriteJsonFile(anim, animation.ToJson()); if (!written) return written.GetError();
        const auto& animMeta = animationMetas[m];
        jsonMeta = json::Parse(animMeta.Stringify()); if (!jsonMeta) return jsonMeta.GetError();
        written = WriteJsonFile(animMetaPath, jsonMeta.Value()); if (!written) return written.GetError();
    }
    files.committed = true;
    return result;
}
} // namespace mye::editor
