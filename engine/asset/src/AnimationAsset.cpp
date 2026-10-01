#include "mye/asset/AnimationAsset.h"

#include <cmath>
#include <limits>

namespace mye::asset {
namespace {
const json::Value* Integer(const json::Value& v, const char* key) {
    const auto* field = v.Find(key);
    return field && field->IsInteger() ? field : nullptr;
}
bool Int32(const json::Value& v, const char* key, int32_t& out) {
    const auto* field = Integer(v, key);
    if (!field || field->AsInt() < std::numeric_limits<int32_t>::min() ||
        field->AsInt() > std::numeric_limits<int32_t>::max()) return false;
    out = static_cast<int32_t>(field->AsInt());
    return true;
}
bool Float(const json::Value& v, const char* key, float& out) {
    const auto* field = v.Find(key);
    if (!field || !field->IsNumber()) return false;
    out = static_cast<float>(field->AsDouble());
    return std::isfinite(out);
}
}

Expected<void, Error> AnimationAsset::Validate() const {
    if (!sheet.texture.guid.IsValid() || imageSize.x <= 0 || imageSize.y <= 0 ||
        imageSize.x > 32768 || imageSize.y > 32768)
        return Error{"Animation requires a texture GUID and valid image size", 1};
    if (sheet.frames.empty() || sheet.frames.size() > 4096 || clip.name.empty() ||
        clip.frameIndices.empty() || clip.frameIndices.size() > 4096 ||
        clip.frameIndices.size() != clip.frameDurations.size())
        return Error{"Animation requires a name, sheet frames and matching frame timings", 1};
    if (static_cast<unsigned>(clip.direction) > 2 || clip.events.size() > 4096)
        return Error{"Invalid animation direction or event count", 1};
    for (const auto& frame : sheet.frames) {
        const auto& r = frame.rect;
        if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 ||
            static_cast<int64_t>(r.x) + r.w > imageSize.x ||
            static_cast<int64_t>(r.y) + r.h > imageSize.y ||
            !std::isfinite(frame.pivot.x) || !std::isfinite(frame.pivot.y))
            return Error{"Sprite frame extends outside its texture or has an invalid pivot", 1};
    }
    for (size_t i = 0; i < clip.frameIndices.size(); ++i)
        if (clip.frameIndices[i] >= sheet.frames.size() || !std::isfinite(clip.frameDurations[i]) ||
            clip.frameDurations[i] < 0.001f || clip.frameDurations[i] > 60.0f)
            return Error{"Invalid frame index or duration (0.001 to 60 seconds)", 1};
    for (const auto& event : clip.events)
        if (event.frameIndex >= clip.frameIndices.size() || event.name.empty() || !std::isfinite(event.floatArg))
            return Error{"Invalid animation event", 1};
    return {};
}

Expected<AnimationAsset, Error> AnimationAsset::FromJson(const json::Value& v) {
    const auto* version = Integer(v, "version");
    const auto* texture = v.Find("texture");
    const auto* frames = v.Find("frames");
    const auto* timeline = v.Find("timeline");
    const auto* name = v.Find("name");
    const auto* loop = v.Find("loop");
    AnimationAsset out;
    int32_t direction = 0;
    if (!v.IsObject() || !version || version->AsInt() != 1 || !texture || !texture->IsString() ||
        !Int32(v, "width", out.imageSize.x) || !Int32(v, "height", out.imageSize.y) ||
        !Int32(v, "direction", direction) || direction < 0 || direction > 2 ||
        !frames || !frames->IsArray() || frames->AsArray().size() > 4096 ||
        !timeline || !timeline->IsArray() || timeline->AsArray().size() > 4096 ||
        !name || !name->IsString() || !loop || !loop->IsBool())
        return Error{"Invalid animation file (version 1 required)", 1};
    if (out.imageSize.x <= 0 || out.imageSize.y <= 0 || out.imageSize.x > 32768 || out.imageSize.y > 32768)
        return Error{"Invalid animation image size", 1};
    auto guid = AssetGuid::FromString(texture->AsString());
    if (!guid) return guid.GetError();
    out.sheet.texture.guid = guid.Value();
    out.clip.name = name->AsString();
    out.clip.loop = loop->AsBool();
    out.clip.direction = static_cast<AnimationClipData::Direction>(direction);
    if (const auto* next = v.Find("nextAnimation")) {
        if (!next->IsString()) return Error{"Invalid next animation GUID", 1};
        auto nextGuid = AssetGuid::FromString(next->AsString());
        if (!nextGuid || !nextGuid.Value().IsValid()) return Error{"Invalid next animation GUID", 1};
        out.nextAnimation.guid = nextGuid.Value();
    }
    for (const auto& f : frames->AsArray()) {
        SpriteFrame frame;
        if (!Int32(f, "x", frame.rect.x) || !Int32(f, "y", frame.rect.y) ||
            !Int32(f, "w", frame.rect.w) || !Int32(f, "h", frame.rect.h) ||
            !Float(f, "pivotX", frame.pivot.x) || !Float(f, "pivotY", frame.pivot.y))
            return Error{"Invalid sprite frame", 1};
        frame.pivotInPixels = true;
        frame.uv = {static_cast<float>(frame.rect.x) / out.imageSize.x,
                    static_cast<float>(frame.rect.y) / out.imageSize.y,
                    static_cast<float>(frame.rect.w) / out.imageSize.x,
                    static_cast<float>(frame.rect.h) / out.imageSize.y};
        out.sheet.frames.push_back(frame);
    }
    for (const auto& f : timeline->AsArray()) {
        int32_t index = 0;
        float duration = 0;
        if (!Int32(f, "frame", index) || index < 0 || !Float(f, "seconds", duration))
            return Error{"Invalid animation timeline", 1};
        out.clip.frameIndices.push_back(static_cast<uint32_t>(index));
        out.clip.frameDurations.push_back(duration);
    }
    if (const auto* events = v.Find("events")) {
        if (!events->IsArray() || events->AsArray().size() > 4096) return Error{"Invalid events", 1};
        for (const auto& e : events->AsArray()) {
            int32_t frame = 0;
            float arg = 0;
            const auto* eventName = e.Find("name");
            const auto* text = e.Find("text");
            if (!Int32(e, "frame", frame) || frame < 0 || !Float(e, "value", arg) ||
                !eventName || !eventName->IsString() || !text || !text->IsString())
                return Error{"Invalid animation event", 1};
            out.clip.events.push_back({static_cast<uint32_t>(frame), std::string(eventName->AsString()),
                                      std::string(text->AsString()), arg});
        }
    }
    auto valid = out.Validate();
    if (!valid) return valid.GetError();
    return out;
}

json::Value AnimationAsset::ToJson() const {
    using V = json::Value;
    V::Array frames, timeline, events;
    for (const auto& f : sheet.frames) {
        const Vec2 pivot = f.pivotInPixels ? f.pivot : Vec2{f.pivot.x * f.rect.w, f.pivot.y * f.rect.h};
        frames.emplace_back(V::Object{{"x", V(int64_t{f.rect.x})}, {"y", V(int64_t{f.rect.y})},
            {"w", V(int64_t{f.rect.w})}, {"h", V(int64_t{f.rect.h})},
            {"pivotX", V(double{pivot.x})}, {"pivotY", V(double{pivot.y})}});
    }
    for (size_t i = 0; i < clip.frameIndices.size(); ++i)
        timeline.emplace_back(V::Object{{"frame", V(static_cast<int64_t>(clip.frameIndices[i]))},
                                       {"seconds", V(static_cast<double>(clip.frameDurations.at(i)))}});
    for (const auto& e : clip.events)
        events.emplace_back(V::Object{{"frame", V(static_cast<int64_t>(e.frameIndex))},
            {"name", V(e.name)}, {"text", V(e.stringArg)}, {"value", V(static_cast<double>(e.floatArg))}});
    V::Object value{{"version", V(int64_t{1})}, {"texture", V(sheet.texture.guid.ToString())},
        {"width", V(int64_t{imageSize.x})}, {"height", V(int64_t{imageSize.y})}, {"name", V(clip.name)},
        {"loop", V(clip.loop)}, {"direction", V(static_cast<int64_t>(clip.direction))},
        {"frames", V(std::move(frames))}, {"timeline", V(std::move(timeline))}, {"events", V(std::move(events))}};
    if (nextAnimation.guid.IsValid()) value["nextAnimation"] = V(nextAnimation.guid.ToString());
    return V(std::move(value));
}
} // namespace mye::asset
