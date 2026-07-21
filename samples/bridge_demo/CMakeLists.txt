# bridge_demo — M2 통합 데모·검증 (하이브리드 깊이 정렬: 다리 위/아래·석상 가림·경사)
add_executable(bridge_demo
    src/main.cpp
)

# mye_scene(ECS·타일맵·물리·추출) + mye_render(하이브리드 렌더러) + mye_imgui.
# mye_render는 scene 심볼(TilemapWorld::Layer 등)을 참조하되 링크 edge는 없으므로,
# 최종 실행 타깃이 두 정적 라이브러리를 함께 링크해 해소한다(mye_scene가 mye_render를 PUBLIC 링크).
target_link_libraries(bridge_demo PRIVATE
    mye_core mye_rhi mye_asset mye_render mye_scene mye_imgui
)

# CTest: 각 시나리오를 짧은 프레임으로 렌더(입력 없이 자동 종료). GPU 필요.
add_test(NAME bridge_demo_smoke COMMAND bridge_demo --frames 10)
add_test(NAME bridge_demo_over_bridge
    COMMAND bridge_demo --scenario over_bridge --frames 10)
