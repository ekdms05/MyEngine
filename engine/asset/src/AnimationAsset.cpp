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
Expected<void, Error> ValidateClip(const AnimationClipData& clip, size_t sheetFrames) {
    if (clip.name.empty() || clip.frameIndices.empty() || clip.frameIndices.size() > 4096 ||
        clip.frameIndices.size() != clip.frameDurations.size())
        return Error{"Animation requires a name and matching frame timings", 1};
    if (static_cast<unsigned>(clip.direction) > 2 || clip.events.size() > 4096)
        return Error{"Invalid animation direction or event count", 1};
    for (size_t i = 0; i < clip.frameIndices.size(); ++i)
        if (clip.frameIndices[i] >= sheetFrames || !std::isfinite(clip.frameDurations[i]) ||
            clip.frameDurations[i] < 0.001f || clip.frameDurations[i] > 60.0f)
            return Error{"Invalid frame index or duration (0.001 to 60 seconds)", 1};
    for (const auto& event : clip.events)
        if (event.frameIndex >= clip.frameIndices.size() || event.name.empty() || !std::isfinite(event.floatArg))
            return Error{"Invalid animation event", 1};
    return {};
}

Expected<AnimationClipData, Error> ReadClip(const json::Value& value) {
    const auto* name = value.Find("name");
    const auto* loop = value.Find("loop");
    const auto* timeline = value.Find("timeline");
    int32_t direction = 0;
    if (!value.IsObject() || !name || !name->IsString() || !loop || !loop->IsBool() ||
        !Int32(value, "direction", direction) || direction < 0 || direction > 2 ||
        !timeline || !timeline->IsArray() || timeline->AsArray().size() > 4096)
        return Error{"Invalid animation clip", 1};
    AnimationClipData clip;
    clip.name = name->AsString(); clip.loop = loop->AsBool();
    clip.direction = static_cast<AnimationClipData::Direction>(direction);
    for (const auto& frame : timeline->AsArray()) {
        int32_t index = 0;
        float duration = 0;
        if (!Int32(frame, "frame", index) || index < 0 || !Float(frame, "seconds", duration))
            return Error{"Invalid animation timeline", 1};
        clip.frameIndices.push_back(static_cast<uint32_t>(index));
        clip.frameDurations.push_back(duration);
    }
    if (const auto* events = value.Find("events")) {
        if (!events->IsArray() || events->AsArray().size() > 4096) return Error{"Invalid events", 1};
        for (const auto& event : events->AsArray()) {
            int32_t frame = 0;
            float arg = 0;
            const auto* eventName = event.Find("name");
            const auto* text = event.Find("text");
            if (!Int32(event, "frame", frame) || frame < 0 || !Float(event, "value", arg) ||
                !eventName || !eventName->IsString() || !text || !text->IsString())
                return Error{"Invalid animation event", 1};
            clip.events.push_back({static_cast<uint32_t>(frame), std::string(eventName->AsString()),
                                   std::string(text->AsString()), arg});
        }
    }
    return clip;
}

json::Value::Object WriteClip(const AnimationClipData& clip) {
    using V = json::Value;
    V::Array timeline, events;
    for (size_t i = 0; i < clip.frameIndices.size(); ++i)
        timeline.emplace_back(V::Object{{"frame", V(static_cast<int64_t>(clip.frameIndices[i]))},
                                       {"seconds", V(static_cast<double>(clip.frameDurations.at(i)))}});
    for (const auto& event : clip.events)
        events.emplace_back(V::Object{{"frame", V(static_cast<int64_t>(event.frameIndex))},
            {"name", V(event.name)}, {"text", V(event.stringArg)}, {"value", V(static_cast<double>(event.floatArg))}});
    return {{"name", V(clip.name)}, {"loop", V(clip.loop)},
        {"direction", V(static_cast<int64_t>(clip.direction))},
        {"timeline", V(std::move(timeline))}, {"events", V(std::move(events))}};
}
}

Expected<void, Error> AnimationAsset::Validate() const {
    if (!sheet.texture.guid.IsValid() || imageSize.x <= 0 || imageSize.y <= 0 ||
        imageSize.x > 32768 || imageSize.y > 32768)
        return Error{"Animation requires a texture GUID and valid image size", 1};
    if (sheet.frames.empty() || sheet.frames.size() > 4096)
        return Error{"Animation requires 1 to 4096 sheet frames", 1};
    for (const auto& frame : sheet.frames) {
        const auto& r = frame.rect;
        if (r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0 ||
            static_cast<int64_t>(r.x) + r.w > imageSize.x ||
            static_cast<int64_t>(r.y) + r.h > imageSize.y ||
            !std::isfinite(frame.pivot.x) || !std::isfinite(frame.pivot.y))
            return Error{"Sprite frame extends outside its texture or has an invalid pivot", 1};
    }
    if (auto valid = ValidateClip(clip, sheet.frames.size()); !valid) return valid.GetError();
    for (size_t i = 0; i < directions.size(); ++i)
        if (directions[i])
            if (auto valid = ValidateClip(*directions[i], sheet.frames.size()); !valid)
                return Error{std::string(Dir8Suffix(static_cast<Dir8>(i))) + ": " + valid.GetError().message, 1};
    return {};
}

Expected<AnimationAsset, Error> AnimationAsset::FromJson(const json::Value& value) {
    const auto* version = Integer(value, "version");
    const auto* texture = value.Find("texture");
    const auto* frames = value.Find("frames");
    AnimationAsset out;
    if (!value.IsObject() || !version || (version->AsInt() != 1 && version->AsInt() != 2) ||
        !texture || !texture->IsString() || !Int32(value, "width", out.imageSize.x) ||
        !Int32(value, "height", out.imageSize.y) || !frames || !frames->IsArray() || frames->AsArray().size() > 4096)
        return Error{"Invalid animation file (version 1 or 2 required)", 1};
    if (out.imageSize.x <= 0 || out.imageSize.y <= 0 || out.imageSize.x > 32768 || out.imageSize.y > 32768)
        return Error{"Invalid animation image size", 1};
    auto guid = AssetGuid::FromString(texture->AsString());
    if (!guid) return guid.GetError();
    out.sheet.texture.guid = guid.Value();
    auto clip = ReadClip(value);
    if (!clip) return clip.GetError();
    out.clip = std::move(clip).Value();
    if (const auto* next = value.Find("nextAnimation")) {
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
    const auto* directions = value.Find("directions");
    const auto* mirror = value.Find("mirrorRight");
    if (version->AsInt() == 1) {
        if (directions || mirror) return Error{"Directional fields require animation version 2", 1};
    } else {
        if (!directions || !directions->IsObject() || directions->AsObject().size() > out.directions.size() ||
            !mirror || !mirror->IsBool())
            return Error{"Version 2 requires directional clips and mirrorRight", 1};
        out.mirrorRight = mirror->AsBool();
        for (const auto& [name, data] : directions->AsObject()) {
            size_t index = 0;
            while (index < out.directions.size() && name != Dir8Suffix(static_cast<Dir8>(index))) ++index;
            if (index == out.directions.size() || !data.IsObject()) return Error{"Invalid animation facing: " + name, 1};
            for (const auto& [key, unused] : data.AsObject())
                if (key != "name" && key != "loop" && key != "direction" && key != "timeline" && key != "events")
                    return Error{"Unknown directional clip field: " + key, 1};
            auto variant = ReadClip(data);
            if (!variant) return Error{name + ": " + variant.GetError().message, 1};
            out.directions[index] = std::move(variant).Value();
        }
    }
    auto valid = out.Validate();
    if (!valid) return valid.GetError();
    return out;
}

json::Value AnimationAsset::ToJson() const {
    using V = json::Value;
    V::Array frames;
    for (const auto& f : sheet.frames) {
        const Vec2 pivot = f.pivotInPixels ? f.pivot : Vec2{f.pivot.x * f.rect.w, f.pivot.y * f.rect.h};
        frames.emplace_back(V::Object{{"x", V(int64_t{f.rect.x})}, {"y", V(int64_t{f.rect.y})},
            {"w", V(int64_t{f.rect.w})}, {"h", V(int64_t{f.rect.h})},
            {"pivotX", V(double{pivot.x})}, {"pivotY", V(double{pivot.y})}});
    }
    V::Object encodedDirections;
    for (size_t i = 0; i < directions.size(); ++i)
        if (directions[i]) encodedDirections.emplace(Dir8Suffix(static_cast<Dir8>(i)), V(WriteClip(*directions[i])));
    const bool directional = !encodedDirections.empty() || mirrorRight;
    auto value = WriteClip(clip);
    value["version"] = V(int64_t{directional ? 2 : 1});
    value["texture"] = V(sheet.texture.guid.ToString());
    value["width"] = V(int64_t{imageSize.x}); value["height"] = V(int64_t{imageSize.y});
    value["frames"] = V(std::move(frames));
    if (directional) {
        value["directions"] = V(std::move(encodedDirections)); value["mirrorRight"] = V(mirrorRight);
    }
    if (nextAnimation.guid.IsValid()) value["nextAnimation"] = V(nextAnimation.guid.ToString());
    return V(std::move(value));
}
} // namespace mye::asset
