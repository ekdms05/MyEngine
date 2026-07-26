# MyGame — 스탠드얼론 게임 런타임 (docs/mmorpg M7). 엔진 전 계층 위 데이터드리븐 실행 파일.
add_executable(MyGame
    main.cpp
)

target_link_libraries(MyGame PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_audio mye_scene
)
