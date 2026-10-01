# third_party — 벤더링된 서드파티 라이브러리 인터페이스 타깃 모음.
#
# 규약: 서드파티는 최상위에서만 인터페이스 타깃으로 노출한다. 소비 라이브러리는
# target_link_libraries(<lib> PRIVATE mye_thirdparty_stb) 로 헤더 경로를 얻는다.
# STB_IMAGE_IMPLEMENTATION 정의는 각 소비자가 정확히 한 .cpp에서만 켠다.

# ---- stb_image (단일 헤더, public domain, v2.30) — PNG 디코드용 ----
add_library(mye_thirdparty_stb INTERFACE)
target_include_directories(mye_thirdparty_stb INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>
)
# 픽셀아트 임포트는 sRGB→linear 자동 변환을 원치 않는다(포인트 샘플·수동 감마).
# stb는 기본 8bit 경로만 쓰므로 특별한 컴파일 정의는 불필요.

# ---- cgltf (단일 헤더, MIT, v1.15) — glTF 2.0 정적 메시 파서(04) ----
# CGLTF_IMPLEMENTATION 정의는 소비자가 정확히 한 .cpp(MeshImporter.cpp)에서만 켠다.
add_library(mye_thirdparty_cgltf INTERFACE)
target_include_directories(mye_thirdparty_cgltf INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cgltf>
)

# ---- stb_vorbis (public domain, v1.22) — OGG Vorbis 디코드(04 AudioImporter) ----
# 헤더 온리로 쓰기 어렵고(구현이 큼) 여러 TU에서 링크 충돌을 피하려 STATIC 라이브러리로 컴파일.
# C 파일이라 별도 언어 활성화. 소비자는 target_link_libraries(<lib> PRIVATE mye_thirdparty_stb_vorbis).
add_library(mye_thirdparty_stb_vorbis STATIC
    stb_vorbis/stb_vorbis_impl.c
)
target_include_directories(mye_thirdparty_stb_vorbis PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb_vorbis>
)
# 벤더 코드의 경고는 우리 규약(/W4)에서 제외 — 서드파티는 경고 억제.
if(MSVC)
    target_compile_options(mye_thirdparty_stb_vorbis PRIVATE /W0)
else()
    target_compile_options(mye_thirdparty_stb_vorbis PRIVATE -w)
endif()

# ---- miniaudio (단일 헤더, public domain / MIT-0, v0.11.25) — 오디오 백엔드(06 M3-B) ----
# 디바이스·리샘플러·믹싱 그래프를 제공한다. 헤더는 인터페이스로 노출하고, 구현(MINIAUDIO_
# IMPLEMENTATION)은 miniaudio_impl.c 단일 TU 에서만 켠다(STATIC 라이브러리). 소비자(mye_audio)는
# target_link_libraries(mye_audio PRIVATE mye_thirdparty_miniaudio) 로 헤더 경로 + 구현을 링크.
add_library(mye_thirdparty_miniaudio STATIC
    miniaudio/miniaudio_impl.c
)
target_include_directories(mye_thirdparty_miniaudio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/miniaudio>
)
# 벤더 코드의 경고는 우리 규약(/W4)에서 제외.
if(MSVC)
    target_compile_options(mye_thirdparty_miniaudio PRIVATE /W0)
else()
    target_compile_options(mye_thirdparty_miniaudio PRIVATE -w)
    # miniaudio 는 POSIX 에서 pthread·dl·m 을 요구.
    find_package(Threads REQUIRED)
    target_link_libraries(mye_thirdparty_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---- Lua 5.4.7 (MIT, lua.org) — 게임 로직 스크립팅 런타임(05 M3-C) ----
# Lua is embedded as a static VM; no interpreter executable or binding library is shipped.
#   LUA_USE_* 플랫폼 매크로는 기본값(luaconf.h) — Windows에서는 표준 C만 사용.
#   lua.c(인터프리터 main)·luac.c(컴파일러 main)는 제외 — 라이브러리로만 임베드.
add_library(mye_thirdparty_lua STATIC
    lua/lapi.c    lua/lauxlib.c lua/lbaselib.c lua/lcode.c   lua/lcorolib.c
    lua/lctype.c  lua/ldblib.c  lua/ldebug.c   lua/ldo.c     lua/ldump.c
    lua/lfunc.c   lua/lgc.c     lua/linit.c    lua/liolib.c  lua/llex.c
    lua/lmathlib.c lua/lmem.c   lua/loadlib.c  lua/lobject.c lua/lopcodes.c
    lua/loslib.c  lua/lparser.c lua/lstate.c   lua/lstring.c lua/lstrlib.c
    lua/ltable.c  lua/ltablib.c lua/ltm.c      lua/lundump.c lua/lutf8lib.c
    lua/lvm.c     lua/lzio.c
)
target_include_directories(mye_thirdparty_lua PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lua>
)
# Compile Lua as C++ so protected Lua errors unwind native RAII objects. Keep
# its public C linkage; this does not introduce a separate binding dependency.
get_target_property(MYE_LUA_SOURCES mye_thirdparty_lua SOURCES)
set_source_files_properties(${MYE_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
if(MSVC)
    target_compile_options(mye_thirdparty_lua PRIVATE /W0 "/FI${CMAKE_CURRENT_SOURCE_DIR}/LuaCpp.h")
    # Lua errors may throw through its C ABI. /EHc would skip caller destructors
    # for extern "C" functions; every direct API consumer needs that assumption off.
    target_compile_options(mye_thirdparty_lua PUBLIC /EHsc-)
else()
    target_compile_options(mye_thirdparty_lua PRIVATE -w -include "${CMAKE_CURRENT_SOURCE_DIR}/LuaCpp.h")
endif()

# ---- FreeType 2.13.3 (FTL/GPLv2, freetype.org) — 폰트 래스터라이즈(06 M5-A 텍스트) ----
# 온디맨드 글리프 아틀라스용 TTF/OTF 래스터라이저. 한글 11,172자 동적 캐싱의 근간.
#   벤더링 범위(트리밍): TrueType·CFF·Type1·CID·Type42 드라이버 + sfnt·psaux·psnames·
#   pshinter·autofit + smooth/raster 렌더러 + ftstroke(아웃라인). 제외: pfr·winfnt·pcf·bdf
#   ·svg·sdf 렌더러·cache·dlg·harfbuzz·zlib(시스템)·libpng·brotli. 외부 의존 0 — 순수 정적.
#   모듈 등록 목록은 include/freetype/config/ftmodule.h 를 벤더 기본에서 트리밍한 버전 사용.
# 소비자(mye_ui/mye_text)는 target_link_libraries(<lib> PRIVATE mye_thirdparty_freetype).
#   FT2_BUILD_LIBRARY 는 라이브러리 컴파일 전용 정의(소비자는 정의하지 않음, PRIVATE).
add_library(mye_thirdparty_freetype STATIC
    # --- base ---
    freetype/src/base/ftsystem.c   # 기본 메모리·스트림 (ANSI stdio)
    freetype/src/base/ftinit.c     # FT_Init_FreeType — ftmodule.h 순회 등록
    freetype/src/base/ftbase.c     # 코어 (ftobjs·ftstream·ftcalc·ftgloadr·ftoutln 등 집합)
    freetype/src/base/ftbbox.c     # 아웃라인 바운딩 박스
    freetype/src/base/ftbitmap.c   # 비트맵 변환·임베딩(FT_Bitmap 유틸)
    freetype/src/base/ftglyph.c    # FT_Glyph 관리(글리프 오브젝트 복제·변환)
    freetype/src/base/ftstroke.c   # FT_Stroker — 아웃라인 스트로크(텍스트 아웃라인/그림자)
    freetype/src/base/ftmm.c       # Multi-Master/Named-Instance API (truetype/cff 가 참조 — FT_Set_Named_Instance)
    freetype/src/base/ftdebug.c    # FT_Trace_Enable/Disable·에러 문자열 (smooth/디버그 빌드 참조)
    # --- sfnt/truetype/cff ---
    freetype/src/sfnt/sfnt.c
    freetype/src/truetype/truetype.c
    freetype/src/cff/cff.c
    # --- Type1 / CID / Type42 (OTF·레거시 폰트 커버) ---
    freetype/src/type1/type1.c
    freetype/src/cid/type1cid.c
    freetype/src/type42/type42.c
    # --- helpers ---
    freetype/src/psaux/psaux.c
    freetype/src/psnames/psnames.c
    freetype/src/pshinter/pshinter.c
    freetype/src/autofit/autofit.c
    # --- renderers ---
    freetype/src/raster/raster.c   # 모노(FT_RENDER_MODE_MONO — 도트 폰트 MONO 힌팅)
    freetype/src/smooth/smooth.c   # 안티에일리어스(FT_RENDER_MODE_NORMAL — R8 그레이)
    # --- gzip (self-contained zlib copy — woff/gz 폰트, 시스템 zlib 미의존) ---
    freetype/src/gzip/ftgzip.c
)
target_include_directories(mye_thirdparty_freetype PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/freetype/include>
)
target_compile_definitions(mye_thirdparty_freetype PRIVATE FT2_BUILD_LIBRARY)
# 벤더 C 코드의 경고는 우리 규약(/W4)에서 제외.
if(MSVC)
    target_compile_options(mye_thirdparty_freetype PRIVATE /W0)
else()
    target_compile_options(mye_thirdparty_freetype PRIVATE -w)
endif()

# ---- Dear ImGui (docking, v1.92.x) — 디버그 오버레이 UI ----
# 헤더 인터페이스 타깃 + 소스 목록 변수(IMGUI_CORE_SOURCES/IMGUI_BACKEND_SOURCES)를 노출한다.
add_subdirectory(imgui)
# 소스 목록을 최상위(engine/imgui가 소비)로 다시 전파.
set(IMGUI_CORE_SOURCES    ${IMGUI_CORE_SOURCES}    PARENT_SCOPE)
set(IMGUI_BACKEND_SOURCES ${IMGUI_BACKEND_SOURCES} PARENT_SCOPE)
