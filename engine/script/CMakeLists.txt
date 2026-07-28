# mye_script — L4 Lua 스크립팅 런타임(05 M3-C: sol2 + Lua 5.4 게임 로직 핫 리로드)
#
# 의존:
#   mye_core   — EventBus·Log·Time·Base(Expected/해시)·Module
#   mye_scene  — ECS(World·SystemScheduler·CommandBuffer)·phys(Trigger 이벤트)·anim(AnimationEvent)
#   mye_asset  — AssetManager·AssetHandle·Importer·AssetReloadedEvent(핫 리로드)
# 백엔드(PRIVATE):
#   mye_thirdparty_sol2 — sol2 단일 헤더(+ mye_thirdparty_lua 를 전이 링크)
#     ★ Lua 는 메인 스레드 전용(잡 워커 실행 금지 — 확정 규약). sol2/Lua 경계는 예외 사용 가능.
#
# docs/05 §Lua 스테이트 소유 모델: 게임 스테이트 1개(ScriptRuntime 소유). 에디터 스테이트는 07.
add_library(mye_script STATIC
    # ---- 공용 타입·계약(sol 비의존 표면) ----
    include/mye/script/ScriptTypes.h
    include/mye/script/IBindingModule.h
    include/mye/script/ScriptImporter.h

    # ---- 엔진 API Lua 바인딩 모듈(05 바인딩 계층) ----
    include/mye/script/bindings/EngineBindings.h
    src/bindings/MathBindings.cpp
    src/bindings/EcsBindings.cpp
    src/bindings/InputBindings.cpp
    src/bindings/AudioBindings.cpp
    src/bindings/EventBindings.cpp
    src/bindings/ReflectBindings.cpp
    src/bindings/DdcBindings.cpp
    src/bindings/EngineBindings.cpp

    # ---- 런타임·컴포넌트·시스템·코루틴(sol 표면) ----
    include/mye/script/ScriptRuntime.h
    include/mye/script/ScriptComponent.h
    include/mye/script/ScriptClass.h
    include/mye/script/ScriptSystem.h
    include/mye/script/CoroutineScheduler.h

    # ---- 구현 ----
    src/ScriptRuntime.cpp
    src/ScriptClass.cpp
    src/ScriptSystem.cpp
    src/CoroutineScheduler.cpp
    src/ScriptImporter.cpp
)

target_include_directories(mye_script PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_script
    PUBLIC  mye_core mye_scene mye_asset mye_audio mye_reflect mye_ddc
    PRIVATE mye_thirdparty_sol2
)

# sol2 는 대형 단일 헤더 — 컴파일 시간·경고를 완화한다. 우리 코드(/W4)는 유지하되 sol2 유래
# 경고만 억제하기 어려우므로, sol2 를 소비하는 이 타깃 전체를 /W3 로 낮춘다(벤더 경계 격리).
if(MSVC)
    target_compile_options(mye_script PRIVATE /W3 /bigobj)
endif()
