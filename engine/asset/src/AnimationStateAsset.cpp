#include "mye/asset/AnimationStateAsset.h"

#include "mye/asset/AnimationAsset.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace mye::asset {
namespace {
constexpr size_t kMaxStates = 64, kMaxParameters = 64, kMaxTransitions = 256, kMaxConditions = 16;
constexpr std::array<std::string_view, 3> kTypes{"bool", "float", "trigger"};
constexpr std::array<std::string_view, 8> kOperations{
    "greater", "less", "greater_equal", "less_equal", "equal", "not_equal", "is_true", "is_false"};

bool ValidName(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name != "*" &&
        std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool Fields(const json::Value& value, std::initializer_list<std::string_view> allowed) {
    if (!value.IsObject()) return false;
    for (const auto& [key, unused] : value.AsObject())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
    return true;
}
const json::Value* Array(const json::Value& value, std::string_view key, size_t limit) {
    const auto* field = value.Find(key);
    return field && field->IsArray() && field->AsArray().size() <= limit ? field : nullptr;
}
int StateIndex(const std::vector<AnimationStateDefinition>& states, std::string_view name) {
    for (size_t i = 0; i < states.size(); ++i) if (states[i].name == name) return static_cast<int>(i);
    return -1;
}
Expected<json::Value, Error> ReadJson(VirtualFileSystem& files, const std::string& path) {
    try {
        auto bytes = files.ReadAll(path);
        if (!bytes) return bytes.GetError();
        const auto& blob = bytes.Value();
        return json::Parse(std::string_view(reinterpret_cast<const char*>(blob.data()), blob.size()));
    } catch (const std::system_error& error) {
        return Error{"Animation state file: " + std::string(error.what()), error.code().value()};
    }
}
}

Expected<void, Error> AnimationStateAsset::Validate() const {
    if (!ValidName(name) || states.empty() || states.size() > kMaxStates ||
        parameters.size() > kMaxParameters || transitions.size() > kMaxTransitions ||
        initialState < 0 || initialState >= static_cast<int>(states.size()))
        return Error{"Invalid animation state name, counts or initial state", 1};
    std::set<std::string_view> stateNames, parameterNames;
    for (const auto& state : states)
        if (!ValidName(state.name) || !stateNames.insert(state.name).second || !state.animation.guid.IsValid())
            return Error{"Animation states require unique names and assigned animation GUIDs", 1};
    for (const auto& param : parameters)
        if (!ValidName(param.name) || !parameterNames.insert(param.name).second ||
            static_cast<size_t>(param.type) >= kTypes.size() || !std::isfinite(param.value) ||
            (param.type == ParamType::Bool && param.value != 0 && param.value != 1) ||
            (param.type == ParamType::Trigger && param.value != 0))
            return Error{"Invalid animation parameter name, type or default", 1};
    for (const auto& transition : transitions) {
        if (transition.from < -1 || transition.from >= static_cast<int>(states.size()) ||
            transition.to < 0 || transition.to >= static_cast<int>(states.size()) || transition.from == transition.to ||
            transition.conditions.size() > kMaxConditions || transition.consumeTriggers.size() > kMaxConditions)
            return Error{"Invalid animation transition endpoints or condition counts", 1};
        for (const auto& condition : transition.conditions) {
            const auto param = std::find_if(parameters.begin(), parameters.end(), [&](const auto& p) { return p.name == condition.param; });
            if (param == parameters.end() || static_cast<size_t>(condition.op) >= kOperations.size() ||
                !std::isfinite(condition.threshold)) return Error{"Unknown animation condition parameter or operation", 1};
            const bool logical = condition.op == CmpOp::IsTrue || condition.op == CmpOp::IsFalse;
            if ((logical && param->type == ParamType::Float) || (!logical && param->type != ParamType::Float) ||
                (logical && condition.threshold != 0))
                return Error{"Animation condition operation does not match its parameter type", 1};
        }
        std::set<std::string_view> consumed;
        for (const auto& nameToConsume : transition.consumeTriggers) {
            const auto param = std::find_if(parameters.begin(), parameters.end(), [&](const auto& p) { return p.name == nameToConsume; });
            if (param == parameters.end() || param->type != ParamType::Trigger || !consumed.insert(nameToConsume).second)
                return Error{"Consumed animation triggers must be declared triggers without duplicates", 1};
        }
    }
    return {};
}

Expected<AnimationStateAsset, Error> AnimationStateAsset::FromJson(const json::Value& value) {
    const auto* version = value.Find("version");
    const auto* name = value.Find("name");
    const auto* initial = value.Find("initialState");
    const auto* parameters = Array(value, "parameters", kMaxParameters);
    const auto* states = Array(value, "states", kMaxStates);
    const auto* transitions = Array(value, "transitions", kMaxTransitions);
    if (!Fields(value, {"version", "name", "initialState", "parameters", "states", "transitions"}) ||
        !version || !version->IsInteger() || version->AsInt() != 1 || !name || !name->IsString() ||
        !initial || !initial->IsString() || !parameters || !states || !transitions)
        return Error{"Invalid animation state file (version 1 required)", 1};
    AnimationStateAsset out;
    out.name = name->AsString();
    for (const auto& state : states->AsArray()) {
        const auto* stateName = state.Find("name");
        const auto* animation = state.Find("animation");
        if (!Fields(state, {"name", "animation"}) || !stateName || !stateName->IsString() || !animation || !animation->IsString())
            return Error{"Invalid animation state definition", 1};
        auto guid = AssetGuid::FromString(animation->AsString());
        if (!guid) return guid.GetError();
        out.states.push_back({std::string(stateName->AsString()), AssetRef{guid.Value()}});
    }
    out.initialState = StateIndex(out.states, initial->AsString());
    for (const auto& parameter : parameters->AsArray()) {
        const auto* parameterName = parameter.Find("name");
        const auto* type = parameter.Find("type");
        const auto* defaultValue = parameter.Find("default");
        if (!Fields(parameter, {"name", "type", "default"}) || !parameterName || !parameterName->IsString() || !type || !type->IsString() || !defaultValue)
            return Error{"Invalid animation parameter", 1};
        const auto found = std::find(kTypes.begin(), kTypes.end(), type->AsString());
        if (found == kTypes.end()) return Error{"Unknown animation parameter type", 1};
        const auto parameterType = static_cast<ParamType>(found - kTypes.begin());
        if (parameterType == ParamType::Float ? !defaultValue->IsNumber() : !defaultValue->IsBool())
            return Error{"Animation parameter default does not match its type", 1};
        out.parameters.push_back({std::string(parameterName->AsString()), parameterType,
            parameterType == ParamType::Float ? static_cast<float>(defaultValue->AsDouble()) : (defaultValue->AsBool() ? 1.0f : 0.0f)});
    }
    for (const auto& entry : transitions->AsArray()) {
        const auto* from = entry.Find("from");
        const auto* to = entry.Find("to");
        const auto* finished = entry.Find("onClipFinished");
        const auto* phase = entry.Find("keepPhase");
        const auto* conditions = Array(entry, "conditions", kMaxConditions);
        const auto* consume = Array(entry, "consumeTriggers", kMaxConditions);
        if (!Fields(entry, {"from", "to", "onClipFinished", "keepPhase", "conditions", "consumeTriggers"}) ||
            !from || !from->IsString() || !to || !to->IsString() || !finished || !finished->IsBool() ||
            !phase || !phase->IsBool() || !conditions || !consume)
            return Error{"Invalid animation transition", 1};
        AnimTransition transition;
        transition.from = from->AsString() == "*" ? -1 : StateIndex(out.states, from->AsString());
        transition.to = StateIndex(out.states, to->AsString());
        if (transition.from < 0 && from->AsString() != "*") return Error{"Unknown animation transition source", 1};
        transition.onClipFinished = finished->AsBool(); transition.keepPhase = phase->AsBool();
        for (const auto& entryCondition : conditions->AsArray()) {
            const auto* param = entryCondition.Find("param");
            const auto* op = entryCondition.Find("op");
            if (!param || !param->IsString() || !op || !op->IsString()) return Error{"Invalid animation condition", 1};
            const auto found = std::find(kOperations.begin(), kOperations.end(), op->AsString());
            if (found == kOperations.end()) return Error{"Unknown animation condition operation", 1};
            AnimCondition condition{std::string(param->AsString()), static_cast<CmpOp>(found - kOperations.begin()), 0};
            const bool logical = condition.op == CmpOp::IsTrue || condition.op == CmpOp::IsFalse;
            if (!Fields(entryCondition, logical ? std::initializer_list<std::string_view>{"param", "op"} :
                                                std::initializer_list<std::string_view>{"param", "op", "value"}))
                return Error{"Unknown animation condition field", 1};
            if (!logical) {
                const auto* threshold = entryCondition.Find("value");
                if (!threshold || !threshold->IsNumber()) return Error{"Numeric animation condition requires value", 1};
                condition.threshold = static_cast<float>(threshold->AsDouble());
            }
            transition.conditions.push_back(std::move(condition));
        }
        for (const auto& trigger : consume->AsArray()) {
            if (!trigger.IsString()) return Error{"Invalid consumed animation trigger", 1};
            transition.consumeTriggers.emplace_back(trigger.AsString());
        }
        out.transitions.push_back(std::move(transition));
    }
    auto valid = out.Validate();
    if (!valid) return valid.GetError();
    return out;
}

Expected<json::Value, Error> AnimationStateAsset::ToJson() const {
    auto valid = Validate();
    if (!valid) return valid.GetError();
    using V = json::Value;
    V::Array encodedStates, encodedParameters, encodedTransitions;
    for (const auto& state : states)
        encodedStates.emplace_back(V::Object{{"name", V(state.name)}, {"animation", V(state.animation.guid.ToString())}});
    for (const auto& param : parameters)
        encodedParameters.emplace_back(V::Object{{"name", V(param.name)}, {"type", V(std::string(kTypes[static_cast<size_t>(param.type)]))},
            {"default", param.type == ParamType::Float ? V(double{param.value}) : V(param.value != 0)}});
    for (const auto& transition : transitions) {
        V::Array conditions, consumed;
        for (const auto& condition : transition.conditions) {
            V::Object fields{{"param", V(condition.param)}, {"op", V(std::string(kOperations[static_cast<size_t>(condition.op)]))}};
            if (condition.op != CmpOp::IsTrue && condition.op != CmpOp::IsFalse) fields["value"] = V(double{condition.threshold});
            conditions.emplace_back(std::move(fields));
        }
        for (const auto& trigger : transition.consumeTriggers) consumed.emplace_back(trigger);
        encodedTransitions.emplace_back(V::Object{
            {"from", V(transition.from < 0 ? std::string("*") : states[static_cast<size_t>(transition.from)].name)},
            {"to", V(states[static_cast<size_t>(transition.to)].name)}, {"onClipFinished", V(transition.onClipFinished)},
            {"keepPhase", V(transition.keepPhase)}, {"conditions", V(std::move(conditions))}, {"consumeTriggers", V(std::move(consumed))}});
    }
    return V(V::Object{{"version", V(int64_t{1})}, {"name", V(name)}, {"initialState", V(states[static_cast<size_t>(initialState)].name)},
        {"parameters", V(std::move(encodedParameters))}, {"states", V(std::move(encodedStates))}, {"transitions", V(std::move(encodedTransitions))}});
}

Expected<AnimationStateAsset, Error> AnimationStateAsset::Load(AssetGuid guid, const AssetDatabase& database,
                                                             VirtualFileSystem& files) {
    const auto path = database.PathFromGuid(guid);
    if (!guid.IsValid() || !path.starts_with("assets://") || !path.ends_with(".animstate"))
        return Error{"Animation state GUID does not identify an .animstate file", 1};
    auto json = ReadJson(files, path);
    if (!json) return json.GetError();
    auto loaded = FromJson(json.Value());
    if (!loaded) return loaded.GetError();
    // ponytail: up to 64 document references checked at load; reuse a bound clip cache only in the app.
    for (const auto& state : loaded.Value().states) {
        const auto clipPath = database.PathFromGuid(state.animation.guid);
        if (!clipPath.starts_with("assets://") || !clipPath.ends_with(".anim"))
            return Error{state.name + ": animation GUID does not identify an .anim file", 1};
        auto clipJson = ReadJson(files, clipPath);
        if (!clipJson) return Error{state.name + ": " + clipJson.GetError().message, 1};
        auto clip = AnimationAsset::FromJson(clipJson.Value());
        if (!clip) return Error{state.name + ": " + clip.GetError().message, 1};
    }
    return loaded;
}
} // namespace mye::asset
