# Dear ImGui (docking 브랜치, v1.92.9-WIP) — 벤더링.
#
# 규약: 서드파티 소스는 여기서 인터페이스 타깃으로만 노출한다. 실제 컴파일은 소비 라이브러리
# (engine/imgui → mye_imgui)가 자기 TU로 끌어와 수행한다. 이렇게 하면 imconfig.h(데이터
# 레이아웃 영향)가 모든 소비 TU에서 동일하게 보이고, /W4 경고는 mye_imgui에서 국소적으로 낮춘다.
#
# 노출:
#   mye_thirdparty_imgui_headers  : include 경로 (imgui.h, backends/) 만 제공하는 INTERFACE
#   IMGUI_CORE_SOURCES / IMGUI_BACKEND_SOURCES : mye_imgui가 add_library에 넣을 소스 목록(부모 스코프)

add_library(mye_thirdparty_imgui_headers INTERFACE)
target_include_directories(mye_thirdparty_imgui_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backends>
)

set(IMGUI_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_demo.cpp
    PARENT_SCOPE
)

set(IMGUI_BACKEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    PARENT_SCOPE
)
