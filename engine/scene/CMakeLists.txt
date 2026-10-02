# mye_scene — L3 씬·ECS·월드 (ECS 코어·Transform·스케줄러·CommandBuffer)
# 의존: mye_core(수학·이벤트·모듈·잡·시간) + mye_asset(AssetRef·비동기 핸들) + mye_render(렌더 추출 타입).
add_library(mye_physics2d STATIC src/phys/Collision.cpp src/phys/Motion2D.cpp)
target_include_directories(mye_physics2d PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(mye_physics2d PUBLIC mye_core)

add_library(mye_physics3d STATIC src/phys/PhysicsWorld3D.cpp)
target_include_directories(mye_physics3d PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(mye_physics3d PUBLIC mye_core)

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
    include/mye/scene/SceneSerializer.h
    include/mye/scene/SceneReflection.h
    include/mye/scene/Camera3D.h

    # ---- 렌더러블·정렬 추출 ----
    include/mye/scene/Renderable.h
    include/mye/scene/RenderExtract.h

    # ---- 스프라이트 애니메이션·8방향·상태머신·이벤트(03) ----
    include/mye/anim/AnimationTypes.h
    include/mye/anim/ClipPlayback.h
    include/mye/anim/SpriteAnimator.h
    include/mye/anim/AnimationSystem.h

    # ---- 타일맵(높이·경사·다리) ----
    include/mye/tilemap/Tilemap.h

    # ---- 길찾기(그리드 A*·높이·다리 인지·비동기) ----
    include/mye/nav/Pathfinding.h

    # ---- 충돌·물리(경량 2D) (03) ----
    include/mye/phys/Collision.h
    include/mye/phys/SpatialHash.h
    include/mye/phys/PhysicsWorld2D.h

    # ---- 구현 ----
    src/ecs/ComponentPool.cpp
    src/ecs/World.cpp
    src/ecs/CommandBuffer.cpp
    src/scene/Transform.cpp
    src/scene/SystemScheduler.cpp
    src/scene/SceneModule.cpp
    src/scene/SceneSerializer.cpp
    src/scene/SceneReflection.cpp
    src/scene/Camera3D.cpp
    src/scene/RenderExtract.cpp
    src/scene/Particle.cpp
    src/anim/AnimationSystem.cpp
    src/tilemap/Tilemap.cpp
    src/nav/Pathfinding.cpp
    src/phys/SpatialHash.cpp
    src/phys/PhysicsWorld2D.cpp
    src/phys/PhysicsComponents3D.cpp
)

target_include_directories(mye_scene PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_scene
    PUBLIC  mye_core mye_asset mye_render mye_physics2d mye_physics3d
    PRIVATE mye_reflect   # SceneSerializer: 리플렉션·JsonArchive(ser) 소비
)
