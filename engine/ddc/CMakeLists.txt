# mye_ddc — 데이터드리븐 컴포넌트(스키마·인스턴스·레지스트리) (엔진 확장성)
# 게임이 엔진 재컴파일 없이 컴포넌트/스탯 타입을 데이터로 정의. 엔진은 범용 저장·직렬화만 제공.
add_library(mye_ddc STATIC
    include/mye/ddc/ComponentSchema.h
    include/mye/ddc/SchemaRegistry.h
    include/mye/ddc/DynamicComponentStore.h

    src/ComponentSchema.cpp
    src/SchemaRegistry.cpp
    src/DynamicComponentStore.cpp
)

target_include_directories(mye_ddc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_ddc
    PUBLIC mye_core
)
