# mye_render — L2 렌더러 (M1-B: 스프라이트 배처·픽셀퍼펙트 / M2-C: 하이브리드 깊이 렌더러)
# 의존: mye_core(수학·메모리) + mye_rhi(디바이스·커맨드·셰이더컴파일) + mye_asset(Texture 에셋).
# M2-C: 렌더 데이터 계약(RenderExtract.h·Tilemap.h)은 mye_scene 헤더로 소비한다. 단, scene은 이미
#   render를 링크하므로(추출 스텁) 링크 사이클을 피하려고 include 경로만 참조하고 링크 edge는 만들지
#   않는다. HybridRenderer.cpp가 참조하는 scene 심볼(TilemapWorld::Layer 등)은 최종 실행/테스트
#   타깃이 두 정적 라이브러리를 함께 링크할 때 해소된다.
add_library(mye_render STATIC
    include/mye/render/Camera2D.h
    include/mye/render/SpriteBatch.h
    include/mye/render/SpriteRenderer.h
    include/mye/render/PixelPerfectTarget.h
    include/mye/render/DepthEncoder.h
    include/mye/render/MeshTypes.h
    include/mye/render/HybridRenderer.h

    src/Camera2D.cpp
    src/SpriteBatch.cpp
    src/SpriteRenderer.cpp
    src/PixelPerfectTarget.cpp
    src/DepthEncoder.cpp
    src/HybridRenderer.cpp
)

target_include_directories(mye_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# 렌더 데이터 계약 헤더(scene) — 컴파일 전용 include 경로(링크 edge 없음, 사이클 회피).
target_include_directories(mye_render PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../scene/include>
)

target_link_libraries(mye_render
    PUBLIC mye_core mye_rhi mye_asset
)
