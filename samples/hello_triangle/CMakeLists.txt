# hello_triangle — M0 데모: 창 + 클리어 컬러 위 DX11 삼각형 (콘솔 서브시스템)
add_executable(hello_triangle
    src/main.cpp
)

# shell32: CommandLineToArgvW (샘플 main이 UTF-8 커맨드라인 변환) / user32: SetWindowPos 등 창 API(--resize-test)
target_link_libraries(hello_triangle PRIVATE mye_core mye_rhi shell32 user32)
