#pragma once

#include "mye/core/Json.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <windows.h>

namespace mye::persist::detail {

constexpr size_t kMaxJsonFileBytes = 64 * 1024 * 1024;

inline std::filesystem::path Utf8Path(std::string_view path) {
    return std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

inline std::string Utf8String(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

inline Expected<json::Value, Error> ReadJsonFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxJsonFileBytes)
        return Error{"ReadJsonFile: inaccessible or oversized file '" + path.string() + "'", 1};
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"ReadJsonFile: open failed '" + path.string() + "'", 1};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return Error{"ReadJsonFile: read failed '" + path.string() + "'", 2};
    return json::Parse(text);
}

// One writer per data directory. Flush the complete replacement before publishing it.
inline Expected<void, Error> WriteJsonFile(const std::filesystem::path& path, const json::Value& value) {
    const std::string text = json::Stringify(value);
    if (text.size() > kMaxJsonFileBytes)
        return Error{"WriteJsonFile: snapshot exceeds 64 MiB limit", 1};
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    HANDLE raw = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        return Error{"WriteJsonFile: open failed '" + tmp.string() + "'", static_cast<int32_t>(GetLastError())};
    const auto close = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(close)> file(raw, close);
    DWORD written = 0;
    if (text.size() > MAXDWORD ||
        !WriteFile(raw, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) ||
        written != text.size() || !FlushFileBuffers(raw))
        return Error{"WriteJsonFile: write/flush failed '" + tmp.string() + "'", static_cast<int32_t>(GetLastError())};
    file.reset();
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return Error{"WriteJsonFile: replace failed '" + path.string() + "'", static_cast<int32_t>(GetLastError())};
    return {};
}

inline bool IntegerInRange(const json::Value& object, std::string_view key,
                           int64_t min, int64_t max, bool required = true) {
    const auto* value = object.Find(key);
    if (!value) return !required;
    return value->IsInteger() && value->AsInt() >= min && value->AsInt() <= max;
}

} // namespace mye::persist::detail
