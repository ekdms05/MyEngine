# mye_mmo — [게임 레이어] 아기자기한 파티 사냥 MMO(3직업·전투) (게임 위 레이어)
# 엔진이 아니라 엔진 위에 얹는 게임. engine/gameplay(Stats/Combat)를 사용한다.
add_library(mye_mmo STATIC
    include/mye/mmo/Jobs.h
    include/mye/mmo/PartyHunt.h
    include/mye/mmo/Monster.h
    include/mye/mmo/HuntingGround.h

    src/Jobs.cpp
    src/PartyHunt.cpp
    src/Monster.cpp
    src/HuntingGround.cpp
)

target_include_directories(mye_mmo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_mmo
    PUBLIC mye_core mye_gameplay
)
