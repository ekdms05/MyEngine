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
    src/NavTests.cpp                  # M5-B: 길찾기(그리드 A*·다리·비동기) + CreateWithId
    src/PhysTests.cpp
    src/SceneSmokeTests.cpp
    src/ReflectTests.cpp
    src/AudioHotReloadTests.cpp   # M3-A: 오디오 임포터(WAV) + 핫 리로드
    src/AnimationTests.cpp        # M3-B: 스프라이트 애니메이션·8방향·상태머신·이벤트
    src/AudioPlaybackTests.cpp    # M3-B: 믹서·버스·큐·크로스페이드·공간화(헤드리스)
    src/ScriptBindingTests.cpp    # M3-C: 엔진 API Lua 바인딩(Math/ECS/Input/Audio/Event)
    src/ScriptTests.cpp           # M3-C: Lua 런타임·콜백·에러격리·핫리로드·코루틴
    src/EditorSerializationTests.cpp  # M4-A: 씬/프리팹 직렬화·인스펙터 값 blob
    src/EditorTests.cpp               # M4-A: 커맨드/Undo·선택·플레이모드·확장·패널
    src/EditorWorkflowTests.cpp       # M4-B: 배치·편집·저장·플레이·Undo 엔드투엔드 워크플로우
    src/UiSmokeTests.cpp              # M5-A: 인게임 UI + 텍스트 골격 계약 스모크(헤드리스)
    src/UiLayoutTests.cpp             # M5-A: 앵커·레이아웃·히트테스트·이벤트·직렬화·9slice·BMP 덤프
    src/TextTests.cpp                 # M5-A: 한글 텍스트 렌더링(FreeType 래스터·아틀라스·리치·레이아웃)
    src/UiIntegrationTests.cpp        # M5-A: 통합 스모크 — 한글 래스터+대화창+버튼콜백+줄바꿈 BMP 덤프
    src/TileAnimEditorTests.cpp       # M5-B: 타일맵 스트로크 Undo·오토타일·높이/경사·애니 클립/이벤트·8방향
)

target_link_libraries(mye_tests PRIVATE mye_core mye_rhi mye_asset mye_scene mye_reflect mye_audio mye_script mye_editor mye_ui)
target_include_directories(mye_tests PRIVATE src)

# ScriptBindingTests 는 sol2 로 Lua 반환값을 직접 읽는다 → sol2 헤더 경로·정의 필요(벤더 경계).
target_link_libraries(mye_tests PRIVATE mye_thirdparty_sol2)

add_test(NAME mye_tests COMMAND mye_tests)
