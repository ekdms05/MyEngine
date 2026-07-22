# mye_ui — L4/06 인게임 UI + 한글 텍스트 렌더링 (M5-A)
#
# 소유: docs/06 §1(인게임 UI 자체 시스템 — ImGui 아님)·§2(FreeType 온디맨드 글리프 아틀라스·
#   리치텍스트·한글 줄바꿈). 02 SpriteBatch 를 스크린 레이어에서 소비, rhi WriteTexture 로 R8
#   글리프 아틀라스 부분 업로드, 04 refl/ser 로 UiDocument/UiSkin 직렬화, core InputState 소비.
# 의존:
#   PUBLIC  mye_core(수학·이벤트·입력·Base) mye_render(SpriteBatch) mye_rhi(디바이스·텍스처)
#           mye_reflect(UiDocument/Skin 직렬화)  — 헤더가 노출하는 타입 경로.
#   PRIVATE mye_thirdparty_freetype(폰트 래스터라이즈 — FT 헤더는 구현 TU 에만).
#           mye_asset 는 후속(폰트/스프라이트 에셋 로드) — M5-A 골격은 blob/핸들 표면만.
#
# 네임스페이스: mye::text (텍스트), mye::ui (위젯/시스템). 텍스트는 mye_ui 안의 text 서브로 둔다
#   (독립 라이브러리 분리는 과분할 — 06 단일 소유, 한 정적 라이브러리).
add_library(mye_ui STATIC
    # ---- 텍스트 (mye::text) ----
    include/mye/text/TextTypes.h
    include/mye/text/FontFace.h
    include/mye/text/GlyphAtlas.h
    include/mye/text/RichText.h
    include/mye/text/TextLayout.h
    include/mye/text/TextRenderer.h
    include/mye/text/BitmapFont.h
    src/text/FontFace.cpp
    src/text/GlyphAtlas.cpp
    src/text/RichText.cpp
    src/text/TextLayout.cpp
    src/text/TextRenderer.cpp
    src/text/BitmapFont.cpp

    # ---- 인게임 UI (mye::ui) ----
    include/mye/ui/UiTypes.h
    include/mye/ui/UiEvent.h
    include/mye/ui/UiDrawContext.h
    include/mye/ui/Widget.h
    include/mye/ui/Widgets.h
    include/mye/ui/UiSkin.h
    include/mye/ui/UiDocument.h
    include/mye/ui/UiRenderer.h
    include/mye/ui/UiSystem.h
    src/ui/UiTypes.cpp
    src/ui/Widget.cpp
    src/ui/Widgets.cpp
    src/ui/UiDrawContext.cpp
    src/ui/UiSkin.cpp
    src/ui/UiDocument.cpp
    src/ui/UiRenderer.cpp
    src/ui/UiSystem.cpp
)

target_include_directories(mye_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(mye_ui
    PUBLIC  mye_core mye_render mye_rhi mye_reflect
    PRIVATE mye_thirdparty_freetype
)
