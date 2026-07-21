# mye_imgui — Dear ImGui(docking) 코어 + win32/dx11 백엔드 + DebugUi 래퍼 (docs/07)
#
# imgui 코어·백엔드 소스는 third_party/imgui가 노출한 IMGUI_CORE_SOURCES/IMGUI_BACKEND_SOURCES
# 변수로 이 라이브러리 TU에 합류한다(imconfig.h 일관성 보장). /W4 경고는 imgui 소스에 한해 완화한다.
add_library(mye_imgui STATIC
    include/mye/imgui/DebugUi.h
    src/DebugUi.cpp

    ${IMGUI_CORE_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

target_include_directories(mye_imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# DebugUi.cpp만 mye_core/mye_rhi 공개 헤더를 사용(imgui 소스는 자기 헤더만).
# imgui 헤더 경로는 PRIVATE — 소비자(통합 데모)는 DebugUi.h만 보면 되고 imgui.h를 직접 쓰려면
# mye_thirdparty_imgui_headers를 별도로 링크한다.
target_link_libraries(mye_imgui
    PUBLIC  mye_core
    PRIVATE mye_rhi
            mye_thirdparty_imgui_headers
            d3d11 d3dcompiler dxgi dwmapi
)

# 통합 데모가 ImGui:: 위젯을 직접 호출할 수 있도록 imgui 헤더 경로를 PUBLIC 소비 가능하게 재노출.
# (DebugUi 사용 예시가 ImGui::Begin/Text/End를 부른다.)
target_link_libraries(mye_imgui PUBLIC mye_thirdparty_imgui_headers)

# imgui 벤더 소스는 /W4에서 다수 경고가 발생 — 이 타깃 한정으로 경고를 낮춘다(우리 코드 아님).
if(MSVC)
    set_source_files_properties(
        ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES}
        PROPERTIES COMPILE_OPTIONS "/W0"
    )
endif()
