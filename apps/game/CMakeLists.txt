# MyGame — 프로젝트 씬·렌더·UI·오디오와 온라인 이동을 조합하는 게임 실행 파일.
add_executable(MyGame
    main.cpp
)

target_link_libraries(MyGame PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_audio mye_scene mye_net
    mye_ui
)
