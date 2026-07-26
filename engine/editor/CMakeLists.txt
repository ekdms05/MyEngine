# mye_editor — L5 에디터 프레임워크 (M4-A 골격: 셸·커맨드/Undo·선택·플레이모드·직렬화·인스펙터)
#
# docs/07 소유. 게임과 동일 엔진 위의 특권 앱. 아래 전부에 의존한다:
#   core(앱·이벤트·모듈) · reflect/ser(인스펙터·Undo blob·직렬화) · scene(World·Transform) ·
#   render(오프스크린 뷰포트는 M4-B) · asset(AssetRef) · imgui(도킹 셸).
# M4-A는 선언 위주 계약 + 컴파일되는 스텁. 실제 ImGui 드로우·World 변경은 M4-B 구현 에이전트.
add_library(mye_editor STATIC
    # ---- 공용 타입·컨텍스트 ----
    include/mye/editor/EditorTypes.h
    include/mye/editor/EditorContext.h
    src/EditorContext.cpp

    # ---- 커맨드 기반 Undo/Redo (docs/07 §4) ----
    include/mye/editor/Command.h
    include/mye/editor/CommandStack.h
    src/Command.cpp
    src/CommandStack.cpp

    # ---- 선택 (docs/07 §5) ----
    include/mye/editor/Selection.h
    src/Selection.cpp

    # ---- 패널·확장 레지스트리 (docs/07 §7, §확장) ----
    include/mye/editor/Panel.h
    include/mye/editor/ExtensionRegistry.h
    src/Panel.cpp
    src/ExtensionRegistry.cpp

    # ---- 리플렉션 자동 인스펙터 (docs/07 인스펙터) ----
    include/mye/editor/Inspector.h
    src/Inspector.cpp

    # ---- 플레이 모드 (docs/07 §3) ----
    include/mye/editor/PlayMode.h
    src/PlayMode.cpp

    # ---- 씬/프리팹 직렬화 (씬 직렬화는 mye_scene으로 이동; 여기선 별칭 헤더 + 프리팹만) ----
    include/mye/editor/SceneSerializer.h
    include/mye/editor/Prefab.h
    src/Prefab.cpp

    # ---- 프로젝트·문서 (docs/07 §2, §6) ----
    include/mye/editor/Project.h
    src/Project.cpp

    # ---- 앱 셸·모듈 (docs/07 §1) ----
    include/mye/editor/EditorApp.h
    include/mye/editor/EditorModule.h
    src/EditorApp.cpp
    src/EditorModule.cpp

    # ---- 씬 뷰포트·기즈모 (docs/07 씬 뷰포트, M4-B) ----
    include/mye/editor/Viewport.h

    # ---- 내장 패널 (docs/07 하이어라키·인스펙터·에셋·콘솔·뷰포트) ----
    include/mye/editor/BuiltinPanels.h
    src/HierarchyPanel.cpp
    src/InspectorPanel.cpp
    src/AssetBrowserPanel.cpp
    src/ConsolePanel.cpp
    src/ViewportPanel.cpp

    # ---- M5-B 콘텐츠 제작 도구: 타일맵 에디터 + 애니메이션 에디터 (docs/07) ----
    include/mye/editor/TileEditing.h
    include/mye/editor/AnimEditing.h
    src/TilePaintCommand.cpp
    src/TilemapEditorPanel.cpp
    src/AnimClipEditCommand.cpp
    src/AnimationEditorPanel.cpp

    # ---- 도트(픽셀아트) 에디터 패널 ----
    src/DotEditorPanel.cpp
)

target_include_directories(mye_editor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# stb_image_write(도트 에디터 PNG 저장) — third_party 헤더 경로(PRIVATE).
target_include_directories(mye_editor PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../third_party>
)

# 에디터는 엔진 전 계층을 소비. imgui는 도킹 셸(M4-B에서 DebugUi 직접 사용).
target_link_libraries(mye_editor
    PUBLIC  mye_core mye_reflect mye_scene mye_asset mye_render
    PRIVATE mye_imgui
)
