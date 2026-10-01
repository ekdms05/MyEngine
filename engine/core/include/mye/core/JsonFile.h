#pragma once

#include "mye/core/Json.h"

#include <filesystem>

namespace mye {

// std::filesystem's narrow Windows paths use the system code page, not UTF-8.
inline std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

inline std::string Utf8String(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// A bounded read and a flushed, atomic replacement. Parent directories must exist.
Expected<json::Value, Error> ReadJsonFile(const std::filesystem::path& path);
Expected<void, Error> WriteJsonFile(const std::filesystem::path& path, const json::Value& value);

} // namespace mye
