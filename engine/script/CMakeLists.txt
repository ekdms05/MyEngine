# Lua VM and direct C API bindings. Main-thread execution only.
add_library(mye_script STATIC
    # ---- 공용 타입·계약 ----
    include/mye/script/ScriptTypes.h
    include/mye/script/LuaApi.h
    src/LuaApi.cpp
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

    # ---- 런타임·컴포넌트·시스템·코루틴 ----
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
    PUBLIC mye_thirdparty_lua
)
