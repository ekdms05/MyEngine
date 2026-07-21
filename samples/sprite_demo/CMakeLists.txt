# sprite_demo — M1 통합 데모·검증 (도트 캐릭터 픽셀퍼펙트 렌더 + WASD 이동 + ImGui 오버레이)
add_executable(sprite_demo
    src/main.cpp
)

target_link_libraries(sprite_demo PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_imgui
)

# CTest: 입력 없이 자동 종료하는 검증 실행(짧은 프레임 수). GPU 필요.
add_test(NAME sprite_demo_smoke COMMAND sprite_demo --frames 30)
