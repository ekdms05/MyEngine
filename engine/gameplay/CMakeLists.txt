# mye_gameplay — L4 게임플레이 시스템 (스탯·전투·인벤·루트·AI·퀘스트) (docs/mmorpg/04, M8)
# 데이터드리븐 + 순수 로직(GPU 무관). 의존: mye_core(수학) + mye_scene(ECS·Transform).
add_library(mye_gameplay STATIC
    include/mye/gameplay/Stats.h
    include/mye/gameplay/StatSystem.h
    include/mye/gameplay/Combat.h

    src/StatSystem.cpp
    src/Combat.cpp
)

target_include_directories(mye_gameplay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_gameplay
    PUBLIC mye_core mye_scene
)
