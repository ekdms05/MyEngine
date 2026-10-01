#pragma once

#include "mye/core/JsonFile.h"

namespace mye::persist::detail {

// Compatibility for the persistence boundary; scene and editor use the same I/O.
using mye::Utf8Path;
using mye::Utf8String;
using mye::ReadJsonFile;
using mye::WriteJsonFile;

inline bool IntegerInRange(const json::Value& object, std::string_view key,
                           int64_t min, int64_t max, bool required = true) {
    const auto* value = object.Find(key);
    if (!value) return !required;
    return value->IsInteger() && value->AsInt() >= min && value->AsInt() <= max;
}

} // namespace mye::persist::detail
