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
# 표준 Lua 5.4 채택(오픈이슈 #1 확정, LuaJIT 미채택). C 소스를 STATIC 라이브러리로 컴파일.
#   docs/05는 장기적으로 lua54.dll(공유) + 플러그인 힙 공유를 명시하나, M3-C는 DLL 로더가
#   없고(플러그인은 M4) 정적 링크 검증 단계이므로 STATIC으로 빌드한다(확정 규약: 정적 링크).
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
# C 컴파일. 벤더 코드 경고는 우리 규약(/W4)에서 제외.
if(MSVC)
    target_compile_options(mye_thirdparty_lua PRIVATE /W0)
    # sol2/C++ 경계에서 예외를 던지므로 C++ 예외를 Lua 경계로 넘기지 않도록
    # LUA_USE_LONGJMP(기본) 대신 C++ 예외 전파를 켠다(스택 언와인딩으로 소멸자 실행).
    target_compile_definitions(mye_thirdparty_lua PUBLIC LUA_USE_LONGJMP)
else()
    target_compile_options(mye_thirdparty_lua PRIVATE -w)
endif()

# ---- sol2 v3.2.3 (MIT, github release) — Lua C++ 바인딩(단일 헤더) ----
# 헤더 온리 인터페이스 타깃. 소비자(mye_script)가 lua 타깃과 함께 링크한다.
#   sol2/Lua 경계는 예외 사용 가능(확정 규약, /EHsc 켜져있음).
add_library(mye_thirdparty_sol2 INTERFACE)
target_include_directories(mye_thirdparty_sol2 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sol2>
)
target_link_libraries(mye_thirdparty_sol2 INTERFACE mye_thirdparty_lua)
# sol2 안전 옵션: 모든 스택 접근·함수 호출을 검사(디버그 안전). 예외 전파 허용.
target_compile_definitions(mye_thirdparty_sol2 INTERFACE
    SOL_ALL_SAFETIES_ON=1
    SOL_EXCEPTIONS_SAFE_PROPAGATION=1
)

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
