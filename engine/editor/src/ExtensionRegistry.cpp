// ExtensionRegistry.cpp — 확장 등록·조회 골격 (M4-A: 활성 항목 저장/조회 구현)
//
// 07 §확장: 패널·메뉴·툴바·프로퍼티 드로어·컴포넌트 에디터를 저장하고 조회한다(내장=1급 플러그인).
//   드로어/에디터는 TypeId(refl) 키로 조회. 뷰포트 확장·컨텍스트 메뉴·설정 페이지는 저장만
//   하고 소비는 M4-B에서.
#include "mye/editor/ExtensionRegistry.h"
#include "mye/refl/TypeInfo.h"

#include <unordered_map>
#include <vector>

namespace mye::editor {

struct EditorExtensionRegistry::Impl {
    std::vector<MenuEntry>    menus;
    std::vector<ToolbarEntry> toolbars;

    std::unordered_map<refl::TypeId, std::unique_ptr<IPropertyDrawer>>  drawers;
    std::unordered_map<refl::TypeId, std::unique_ptr<IComponentEditor>> compEditors;

    // 후속(M4-B) 저장소 — 소비는 뷰포트/컨텍스트/설정 구현에서.
    std::vector<std::unique_ptr<IGizmoExtension>>  gizmos;
    std::vector<std::unique_ptr<IViewportOverlay>> overlays;
    std::vector<std::unique_ptr<IViewportTool>>    tools;
    std::vector<std::unique_ptr<ISettingsPage>>    settingsPages;
};

EditorExtensionRegistry::EditorExtensionRegistry() : m_impl(std::make_unique<Impl>()) {}
EditorExtensionRegistry::~EditorExtensionRegistry() = default;

void EditorExtensionRegistry::AddPanel(std::unique_ptr<IEditorPanelFactory> /*factory*/) {
    // 골격: 패널은 PanelManager가 정본 소유. 확장 경로는 EditorApp이 PanelManager로 포워딩한다.
    // TODO(M4-B): EditorApp::Extensions 진입 시 PanelManager::RegisterFactory로 위임.
}

void EditorExtensionRegistry::AddMenuItem(const MenuPath& path, MenuItemDesc desc,
                                          MenuCallback onClick) {
    m_impl->menus.push_back(MenuEntry{path, std::move(desc), std::move(onClick)});
}

void EditorExtensionRegistry::AddToolbarButton(ToolbarSection section, ToolbarButtonDesc desc,
                                               MenuCallback onClick) {
    m_impl->toolbars.push_back(ToolbarEntry{section, std::move(desc), std::move(onClick)});
}

void EditorExtensionRegistry::AddPropertyDrawer(const refl::TypeInfo& type,
                                                std::unique_ptr<IPropertyDrawer> drawer) {
    m_impl->drawers[type.Id()] = std::move(drawer);
}
void EditorExtensionRegistry::AddComponentEditor(const refl::TypeInfo& componentType,
                                                 std::unique_ptr<IComponentEditor> editor) {
    m_impl->compEditors[componentType.Id()] = std::move(editor);
}

IPropertyDrawer* EditorExtensionRegistry::FindPropertyDrawer(const refl::TypeInfo& type) const {
    auto it = m_impl->drawers.find(type.Id());
    return it == m_impl->drawers.end() ? nullptr : it->second.get();
}
IComponentEditor*
EditorExtensionRegistry::FindComponentEditor(const refl::TypeInfo& componentType) const {
    auto it = m_impl->compEditors.find(componentType.Id());
    return it == m_impl->compEditors.end() ? nullptr : it->second.get();
}

void EditorExtensionRegistry::AddGizmo(std::unique_ptr<IGizmoExtension> gizmo) {
    m_impl->gizmos.push_back(std::move(gizmo));
}
void EditorExtensionRegistry::AddViewportOverlay(std::unique_ptr<IViewportOverlay> overlay) {
    m_impl->overlays.push_back(std::move(overlay));
}
void EditorExtensionRegistry::AddViewportTool(std::unique_ptr<IViewportTool> tool) {
    m_impl->tools.push_back(std::move(tool));
}
void EditorExtensionRegistry::AddAssetContextMenu(AssetTypeFilter /*filter*/, MenuItemDesc /*desc*/,
                                                  AssetMenuCallback /*cb*/) {
    // TODO(M4-B): 에셋 브라우저 컨텍스트 메뉴 저장/소비.
}
void EditorExtensionRegistry::AddEntityContextMenu(MenuItemDesc /*desc*/,
                                                   EntityMenuCallback /*cb*/) {
    // TODO(M4-B): 하이어라키/뷰포트 엔티티 컨텍스트 메뉴 저장/소비.
}
void EditorExtensionRegistry::AddSettingsPage(std::string_view /*category*/,
                                              std::unique_ptr<ISettingsPage> page) {
    m_impl->settingsPages.push_back(std::move(page));
}

std::span<const EditorExtensionRegistry::MenuEntry>
EditorExtensionRegistry::MenuEntries() const { return m_impl->menus; }
std::span<const EditorExtensionRegistry::ToolbarEntry>
EditorExtensionRegistry::ToolbarEntries() const { return m_impl->toolbars; }

} // namespace mye::editor
