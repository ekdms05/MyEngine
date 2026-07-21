# mye_render — L2 렌더러 (M1-B: 스프라이트 배처·픽셀퍼펙트 파이프라인 구현)
# 의존: mye_core(수학·메모리) + mye_rhi(디바이스·커맨드·셰이더컴파일) + mye_asset(Texture 에셋).
add_library(mye_render STATIC
    include/mye/render/Camera2D.h
    include/mye/render/SpriteBatch.h
    include/mye/render/PixelPerfectTarget.h

    src/Camera2D.cpp
    src/SpriteBatch.cpp
    src/PixelPerfectTarget.cpp
)

target_include_directories(mye_render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_render
    PUBLIC mye_core mye_rhi mye_asset
)
