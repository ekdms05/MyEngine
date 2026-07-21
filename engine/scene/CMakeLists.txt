# mye_scene — L3 씬·ECS·월드 (M2-A 골격: ECS 코어·Transform·스케줄러·CommandBuffer)
# 의존: mye_core(수학·이벤트·모듈·잡·시간) + mye_asset(AssetRef·비동기 핸들) + mye_render(추출 대상, 스텁).
# 타일맵·충돌·렌더러블 추출·하이브리드 렌더는 M2-B/M2-C — 여기서는 헤더 스텁만.
add_library(mye_scene STATIC
    # ---- ECS 코어 ----
    include/mye/ecs/Entity.h
    include/mye/ecs/ComponentType.h
    include/mye/ecs/ComponentPool.h
    include/mye/ecs/World.h
    include/mye/ecs/View.h
    include/mye/ecs/View.inl
    include/mye/ecs/CommandBuffer.h

    # ---- Transform·스케줄러·모듈 ----
    include/mye/scene/Transform.h
    include/mye/scene/SystemScheduler.h
    include/mye/scene/SceneModule.h
    include/mye/scene/Scene.h

    # ---- 렌더러블·정렬(추출 스텁) ----
    include/mye/scene/Renderable.h
    include/mye/scene/RenderExtract.h

    # ---- 스텁(후속 워크플로우 M2-B/M2-C) ----
    include/mye/tilemap/Tilemap.h
    include/mye/phys/Collision.h

    # ---- 구현 ----
    src/ecs/ComponentPool.cpp
    src/ecs/World.cpp
    src/ecs/CommandBuffer.cpp
    src/scene/Transform.cpp
    src/scene/SystemScheduler.cpp
    src/scene/SceneModule.cpp
)

target_include_directories(mye_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_scene
    PUBLIC mye_core mye_asset mye_render
)
