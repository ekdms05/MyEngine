# mye_tests — 자체 TEST 매크로 테스트 러너 (서드파티 금지 규약)
add_executable(mye_tests
    src/TestFramework.h
    src/TestMain.cpp
    src/MathTests.cpp
    src/CoreTests.cpp
    src/JsonTests.cpp
    src/DiagConfigTests.cpp
    src/MemoryTests.cpp
    src/InputTests.cpp
    src/AssetTests.cpp
    src/JobsTests.cpp
    src/SceneTests.cpp
)

target_link_libraries(mye_tests PRIVATE mye_core mye_rhi mye_asset mye_scene)
target_include_directories(mye_tests PRIVATE src)

add_test(NAME mye_tests COMMAND mye_tests)
