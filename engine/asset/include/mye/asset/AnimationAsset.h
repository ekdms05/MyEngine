#pragma once

#include "mye/asset/SpriteSheet.h"
#include "mye/core/Json.h"

namespace mye::asset {

// Editable sprite animation. Runtime pointers remain outside the file contract.
struct AnimationAsset {
    SpriteSheet sheet;
    Vec2i imageSize{};
    AnimationClipData clip;
    AssetRef nextAnimation; // Optional successor after a non-looping clip finishes.

    Expected<void, Error> Validate() const;
    static Expected<AnimationAsset, Error> FromJson(const json::Value& value);
    json::Value ToJson() const;
};

} // namespace mye::asset
