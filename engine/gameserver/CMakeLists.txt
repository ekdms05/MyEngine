# mye_gameserver — 게임플레이↔영속(↔넷) 통합 세션 관리 (M8·M10 통합)
add_library(mye_gameserver STATIC
    include/mye/gameserver/GameServer.h

    src/GameServer.cpp
)

target_include_directories(mye_gameserver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_gameserver
    PUBLIC mye_core mye_gameplay mye_persist
)
