# mye_asset — L2 에셋 파이프라인 (VFS·GUID·.meta·핸들·AssetManager·TextureImporter)
# 의존: mye_core(Json·Base·Log) + mye_rhi(텍스처 업로드). third_party/stb(PNG 디코드).
add_library(mye_asset STATIC
    include/mye/asset/AssetGuid.h
    include/mye/asset/FileSystem.h
    include/mye/asset/AssetMeta.h
    include/mye/asset/AssetHandle.h
    include/mye/asset/Texture.h
    include/mye/asset/Importer.h
    include/mye/asset/AsyncLoad.h
    include/mye/asset/AssetManager.h

    src/AssetGuid.cpp
    src/FileSystem.cpp
    src/AssetMeta.cpp
    src/AssetHandle.cpp
    src/TextureImporter.cpp
    src/AssetManager.cpp
)

target_include_directories(mye_asset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_asset
    PUBLIC  mye_core mye_rhi
    PRIVATE mye_thirdparty_stb
)
