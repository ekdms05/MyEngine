# MyEditor — 에디터 실행 파일 (docs/07). L5 얇은 앱: 엔진 스택 + EditorModule.
#
# main()이 LaunchArgs를 구성하고 GuardedMain에 위임한다.
add_executable(MyEditor
    src/main.cpp
)

target_link_libraries(MyEditor PRIVATE
    mye_core mye_scene mye_editor
)
