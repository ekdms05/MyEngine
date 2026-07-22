// mye/script/ScriptImporter.cpp — .lua → ScriptAsset 임포터 (M3-C 골격 스텁)
#include "mye/script/ScriptImporter.h"

#include "mye/core/Base.h"   // HashFnv1a64

#include <array>
#include <cstring>
#include <string_view>

namespace mye::script {

std::span<const std::string_view> ScriptImporter::SourceExtensions() const {
    static constexpr std::array<std::string_view, 1> kExts{ std::string_view(".lua") };
    return kExts;
}

Expected<void*, Error> ScriptImporter::Import(asset::ImportContext& ctx) {
    auto* asset = new ScriptAsset();
    asset->sourcePath = std::string(ctx.sourcePath);
    // 소스 바이트(UTF-8 .lua 원문)를 그대로 보관.
    const char* bytes = reinterpret_cast<const char*>(ctx.sourceBytes.data());
    asset->source.assign(bytes, bytes + ctx.sourceBytes.size());
    asset->sourceHash = HashFnv1a64(asset->source);
    asset->version = 1;
    return static_cast<void*>(asset);
}

void ScriptImporter::Destroy(void* assetObject) {
    delete static_cast<ScriptAsset*>(assetObject);
}

} // namespace mye::script
