#include "mye/core/JsonFile.h"

#include <Windows.h>

#include <fstream>
#include <iterator>
#include <memory>

namespace mye {
namespace {
constexpr std::size_t kMaxJsonFileBytes = 64 * 1024 * 1024;
}

Expected<json::Value, Error> ReadJsonFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxJsonFileBytes)
        return Error{"ReadJsonFile: inaccessible or oversized file '" + Utf8String(path) + "'", 1};
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"ReadJsonFile: open failed '" + Utf8String(path) + "'", 1};
    std::string text(static_cast<std::size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!in || in.peek() != std::char_traits<char>::eof())
        return Error{"ReadJsonFile: read failed or file grew beyond limit", 2};
    if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
    return json::Parse(text);
}

Expected<void, Error> WriteJsonFile(const std::filesystem::path& path, const json::Value& value) {
    const std::string text = json::Stringify(value);
    if (text.size() > kMaxJsonFileBytes)
        return Error{"WriteJsonFile: snapshot exceeds 64 MiB limit", 1};

    // One writer per file. CREATE_NEW never truncates a pre-existing staging file.
    auto tmp = path;
    tmp += ".tmp";
    HANDLE raw = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE)
        return Error{"WriteJsonFile: staging file unavailable '" + Utf8String(tmp) + "'",
                     static_cast<int32_t>(GetLastError())};
    const auto close = [](void* handle) { CloseHandle(handle); };
    std::unique_ptr<void, decltype(close)> file(raw, close);
    DWORD written = 0;
    const bool complete = WriteFile(raw, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                          written == text.size() && FlushFileBuffers(raw);
    const DWORD writeError = complete ? ERROR_SUCCESS : GetLastError();
    file.reset();
    if (!complete) {
        DeleteFileW(tmp.c_str());
        return Error{"WriteJsonFile: write/flush failed", static_cast<int32_t>(writeError)};
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD replaceError = GetLastError();
        DeleteFileW(tmp.c_str());
        return Error{"WriteJsonFile: replace failed '" + Utf8String(path) + "'",
                     static_cast<int32_t>(replaceError)};
    }
    return {};
}

} // namespace mye
