# MyGame — standalone project player; shares the editor's scene/asset/runtime contracts.
add_executable(MyGame
    main.cpp
)

target_link_libraries(MyGame PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_scene mye_runtime mye_gameplay mye_net mye_ui
)

add_custom_target(MyGameContent
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MyGame>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/fonts/NanumSquareRoundR.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/NanumSquareRound-LICENSE.txt"
        "$<TARGET_FILE_DIR:MyGame>/fonts"
    COMMENT "Copying the licensed game UI font"
)
add_dependencies(MyGame MyGameContent)
