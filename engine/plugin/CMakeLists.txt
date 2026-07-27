# mye_plugin — 플러그인 확장 시스템(인프로세스 호스트) (엔진 확장성)
# 게임 로직·툴·콘텐츠는 이 위에 플러그인으로 붙는다(엔진에 포함되지 않음). DLL 로더는 후속.
add_library(mye_plugin STATIC
    include/mye/plugin/Plugin.h
    include/mye/plugin/PluginHost.h

    src/PluginHost.cpp
)

target_include_directories(mye_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_plugin
    PUBLIC mye_core mye_reflect
)
