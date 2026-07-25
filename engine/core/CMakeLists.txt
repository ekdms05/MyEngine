# mye_core — L0 Core (수학·로그/어서트·설정·이벤트·모듈·플랫폼(win32)·앱 프레임워크)
add_library(mye_core STATIC
    include/mye/core/Base.h
    include/mye/core/Math.h
    include/mye/core/Log.h
    include/mye/core/Assert.h
    include/mye/core/CrashHandler.h
    include/mye/core/Json.h
    include/mye/core/I18n.h
    include/mye/core/Config.h
    include/mye/core/Events.h
    include/mye/core/Time.h
    include/mye/core/Module.h
    include/mye/core/Window.h
    include/mye/core/Input.h
    include/mye/core/Memory.h
    include/mye/core/Jobs.h
    include/mye/core/platform/Win32Window.h
    include/mye/core/platform/Win32Input.h
    include/mye/core/App.h

    src/Math.cpp
    src/Log.cpp
    src/Assert.cpp
    src/Json.cpp
    src/I18n.cpp
    src/Config.cpp
    src/Events.cpp
    src/Time.cpp
    src/Module.cpp
    src/Input.cpp
    src/Memory.cpp
    src/Jobs.cpp
    src/App.cpp
    src/platform/Win32Window.cpp
    src/platform/Win32Input.cpp
)

target_include_directories(mye_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# user32: 창·메시지 펌프 / shell32: CommandLineToArgvW (UTF-8 커맨드라인 변환)
target_link_libraries(mye_core PRIVATE user32 shell32)
