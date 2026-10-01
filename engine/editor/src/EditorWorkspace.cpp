#include "mye/editor/EditorApp.h"
#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/Viewport.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/scene/Transform.h"
#include "mye/scene/Camera3D.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/phys/Collision.h"
#include "mye/ser/JsonArchive.h"
#include "mye/refl/TypeRegistry.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace mye::editor {
namespace {
using Element = EditorApp::SceneElement;
struct ElementDescription { Element kind; const char* name; const char* description; };
constexpr std::array kElements{
    ElementDescription{Element::Object, "오브젝트", "계층과 위치를 가진 빈 오브젝트"},
    ElementDescription{Element::Sprite, "스프라이트", "PNG 이미지를 지정해 표시하는 2D 오브젝트"},
    ElementDescription{Element::Character, "캐릭터", "WASD / 방향키 이동, 충돌, 속도·모션 설정. 씬 최상위에 배치"},
    ElementDescription{Element::Collider, "충돌 영역", "벽·건물 등 이동을 막는 박스 영역"},
    ElementDescription{Element::Trigger, "트리거", "진입·이탈 이벤트를 감지하는 영역"},
    ElementDescription{Element::Interaction, "상호작용", "E 키 상호작용 대상과 이벤트 연결"},
    ElementDescription{Element::Spawn, "도착 지점", "맵 포털에서 이름으로 지정하는 도착 위치"},
    ElementDescription{Element::Lua, "Lua 오브젝트", "씬에 저장되는 오브젝트별 Lua 코드와 이벤트"},
    ElementDescription{Element::Mesh, "3D 메시", "정적 GLB/glTF와 알베도 PNG를 지정하는 오브젝트"},
    ElementDescription{Element::Billboard, "3D 빌보드", "카메라를 향하는 PNG/.anim. 충돌은 별도로 구성"},
    ElementDescription{Element::Camera, "게임 카메라 3D", "Play/MyGame의 저장 가능한 원근 카메라"},
};

struct ComponentValue { const refl::TypeInfo* type; ValueBlob before, after; };
template<class T>
Expected<void, Error> PrepareComponent(ecs::World& world, const T& value, std::vector<ComponentValue>& out) {
    const auto* type = refl::TypeRegistry::Get().Find(static_cast<refl::TypeId>(T::kComponentTypeId));
    if (!type || !world.IsRegistered(static_cast<ecs::ComponentTypeId>(type->Id())))
        return Error{"씬에 필요한 컴포넌트가 등록되지 않았습니다.", 1};
    const T initial{};
    auto before = ser::JsonArchive::ForWrite(), after = ser::JsonArchive::ForWrite();
    auto a = refl::ReadValue(*type, &initial, {}, before);
    auto b = refl::ReadValue(*type, &value, {}, after);
    if (!a) return a.GetError();
    if (!b) return b.GetError();
    out.push_back({type, {json::Stringify(before.Root())}, {json::Stringify(after.Root())}});
    return {};
}

class LuaPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override {
        static const PanelDesc desc{"mye.lua", "Lua", false, DockSlot::Center};
        return desc;
    }
    void OnGui(EditorContext& ctx) override {
        if (ImGui::Begin("Lua###mye.lua")) DrawObjectLua(ctx, std::max(100.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4));
        ImGui::End();
    }
};
class WorkspacePanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return m_description.Desc(); }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<LuaPanel>(); }
private:
    LuaPanel m_description;
};
}

std::unique_ptr<IEditorPanelFactory> MakeLuaPanelFactory() { return std::make_unique<WorkspacePanelFactory>(); }

std::string_view EditorApp::CentralPanelId() const {
    switch (m_workspace) {
    case Workspace::Lua: return "mye.lua";
    default: return "mye.viewport";
    }
}

void EditorApp::SelectWorkspace(Workspace workspace) {
    m_workspace = workspace;
    m_animationFocused = false;
    if (m_playMode) m_playMode->SetInputEnabled(false);
    if (m_viewport && (workspace == Workspace::Scene2D || workspace == Workspace::Scene3D)) {
        auto camera = m_viewport->Camera();
        camera.perspective = workspace == Workspace::Scene3D;
        m_viewport->SetCamera(camera);
    }
    if (m_panels) { m_panels->Open(CentralPanelId()); m_panels->Focus(CentralPanelId()); }
}

Expected<void, Error> EditorApp::CreateScene(bool perspective) {
    if (!m_project || !m_project->IsOpen() || (m_playMode && m_playMode->IsPlaying()))
        return Error{"프로젝트를 열고 실행을 중지한 뒤 씬을 만드세요.", 1};
    m_project->NewScene();
    RefreshDocumentContext();
    m_selectDocumentTab = true;
    SelectWorkspace(perspective ? Workspace::Scene3D : Workspace::Scene2D);
    return {};
}

Expected<ecs::Entity, Error> EditorApp::CreateSceneElement(SceneElement element, ecs::Entity parent) {
    auto* world = m_ctx.activeWorld();
    if (!m_project || !m_project->IsOpen() || !world || !m_ctx.commands || m_playMode->IsPlaying())
        return Error{"편집 중인 씬에서 요소를 추가하세요.", 1};
    if (!parent.IsNull() && !world->Valid(parent)) return Error{"선택한 부모 오브젝트가 없습니다.", 1};
    const auto description = std::find_if(kElements.begin(), kElements.end(), [element](const auto& d) { return d.kind == element; });
    if (description == kElements.end()) return Error{"지원하지 않는 씬 요소입니다.", 1};
    if (element == SceneElement::Character) {
        if (!parent.IsNull()) return Error{"캐릭터는 씬 최상위에 배치하세요.", 1};
        bool occupied = false;
        world->Query<runtime::CharacterController2D>().Each([&](ecs::Entity, const auto& c) { occupied |= c.enabled; });
        if (occupied) return Error{"씬에 조작 캐릭터가 이미 있습니다. 기존 캐릭터 조작을 먼저 끄세요.", 1};
        for (const auto* name : {"SpriteRenderer", "Collider2D", "KinematicBody2D", "CharacterController2D"}) {
            const auto* type = refl::TypeRegistry::Get().Find(name);
            if (!type || !world->IsRegistered(static_cast<ecs::ComponentTypeId>(type->Id())))
                return Error{"캐릭터 컴포넌트가 등록되지 않았습니다.", 1};
        }
    }

    std::unordered_set<std::string> names;
    world->Query<scene::ObjectName>().Each([&](ecs::Entity, const auto& n) { names.insert(n.value); });
    std::string name = description->name;
    for (int suffix = 2; names.contains(name); ++suffix) name = std::string(description->name) + " " + std::to_string(suffix);
    std::vector<ComponentValue> components;
    auto prepared = PrepareComponent(*world, scene::ObjectName{name}, components);
    if (prepared && element == SceneElement::Sprite) prepared = PrepareComponent(*world, scene::SpriteRenderer{}, components);
    if (prepared && element == SceneElement::Mesh) prepared = PrepareComponent(*world, scene::MeshRenderer{}, components);
    if (prepared && element == SceneElement::Billboard) prepared = PrepareComponent(*world, scene::BillboardRenderer{}, components);
    if (prepared && element == SceneElement::Camera) {
        bool occupied = false;
        world->Query<scene::Camera3D>().Each([&](ecs::Entity, const auto& c) { occupied |= c.current; });
        scene::Camera3D camera; camera.current = !occupied;
        prepared = PrepareComponent(*world, camera, components);
        scene::LocalTransform transform; transform.position = {0, 3, -8};
        if (prepared) prepared = PrepareComponent(*world, transform, components);
    }
    if (prepared && (element == SceneElement::Collider || element == SceneElement::Trigger)) {
        phys::Collider2D collider;
        collider.shape = phys::Shape2D::MakeBox(.5f, .5f);
        collider.isTrigger = element == SceneElement::Trigger;
        prepared = PrepareComponent(*world, collider, components);
    }
    if (prepared && element == SceneElement::Interaction) prepared = PrepareComponent(*world, runtime::InteractionTarget{}, components);
    if (prepared && (element == SceneElement::Lua || element == SceneElement::Interaction || element == SceneElement::Trigger))
        prepared = PrepareComponent(*world, runtime::ObjectBehavior{}, components);
    if (!prepared) return prepared.GetError();

    auto create = std::make_unique<CreateEntityCommand>(parent);
    auto* command = create.get();
    m_ctx.commands->BeginTransaction("씬 요소 추가");
    m_ctx.commands->Push(std::move(create));
    const auto entity = command->Created();
    for (const auto& component : components) {
        m_ctx.commands->Push(std::make_unique<AddComponentCommand>(entity, *component.type));
        m_ctx.commands->Push(std::make_unique<PropertyEditCommand>(ObjectRef::Component(entity, *component.type),
            refl::PropertyPath{}, component.before, component.after));
    }
    auto movement = element == SceneElement::Character ? SetupCharacterMovement(m_ctx, entity) : Expected<void, Error>{};
    m_ctx.commands->EndTransaction();
    if (!movement) { m_ctx.commands->Undo(); return movement.GetError(); }
    if (m_selection) m_selection->Select(SelectableRef::OfEntity(entity));
    return entity;
}

void EditorApp::RequestAddElement(ecs::Entity parent) {
    if (!m_project || !m_project->Active() || m_playMode->IsPlaying()) return;
    m_elementParent = parent;
    m_elementDocument = m_project->Active()->Id();
    m_elementSearch.fill(0);
    m_elementChoice = SceneElement::Object;
    m_fileError = false;
    m_showAddElement = true;
}

void EditorApp::DrawWorkspaceDialogs() {
    if (m_showNewScene) { ImGui::OpenPopup("씬 만들기###new_scene"); m_showNewScene = false; }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 27, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("씬 만들기###new_scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("어떤 씬을 만드시겠습니까?");
        ImGui::TextWrapped("2D는 평면 보기, 3D는 원근 보기로 시작합니다. 상단에서 언제든 전환할 수 있습니다.");
        if (m_fileError) ImGui::TextWrapped("%s", m_fileStatus.c_str());
        if (ImGui::Button("2D 씬", ImVec2(ImGui::GetFontSize() * 6, 0))) {
            auto result = CreateScene(false); ReportFileResult(result, "2D 씬을 만들었습니다.");
            if (result) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("3D 씬", ImVec2(ImGui::GetFontSize() * 6, 0))) {
            auto result = CreateScene(true); ReportFileResult(result, "3D 보기의 씬을 만들었습니다.");
            if (result) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("취소") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (m_showAddElement) { ImGui::OpenPopup("요소 추가###add_scene_element"); m_showAddElement = false; }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 38, ImGui::GetFontSize() * 36), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("요소 추가###add_scene_element", nullptr)) {
        ImGui::TextUnformatted("씬에 추가할 요소를 선택하세요.");
        ImGui::TextWrapped("추가 후 인스펙터에서 이미지·속도·충돌과 동작을 설정합니다.");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##element_search", "요소 검색...", m_elementSearch.data(), m_elementSearch.size());
        const auto matches = [&](const auto& d) {
            return std::string_view(d.name).find(m_elementSearch.data()) != std::string_view::npos ||
                std::string_view(d.description).find(m_elementSearch.data()) != std::string_view::npos;
        };
        const auto firstMatch = std::find_if(kElements.begin(), kElements.end(), matches);
        if (firstMatch != kElements.end() && !std::any_of(kElements.begin(), kElements.end(), [&](const auto& d) { return d.kind == m_elementChoice && matches(d); }))
            m_elementChoice = firstMatch->kind;
        bool accepted = false, found = false;
        const float footer = ImGui::GetFontSize() * (m_fileError ? 6 : 4) + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2;
        if (ImGui::BeginChild("##elements", ImVec2(0, std::max(60.0f, ImGui::GetContentRegionAvail().y - footer)), ImGuiChildFlags_Borders)) {
            for (const auto& d : kElements) {
                if (!matches(d)) continue;
                found = true;
                if (ImGui::Selectable(d.name, d.kind == m_elementChoice, ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, ImGui::GetFrameHeight()))) {
                    m_elementChoice = d.kind;
                    accepted = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                }
            }
            if (!found) ImGui::TextDisabled("검색 결과가 없습니다.");
        }
        ImGui::EndChild();
        const auto choice = std::find_if(kElements.begin(), kElements.end(), [this](const auto& d) { return d.kind == m_elementChoice; });
        if (found && choice != kElements.end()) {
            ImGui::TextUnformatted(choice->name);
            ImGui::TextWrapped("%s", choice->description);
        }
        ImGui::Separator();
        if (m_fileError) ImGui::TextWrapped("%s", m_fileStatus.c_str());
        ImGui::BeginDisabled(!found);
        accepted |= ImGui::Button("추가", ImVec2(ImGui::GetFontSize() * 6, 0)) || (found && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
        ImGui::EndDisabled();
        if (accepted) {
            if (!m_project->Active() || m_project->Active()->Id() != m_elementDocument) {
                ReportFileResult(Error{"씬이 바뀌었습니다. 요소 추가 창을 다시 여세요.", 1}, "");
            } else {
                auto created = CreateSceneElement(m_elementChoice,
                    m_elementChoice == SceneElement::Character ? ecs::Entity::Null() : m_elementParent);
                ReportFileResult(created ? Expected<void, Error>{} : Expected<void, Error>{created.GetError()}, "요소를 추가했습니다. Inspector에서 속성을 지정하세요.");
                if (created) {
                    const bool perspective = m_viewport && m_viewport->Camera().perspective;
                    SelectWorkspace(m_elementChoice == SceneElement::Lua ? Workspace::Lua : perspective ? Workspace::Scene3D : Workspace::Scene2D);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("취소", ImVec2(ImGui::GetFontSize() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
} // namespace mye::editor
