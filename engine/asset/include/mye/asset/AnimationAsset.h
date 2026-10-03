#pragma once

#include "mye/asset/SpriteSheet.h"
#include "mye/core/Json.h"
#include <array>
#include <optional>

namespace mye::asset {

// Editable sprite animation. Runtime pointers remain outside the file contract.
struct AnimationAsset {
    SpriteSheet sheet;
    Vec2i imageSize{};
    AnimationClipData clip;
    std::array<std::optional<AnimationClipData>, static_cast<size_t>(Dir8::Count)> directions;
    bool mirrorRight = false;
    AssetRef nextAnimation; // Optional successor after a non-looping clip finishes.

    struct ResolvedClip { const AnimationClipData* clip; bool flipX; };
    ResolvedClip Resolve(Dir8 facing) const {
        const auto index = static_cast<size_t>(facing);
        if (index >= directions.size()) return {&clip, false};
        if (directions[index]) return {&*directions[index], false};
        const auto mirrored = ResolveDir(facing, mirrorRight);
        const auto& variant = directions[static_cast<size_t>(mirrored.clipDir)];
        return variant ? ResolvedClip{&*variant, mirrored.flipX} : ResolvedClip{&clip, false};
    }

    Expected<void, Error> Validate() const;
    static Expected<AnimationAsset, Error> FromJson(const json::Value& value);
    json::Value ToJson() const;
};

} // namespace mye::asset
