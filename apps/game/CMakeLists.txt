# MyGame — standalone project player; shares the editor's scene/asset/runtime contracts.
add_executable(MyGame
    main.cpp
)

target_link_libraries(MyGame PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_scene mye_runtime mye_gameplay
)
