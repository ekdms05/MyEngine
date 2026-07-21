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
    src/SpriteImportTests.cpp
    src/MeshTests.cpp
    src/JobsTests.cpp
    src/SceneTests.cpp
    src/TilemapTests.cpp
    src/PhysTests.cpp
    src/SceneSmokeTests.cpp
    src/ReflectTests.cpp
    src/AudioHotReloadTests.cpp   # M3-A: 오디오 임포터(WAV) + 핫 리로드
    src/AnimationTests.cpp        # M3-B: 스프라이트 애니메이션·8방향·상태머신·이벤트
    src/AudioPlaybackTests.cpp    # M3-B: 믹서·버스·큐·크로스페이드·공간화(헤드리스)
)

target_link_libraries(mye_tests PRIVATE mye_core mye_rhi mye_asset mye_scene mye_reflect mye_audio)
target_include_directories(mye_tests PRIVATE src)

add_test(NAME mye_tests COMMAND mye_tests)
