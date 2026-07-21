# mye_audio — L2 오디오 재생(06 M3-B: 믹서·버스·큐·BGM 크로스페이드·공간화)
# 의존: mye_core(Math·Config·Module·Log) + mye_asset(AudioClip). 백엔드: third_party miniaudio.
#   스트리밍 ogg 온디맨드 디코드를 위해 stb_vorbis 헤더 온리 참조(구현 TU 는 mye_asset 소유).
add_library(mye_audio STATIC
    include/mye/audio/AudioTypes.h
    include/mye/audio/ClipSource.h
    include/mye/audio/AudioCue.h
    include/mye/audio/SoftwareMixer.h
    include/mye/audio/IAudioBackend.h
    include/mye/audio/MiniaudioBackend.h
    include/mye/audio/AudioEngine.h
    include/mye/audio/AudioModule.h

    src/AudioTypes.cpp
    src/ClipSource.cpp
    src/SoftwareMixer.cpp
    src/MiniaudioBackend.cpp
    src/AudioEngine.cpp
    src/AudioModule.cpp
)

target_include_directories(mye_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_audio
    PUBLIC  mye_core mye_asset
    PRIVATE mye_thirdparty_miniaudio mye_thirdparty_stb_vorbis
)
