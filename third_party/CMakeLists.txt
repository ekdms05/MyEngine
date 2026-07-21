# third_party — 벤더링된 서드파티 라이브러리 인터페이스 타깃 모음.
#
# 규약: 서드파티는 최상위에서만 인터페이스 타깃으로 노출한다. 소비 라이브러리는
# target_link_libraries(<lib> PRIVATE mye_thirdparty_stb) 로 헤더 경로를 얻는다.
# STB_IMAGE_IMPLEMENTATION 정의는 각 소비자가 정확히 한 .cpp에서만 켠다.

# ---- stb_image (단일 헤더, public domain, v2.30) — PNG 디코드용 ----
add_library(mye_thirdparty_stb INTERFACE)
target_include_directories(mye_thirdparty_stb INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
)
# 픽셀아트 임포트는 sRGB→linear 자동 변환을 원치 않는다(포인트 샘플·수동 감마).
# stb는 기본 8bit 경로만 쓰므로 특별한 컴파일 정의는 불필요.

# ---- Dear ImGui (docking, v1.92.x) — 디버그 오버레이 UI ----
# 헤더 인터페이스 타깃 + 소스 목록 변수(IMGUI_CORE_SOURCES/IMGUI_BACKEND_SOURCES)를 노출한다.
add_subdirectory(imgui)
# 소스 목록을 최상위(engine/imgui가 소비)로 다시 전파.
set(IMGUI_CORE_SOURCES    ${IMGUI_CORE_SOURCES}    PARENT_SCOPE)
set(IMGUI_BACKEND_SOURCES ${IMGUI_BACKEND_SOURCES} PARENT_SCOPE)
