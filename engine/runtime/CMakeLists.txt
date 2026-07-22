# mye_runtime — L4/06 게임 런타임 시스템 (M6-A: 대화·컷신·씬전환·세이브·로컬라이제이션·2.5D 오디오)
#
# 소유: docs/06 §7(세이브·로컬라이제이션·씬전환) + 대화/컷신 연출(코루틴 프리미티브 say/choose/
#   move_to/camera.focus). "데이터(에셋)로 정의 + Lua 로 로직" 철학. 시뮬(고정틱)/표현(가변) 분리.
#
# 의존:
#   PUBLIC  mye_core   — Base(Expected)·Events·Config·Module·Math
#           mye_ui     — DialogueBox(위젯/텍스트 한글 RichText)·UiDocument·UiSystem·로딩 화면
#           mye_scene  — ecs::Entity/World(이동·리스너 관심점)·nav(NavSystem 경로 이동)
#           mye_audio  — AudioEngine(voiceCue·리스너·거리 감쇠 소비, 재구현 아님)
#           mye_asset  — VirtualFileSystem(user:// 세이브·문자열 테이블 로드)
#           mye_reflect— refl/ser JsonArchive(세이브 페이로드·대사/UI 에셋 직렬화)
#   PRIVATE mye_script — IBindingModule(RuntimeBindings)·CoroutineScheduler(컷신 코루틴 결합)
#           mye_thirdparty_sol2 — sol2 단일 헤더(RuntimeBindings.cpp 만 소비 — 벤더 경계)
#
# 네임스페이스: mye::runtime. Lua 는 메인 스레드 전용(확정 규약).
add_library(mye_runtime STATIC
    # ---- 공용 값 타입·계약 ----
    include/mye/runtime/RuntimeTypes.h
    src/RuntimeTypes.cpp

    # ---- 로컬라이제이션(StringTable·LocalizedText) ----
    include/mye/runtime/Localization.h
    src/Localization.cpp

    # ---- 대화(데이터·상태기계·DialogueBox) ----
    include/mye/runtime/DialogueData.h
    include/mye/runtime/DialogueSystem.h
    src/DialogueData.cpp
    src/DialogueSystem.cpp

    # ---- 컷신 코루틴 프리미티브 백엔드(say/choose/move_to/camera.focus) ----
    include/mye/runtime/CutsceneRuntime.h
    src/CutsceneRuntime.cpp

    # ---- 세이브/로드(슬롯·버전·ISaveParticipant) ----
    include/mye/runtime/SaveSystem.h
    src/SaveSystem.cpp

    # ---- 씬 전환(페이드·로딩·진행률) ----
    include/mye/runtime/SceneTransition.h
    src/SceneTransition.cpp

    # ---- 2.5D 오디오 리스너 배선(플레이어 위치) ----
    include/mye/runtime/AudioListener.h
    src/AudioListener.cpp

    # ---- 모듈 라이프사이클 배선 ----
    include/mye/runtime/RuntimeModule.h
    src/RuntimeModule.cpp

    # ---- Lua 바인딩 모듈(sol 표면 — 벤더 경계) ----
    include/mye/runtime/RuntimeBindings.h
    src/RuntimeBindings.cpp
)

target_include_directories(mye_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_runtime
    PUBLIC  mye_core mye_ui mye_scene mye_audio mye_asset mye_reflect
    PRIVATE mye_script mye_thirdparty_sol2
)

# RuntimeBindings.cpp 만 sol2 대형 단일 헤더를 소비한다. 라이브러리 전역을 낮추지 않고
#   해당 TU 만 /W3 /bigobj 로 완화(벤더 경계 격리, mye_script 와 동일 정책).
if(MSVC)
    set_source_files_properties(src/RuntimeBindings.cpp PROPERTIES
        COMPILE_OPTIONS "/W3;/bigobj")
endif()
