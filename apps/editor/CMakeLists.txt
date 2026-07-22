# MyEditor — 에디터 실행 파일 (docs/07 §1). L5 얇은 앱: 엔진 스택 + EditorModule.
#
# M4-A 골격: 컴파일·부팅 경로 확립. main()이 GuardedMain(argc,argv)에 위임(엔진 진입 규약).
add_executable(MyEditor
    src/main.cpp
)

target_link_libraries(MyEditor PRIVATE
    mye_core mye_scene mye_editor
)
