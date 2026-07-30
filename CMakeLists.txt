# MyEngine — 테일즈위버류 2.5D 픽셀아트 하이브리드 엔진 (M0 부트스트랩)
cmake_minimum_required(VERSION 3.26)

project(MyEngine VERSION 0.1.0 LANGUAGES CXX C)   # C: 벤더 stb_vorbis(OGG 디코드) 컴파일

# ---- 전역 언어·툴체인 규약 (docs/01: C++20, /W4, /utf-8, 예외는 /EHsc 유지) ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC 런타임: /MD(Release) · /MDd(Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(MSVC)
    add_compile_options(/W4 /utf-8 /EHsc /permissive- /MP)
    # Debug: CRT 디버그 힙 릭 체크 활성 스위치 (GuardedMain이 _CrtSetDbgFlag 설치)
    add_compile_definitions(
        $<$<CONFIG:Debug>:MYE_DEBUG=1>
        $<$<CONFIG:Debug>:MYE_CRT_LEAK_CHECK=1>
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )
endif()

enable_testing()

# 서드파티 인터페이스 타깃(stb 등) — 라이브러리보다 먼저 등록해 소비 가능하게.
add_subdirectory(third_party)

add_subdirectory(engine/core)
add_subdirectory(engine/reflect)
add_subdirectory(engine/plugin)
add_subdirectory(engine/ddc)
add_subdirectory(engine/rhi)
add_subdirectory(engine/asset)
add_subdirectory(engine/render)
add_subdirectory(engine/scene)
add_subdirectory(engine/gameplay)
add_subdirectory(engine/net)
add_subdirectory(engine/persist)
add_subdirectory(engine/liveops)
add_subdirectory(engine/gameserver)
add_subdirectory(engine/audio)
add_subdirectory(engine/script)
add_subdirectory(engine/imgui)
add_subdirectory(engine/ui)
add_subdirectory(engine/runtime)
add_subdirectory(engine/editor)
add_subdirectory(game/social)
add_subdirectory(apps/editor)
add_subdirectory(apps/paktool)
add_subdirectory(apps/game)
add_subdirectory(apps/server)
add_subdirectory(samples/hello_triangle)
add_subdirectory(samples/asset_smoke)
add_subdirectory(samples/sprite_demo)
add_subdirectory(samples/bridge_demo)
add_subdirectory(samples/character_demo)
add_subdirectory(samples/village_demo)
add_subdirectory(tests)
