# mye_asset — L2 에셋 파이프라인 (VFS·GUID·.meta·핸들·AssetManager·TextureImporter)
# 의존: mye_core(Json·Base·Log) + mye_rhi(텍스처 업로드). third_party/stb(PNG 디코드).
add_library(mye_asset STATIC
    include/mye/asset/AssetGuid.h
    include/mye/asset/FileSystem.h
    include/mye/asset/AssetMeta.h
    include/mye/asset/AssetHandle.h
    include/mye/asset/Texture.h
    include/mye/asset/Mesh.h
    include/mye/asset/Importer.h
    include/mye/asset/MeshImporter.h
    include/mye/asset/AsyncLoad.h
    include/mye/asset/AssetManager.h
    # ---- M3-A: 핫 리로드·콘텐츠 임포터·오디오 ----
    include/mye/asset/FileWatcher.h
    include/mye/asset/AssetDatabase.h
    include/mye/asset/SpriteSheet.h
    include/mye/asset/SpriteImporter.h
    include/mye/asset/AtlasPacker.h
    include/mye/asset/AudioClip.h
    include/mye/asset/AudioImporter.h

    src/AssetGuid.cpp
    src/FileSystem.cpp
    src/AssetMeta.cpp
    src/AssetHandle.cpp
    src/TextureImporter.cpp
    src/MeshImporter.cpp
    src/AssetManager.cpp
    # ---- M3-A ----
    src/FileWatcher.cpp
    src/AssetDatabase.cpp
    src/SpriteImporter.cpp
    src/AtlasPacker.cpp
    src/AudioImporter.cpp
)

target_include_directories(mye_asset PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_asset
    PUBLIC  mye_core mye_rhi mye_reflect
    PRIVATE mye_thirdparty_stb mye_thirdparty_cgltf mye_thirdparty_stb_vorbis
)
