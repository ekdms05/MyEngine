# mye_liveops — 라이브옵스(운영) 계층: 서버 CVar/피처플래그·메트릭·확률공개 (docs/mmorpg/09, M11)
# 윈도우 개발/서버 전제. 파일 기반(JSON) — 원격 CVar 푸시·대시보드는 후속.
add_library(mye_liveops STATIC
    include/mye/liveops/ServerConfig.h
    include/mye/liveops/GachaTable.h
    include/mye/liveops/Metrics.h

    src/ServerConfig.cpp
    src/GachaTable.cpp
    src/Metrics.cpp
)

target_include_directories(mye_liveops PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_liveops
    PUBLIC mye_core
)
