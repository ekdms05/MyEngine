# village_demo — M6-B 로드맵 최종 수직 슬라이스 데모(테일즈위버풍 마을).
#
# 전체 엔진 스택 관통: RHI(DX11) · Asset(PNG/GLB/WAV/Lua) · ECS/scene(물리·타일맵·nav·anim) ·
#   render(HybridRenderer·PixelPerfectTarget) · audio · script(ScriptRuntime + 바인딩) ·
#   runtime(Dialogue·Cutscene·Npc·Localization) · text/ui(한글 대화창 오버레이) · imgui(DebugUi).
add_executable(village_demo
    src/main.cpp
)

# main.cpp 가 sol::state 를 직접 다루고(NPC/대화 배선), 한글 텍스트 오버레이를 text 스택으로
#   렌더하므로 mye_ui(text) 와 mye_runtime, mye_thirdparty_sol2 를 함께 링크한다.
target_link_libraries(village_demo PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_scene mye_audio mye_script
    mye_ui mye_runtime mye_imgui
    mye_thirdparty_sol2
)

# sol2 대형 단일 헤더 — 이 타깃도 /W3 /bigobj 로 완화(벤더 경계 격리).
if(MSVC)
    target_compile_options(village_demo PRIVATE /W3 /bigobj)
endif()

# CTest: 결정적 시나리오를 짧은 프레임으로 헤드리스 구동(입력 없이 자동 종료). GPU 필요.
add_test(NAME village_demo_smoke  COMMAND village_demo --frames 10 --scenario walk)
add_test(NAME village_demo_bridge COMMAND village_demo --frames 10 --scenario bridge)
add_test(NAME village_demo_talk   COMMAND village_demo --frames 20 --scenario talk)
