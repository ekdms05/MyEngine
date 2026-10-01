# MyEditor — 에디터 실행 파일 (docs/07). L5 얇은 앱: 엔진 스택 + EditorModule.
#
# main()이 LaunchArgs를 구성하고 GuardedMain에 위임한다.
add_executable(MyEditor
    src/main.cpp
)

target_link_libraries(MyEditor PRIVATE
    mye_core mye_scene mye_editor
)

add_custom_target(MyEditorContent
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/game/starter/meadow_village"
        "$<TARGET_FILE_DIR:MyEditor>/templates/meadow_village"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/docs"
        "$<TARGET_FILE_DIR:MyEditor>/docs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MyEditor>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/fonts/NanumSquareRoundR.ttf"
        "${CMAKE_SOURCE_DIR}/assets/fonts/NanumSquareRound-LICENSE.txt"
        "$<TARGET_FILE_DIR:MyEditor>/fonts"
    COMMENT "Copying editor starter assets, offline guides and licensed UI font"
)
add_dependencies(MyEditor MyEditorContent)
