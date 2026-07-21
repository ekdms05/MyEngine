# MyEngine — 테일즈위버류 2.5D 픽셀아트 하이브리드 엔진 (M0 부트스트랩)
cmake_minimum_required(VERSION 3.26)

project(MyEngine VERSION 0.1.0 LANGUAGES CXX)

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

add_subdirectory(engine/core)
add_subdirectory(engine/rhi)
add_subdirectory(samples/hello_triangle)
add_subdirectory(tests)
