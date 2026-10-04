#pragma once

#include "mye/core/Base.h"
#include <string_view>

namespace mye::runtime {

// runtimeDirectory is an engine distribution containing MyGame, fonts and notices.
// A new destination is published only after every file has been copied successfully.
Expected<void, Error> ExportGameProject(std::string_view projectFile,
    std::string_view destination, std::string_view runtimeDirectory);

} // namespace mye::runtime
