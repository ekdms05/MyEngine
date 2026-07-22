# character_demo — M3 통합 데모·검증 (Lua 스크립팅 + 8방향 애니 + 발소리 오디오 + 핫리로드)
add_executable(character_demo
    src/main.cpp
)

# mye_scene(ECS·anim) + mye_render(하이브리드) + mye_audio + mye_script + mye_imgui.
# main.cpp 가 sol::state 를 직접 다루므로(강제 이동 주입·self.state 조회) mye_thirdparty_sol2
# 헤더 경로·정의가 필요하다(벤더 경계).
target_link_libraries(character_demo PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_scene mye_audio mye_script mye_imgui
    mye_thirdparty_sol2
)

# sol2 대형 단일 헤더 — 이 타깃도 /W3 /bigobj 로 완화(벤더 경계 격리, mye_script 와 동일).
if(MSVC)
    target_compile_options(character_demo PRIVATE /W3 /bigobj)
endif()

# CTest: 결정적 시나리오를 짧은 프레임으로 구동(입력 없이 자동 종료). GPU 필요.
add_test(NAME character_demo_smoke    COMMAND character_demo --frames 10)
add_test(NAME character_demo_8way     COMMAND character_demo --scenario 8way --frames 30)
add_test(NAME character_demo_footstep COMMAND character_demo --scenario footstep --frames 40)
add_test(NAME character_demo_hotreload COMMAND character_demo --scenario hotreload --frames 40)
add_test(NAME character_demo_error    COMMAND character_demo --scenario error --frames 20)
