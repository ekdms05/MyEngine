// mye/editor/ExtensionRegistry.h — 패널·메뉴·툴바·드로어 확장 진입점 (docs/07 §확장 포인트)
//
// 07: 에디터의 모든 내장 기능은 확장 포인트 위에서 구현된다(내장 = 1급 플러그인). "내장은
//   되는데 플러그인은 안 되는" API를 만들지 않는다. 플러그인(C++ DLL, 05)·MCP(08)·Lua가
//   동일 레지스트리로 패널·메뉴·툴바·인스펙터 드로어·(후속)기즈모/오버레이/툴을 추가한다.
//
// M4-A 범위: 패널·메뉴·툴바·프로퍼티 드로어·컴포넌트 에디터 등록(선언). 기즈모·오버레이·툴·
//   에셋/엔티티 컨텍스트 메뉴·설정 페이지는 전방선언 + 등록 시그니처만(구현은 후속 M4-B).
#pragma once

#include "mye/editor/EditorTypes.h"
#include "mye/editor/Panel.h"
#include "mye/editor/Inspector.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mye::refl { class TypeInfo; }

namespace mye::editor {

// ---- 메뉴·툴바 서술자 ----
// 경로 문자열 기반 선언적 등록. "Tools/My Plugin/Do Thing".
struct MenuPath {
    std::string path;   // '/' 구분. 마지막 세그먼트가 항목 라벨.
};
struct MenuItemDesc {
    std::string shortcut;                       // "Ctrl+S" 등(표시·리바인딩 키)
    std::function<bool()> isEnabled;            // null=항상 활성
    std::function<bool()> isChecked;            // null=체크 없음(토글 아님)
};
using MenuCallback = std::function<void(EditorContext&)>;

// 툴바 섹션(07 §7 툴바 레이아웃 대응).
enum class ToolbarSection : std::uint8_t { File, Transform, View, Play, Custom };
struct ToolbarButtonDesc {
    std::string icon;       // 아이콘 폰트 글리프/텍스트
    std::string tooltip;
    std::function<bool()> isEnabled;
    std::function<bool()> isChecked;
};

// ---- 후속(M4-B) 확장 인터페이스 — 순수 가상 골격(시그니처는 M4-B에서 확장) ----
// unique_ptr 저장을 위해 완전한 다형 기반 타입으로 둔다(등록만, 소비 로직은 후속).
class IGizmoExtension {      // 컴포넌트 선택 시 커스텀 핸들(07 §확장 4)
public:
    virtual ~IGizmoExtension() = default;
    virtual const refl::TypeInfo* TargetComponent() const = 0;
};
class IViewportOverlay {     // 상시/토글 오버레이
public:
    virtual ~IViewportOverlay() = default;
};
class IViewportTool {        // 모드형 툴(타일 브러시 동급)
public:
    virtual ~IViewportTool() = default;
};
class ISettingsPage {        // Preferences 카테고리
public:
    virtual ~ISettingsPage() = default;
};
struct AssetTypeFilter { std::uint64_t assetType = 0; };  // 04 AssetTypeId
using AssetMenuCallback  = std::function<void(EditorContext&)>;
using EntityMenuCallback = std::function<void(EditorContext&, ecs::Entity)>;

// 확장 레지스트리 — 07 API 스케치.
class EditorExtensionRegistry {
public:
    EditorExtensionRegistry();
    ~EditorExtensionRegistry();
    EditorExtensionRegistry(const EditorExtensionRegistry&) = delete;
    EditorExtensionRegistry& operator=(const EditorExtensionRegistry&) = delete;

    // ---- 패널·메뉴·툴바 (M4-A 활성) ----
    void AddPanel(std::unique_ptr<IEditorPanelFactory> factory);
    void AddMenuItem(const MenuPath& path, MenuItemDesc desc, MenuCallback onClick);
    void AddToolbarButton(ToolbarSection section, ToolbarButtonDesc desc, MenuCallback onClick);

    // ---- 인스펙터 커스터마이즈 (M4-A 활성) ----
    void AddPropertyDrawer(const refl::TypeInfo& type, std::unique_ptr<IPropertyDrawer> drawer);
    void AddComponentEditor(const refl::TypeInfo& componentType,
                            std::unique_ptr<IComponentEditor> editor);

    // 인스펙터가 조회 — 타입/컴포넌트에 등록된 드로어(없으면 nullptr → 기본 자동 UI).
    IPropertyDrawer*  FindPropertyDrawer(const refl::TypeInfo& type) const;
    IComponentEditor* FindComponentEditor(const refl::TypeInfo& componentType) const;

    // ---- 뷰포트·컨텍스트·설정 (M4-B: 등록 시그니처만, 구현은 후속) ----
    void AddGizmo(std::unique_ptr<IGizmoExtension> gizmo);
    void AddViewportOverlay(std::unique_ptr<IViewportOverlay> overlay);
    void AddViewportTool(std::unique_ptr<IViewportTool> tool);
    void AddAssetContextMenu(AssetTypeFilter filter, MenuItemDesc desc, AssetMenuCallback cb);
    void AddEntityContextMenu(MenuItemDesc desc, EntityMenuCallback cb);
    void AddSettingsPage(std::string_view category, std::unique_ptr<ISettingsPage> page);

    // 메뉴바 빌드가 소비하는 등록 항목 열거(경로·콜백). PanelManager/메뉴바가 사용.
    struct MenuEntry { MenuPath path; MenuItemDesc desc; MenuCallback onClick; };
    std::span<const MenuEntry> MenuEntries() const;
    struct ToolbarEntry { ToolbarSection section; ToolbarButtonDesc desc; MenuCallback onClick; };
    std::span<const ToolbarEntry> ToolbarEntries() const;

    // 확장 경로로 등록된 패널 팩토리를 PanelManager로 옮긴다(EditorApp이 초기화 시 호출).
    //   AddPanel은 등록 시점에 PanelManager가 없을 수 있으므로 팩토리를 임시 보관하고,
    //   여기서 RegisterFactory로 위임한다(플러그인이 패널을 등록할 수 있게 함).
    void DrainPanelFactories(PanelManager& panels);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mye::editor
