#pragma once

#include "mye/asset/AssetGuid.h"
#include "mye/core/Json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mye::asset {

enum class ParamType : uint8_t { Bool, Float, Trigger };
struct AnimParam {
    std::string name;
    ParamType type = ParamType::Float;
    float value = 0.0f; // Bool/trigger use 0/1; persisted triggers start clear.
};
enum class CmpOp : uint8_t { Greater, Less, GreaterEqual, LessEqual, Equal, NotEqual, IsTrue, IsFalse };
struct AnimCondition {
    std::string param;
    CmpOp op = CmpOp::IsTrue;
    float threshold = 0.0f;
};
struct AnimTransition {
    int from = -1; // Any state. File references use names, resolved to indices on load.
    int to = 0;
    std::vector<AnimCondition> conditions; // All conditions must pass.
    bool onClipFinished = false;
    std::vector<std::string> consumeTriggers;
    bool keepPhase = true; // False restarts the destination; completion always starts fresh.
};

struct AnimationStateDefinition {
    std::string name;
    AssetRef animation; // Existing .anim, including its optional directional clips.
};

class AssetDatabase;
class VirtualFileSystem;

// Value-only .animstate document; bound clips and runtime playback never enter the file.
struct AnimationStateAsset {
    std::string name;
    int initialState = 0;
    std::vector<AnimParam> parameters;
    std::vector<AnimationStateDefinition> states;
    std::vector<AnimTransition> transitions; // Stored order is transition priority.

    Expected<void, Error> Validate() const;
    static Expected<AnimationStateAsset, Error> FromJson(const json::Value& value);
    Expected<json::Value, Error> ToJson() const;
    // Resolve GUIDs through the existing project DB/VFS and validate assigned clip files.
    static Expected<AnimationStateAsset, Error> Load(AssetGuid guid, const AssetDatabase& database,
                                                    VirtualFileSystem& files);
};

} // namespace mye::asset
