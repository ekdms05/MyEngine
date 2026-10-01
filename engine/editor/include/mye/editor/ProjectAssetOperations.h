#pragma once

#include "mye/core/Base.h"

namespace mye::editor {
class ProjectContext;

// Paths are relative to <project>/assets. Import validates before copying and never replaces a file.
Expected<void, Error> ImportProjectAsset(std::string_view projectRoot, std::string_view sourcePath,
                                       std::string_view assetsRelativePath);
Expected<void, Error> CreateProjectAssetFolder(std::string_view projectRoot, std::string_view assetsRelativePath);
Expected<void, Error> CheckProjectAssetDeletion(const ProjectContext& project, std::string_view assetsRelativePath);
Expected<void, Error> RecycleProjectAsset(const ProjectContext& project, std::string_view assetsRelativePath,
                                        void* nativeWindow = nullptr);
Expected<void, Error> RevealProjectAssetFolder(std::string_view projectRoot, std::string_view assetsRelativeFolder);
} // namespace mye::editor
