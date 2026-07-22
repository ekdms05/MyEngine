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

# M3-C: AssetManager.cpp 가 ScriptAsset(.lua) LoadSync 를 명시적 인스턴스화한다. ScriptTypes.h
#   는 헤더 온리(mye_script 링크 의존 없음)이며 mye/ecs/Entity.h(헤더 온리)만 추가로 참조한다.
#   빌드 전용 include 경로만 부여해 링크 사이클 없이 타입을 컴파일 가능하게 한다.
target_include_directories(mye_asset PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/engine/script/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/engine/scene/include>
)

target_link_libraries(mye_asset
    PUBLIC  mye_core mye_rhi mye_reflect
    PRIVATE mye_thirdparty_stb mye_thirdparty_cgltf mye_thirdparty_stb_vorbis
)
