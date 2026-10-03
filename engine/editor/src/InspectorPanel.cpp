// InspectorPanel.cpp — 인스펙터 패널(선택 엔티티 컴포넌트 UI + 컴포넌트 추가/제거)
//   (docs/07 인스펙터)
//
// 07 인스펙터: 선택된 엔티티(SelectionManager::Primary)의 컴포넌트를 InspectorRenderer::
//   DrawEntity 로 자동 UI 생성한다(리플렉션 순회→위젯→PropertyEditCommand). 패널은 값 편집을
//   직접 하지 않고, 이 렌더러가 커맨드를 발행한다. 추가로 [+ 컴포넌트 추가] 버튼(리플렉션 등록
//   Struct 타입 목록)과 컴포넌트별 [제거]를 AddComponentCommand/RemoveComponentCommand 로
//   발행한다. 다중 선택 시 주 선택(primary) 기준으로 표시한다(다중 편집 확장은 후속).
//
// 소유: 내장 패널도 1급 플러그인 — IEditorPanelFactory 로 등록(RegisterBuiltinPanels 경유).
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Inspector.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/PlayMode.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/BuiltinPanels.h"
#include "mye/core/I18n.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/ser/JsonArchive.h"
#include "mye/core/Log.h"

#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "mye/ecs/ComponentType.h"
#include "mye/refl/TypeInfo.h"
#include "mye/refl/TypeId.h"
#include "mye/refl/TypeRegistry.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mye::editor {

namespace {
const char* ComponentHelp(std::string_view name) {
    static constexpr std::pair<std::string_view, const char*> descriptions[]{
        {"ObjectName", "오브젝트의 고유 이름입니다. 이벤트 대상과 맵 도착 지점에서 이 이름을 사용합니다."},
        {"LocalTransform", "부모 기준 위치·회전·크기입니다. +Y는 위쪽이며 1 단위는 기본 48 픽셀입니다."},
        {"WorldTransform", "계층에서 계산되는 월드 변환입니다. 위치는 LocalTransform에서 편집하세요."},
        {"Parent", "부모 오브젝트 연결입니다. 하이어라키에서 이동하여 계층을 편집하세요."},
        {"Children", "자식 목록은 계층 변경 시 관리됩니다. 하이어라키에서 편집하세요."},
        {"SpriteRenderer", "PNG를 드래그해 sprite에 지정합니다. UV는 0~1 비율, 피벗은 잘라낸 영역의 픽셀 기준입니다."},
        {"BillboardRenderer", "3D 공간의 PNG/.anim 표시입니다. mode에서 카메라 전체 축, Y축 유지 또는 고정 방향을 선택합니다. 3D 충돌체는 제공하지 않습니다."},
        {"MeshRenderer", "정적 GLB/glTF를 mesh에, 알베도 PNG를 material에 드래그하세요. 리깅·glTF 재질 자동 변환은 지원하지 않습니다."},
        {"Camera2D", "2D/3D 카메라의 current는 하나만 켭니다. followTarget은 고유 이름이며 비우면 자신의 월드 XY를 추종합니다. deadzoneHalf는 월드 단위, bounds는 최소 XY와 크기입니다. 휠로 확대·축소하며 Lua에서 줌·흔들림·좌표 변환을 사용할 수 있습니다."},
        {"Camera3D", "2D/3D 카메라의 current는 하나만 켭니다. followTarget은 추종 대상 이름입니다. orbitEnabled를 켜면 Q/R · 오른쪽 드래그로 360도 회전하며 distance · pitchDegrees · 감도를 설정할 수 있습니다."},
        {"CharacterController3D", "XZ 이동 · Space 점프. Collider3D와 KinematicBody3D가 필요합니다. 루트·회전 없음·양수 스케일을 사용하세요. Collider의 로컬 크기도 배율을 적용합니다. cameraRelative는 카메라 기준 이동이며 speed · 중력 · 점프는 body.settings에서 설정합니다."},
        {"Collider3D", "XYZ 충돌 상자 또는 +Z 경사입니다. half는 반크기, offset은 중심입니다. isTrigger는 충돌 없이 진입·이탈 이벤트를 만듭니다. 회전·음수 스케일은 지원하지 않습니다."},
        {"KinematicBody3D", "캐릭터의 중력·점프·바닥/벽/천장·경사·낮은 턱 이동을 계산합니다. 동적 강체와 임의 삼각형 메시 충돌은 지원하지 않습니다."},
        {"FloorLevel", "높이 층입니다. 렌더 가림과 충돌 층을 함께 확인하세요. 일반 지면은 0입니다."},
        {"SpriteAnimator", ".anim 에셋을 드래그하여 지정합니다. speed는 재생 배율, playing은 자동 재생 여부입니다."},
        {"Collider2D", "벽·캐릭터의 충돌 범위입니다. 트리거는 이동을 막지 않고 진입·이탈 이벤트를 보냅니다."},
        {"KinematicBody2D", "고정 틱에서 충돌하며 이동하는 본체입니다. 기본값으로 시작하고 충돌 가장자리에서만 조정하세요."},
        {"CharacterController2D", "WASD·방향키로 조작합니다. speed는 단위/초입니다. 활성 캐릭터는 씬 최상위에 1명만 둡니다."},
        {"InteractionTarget", "캐릭터가 radius 안에서 E를 누르면 상호작용합니다. ObjectBehavior에 실행할 행동을 연결하세요."},
        {"GameUi", "저장한 .ui 에셋을 document에 드래그하세요. 로컬 Play/MyGame의 화면 좌표로 표시되며 ObjectBehavior Lua의 mye.ui로 갱신합니다. 버튼 클릭·텍스트 필드/키보드 포커스·모달 입력 차단을 지원합니다. 실제 IME 장치/후보창 검수·온라인 UI는 별도입니다."},
        {"ScenePortal", "프로젝트의 씬 경로와 목적지의 고유 오브젝트 이름을 지정합니다. E 또는 트리거로 이동합니다."},
        {"ObjectBehavior", "이벤트에서 행동을 순서대로 실행합니다. 연결 설정 또는 Lua 콜백 중 필요한 방식으로 작성하세요."},
        {"Progression", "캐릭터 레벨과 현재 레벨의 경험치입니다. 성장 규칙은 게임 콘텐츠에서 정합니다."},
    };
    for (const auto& [type, description] : descriptions) if (name == type) return description;
    return "등록된 컴포넌트입니다. 필드에 마우스를 올리면 사용 설명을 확인할 수 있습니다.";
}
}

namespace {

const PanelDesc kInspectorDesc{
    /*id*/ "mye.inspector",
    /*title*/ "인스펙터",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Right,
};

ecs::ComponentTypeId ComponentIdOf(const refl::TypeInfo& t) {
    return static_cast<ecs::ComponentTypeId>(t.Id());
}

// 07 §3·§4: 편집 대상 스택 — 플레이 중이면 플레이 스택, 아니면 포커스 문서 스택.
CommandStack* Stack(EditorContext& ctx) {
    if (ctx.playMode && ctx.playMode->IsPlaying())
        return ctx.playMode->PlayCommandStack();
    return ctx.commands;
}

bool EditText(const char* label, std::string& text, bool multiline = false, float height = 260) {
    std::vector<char> buffer(multiline ? 65537 : std::max<std::size_t>(1024, text.size() + 256), 0);
    std::copy_n(text.data(), std::min(text.size(), buffer.size() - 1), buffer.data());
    const bool changed = multiline
        ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(-1, height), ImGuiInputTextFlags_AllowTabInput)
        : ImGui::InputText(label, buffer.data(), buffer.size());
    if (changed) text = buffer.data();
    return changed;
}
Expected<ValueBlob, Error> BehaviorBlob(const runtime::ObjectBehavior& data) {
    auto archive = ser::JsonArchive::ForWrite();
    auto result = refl::ReadValue(*refl::GetType<runtime::ObjectBehavior>(), &data, {}, archive);
    if (!result) return result.GetError();
    return ValueBlob{json::Stringify(archive.Root(), 0)};
}
void DrawObjectBehavior(EditorContext& ctx, ecs::Entity entity, const runtime::ObjectBehavior& current) {
    auto edited = current;
    bool changed = false;
    ImGui::TextWrapped("이벤트에서 동작으로 연결합니다. 같은 이벤트는 위에서 아래 순서로 실행됩니다.");
    static const char* events[] = {"시작", "상호작용", "트리거 진입", "트리거 이탈"};
    static const char* actions[] = {"메시지", "표시/숨김", "위치 변경", "맵 이동", "Lua 함수"};
    int remove = -1;
    for (std::size_t i = 0; i < edited.connections.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        auto& connection = edited.connections[i];
        ImGui::BeginGroup();
        int event = static_cast<int>(connection.event), action = static_cast<int>(connection.action);
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("##event", &event, events, 4)) { connection.event = static_cast<runtime::ObjectEvent>(event); changed = true; }
        const ImVec2 start = ImGui::GetItemRectMax();
        ImGui::SameLine(); ImGui::Dummy(ImVec2(26, 1)); ImGui::SameLine();
        const ImVec2 end = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + 2, start.y - 10), ImVec2(end.x - 3, start.y - 10), IM_COL32(90,180,230,255), 2);
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("##action", &action, actions, 5)) { connection.action = static_cast<runtime::ObjectAction>(action); changed = true; }
        ImGui::SameLine(); if (ImGui::SmallButton("삭제")) remove = static_cast<int>(i);
        if (connection.action == runtime::ObjectAction::ChangeMap) {
            changed |= EditText("씬 경로", connection.text);
            changed |= EditText("도착 오브젝트 이름", connection.target);
            ImGui::TextDisabled("예: assets/scenes/cottage.scene");
        } else {
            if (connection.action != runtime::ObjectAction::Message) {
                changed |= EditText("대상 이름", connection.target);
                ImGui::TextDisabled("비워 두면 현재 오브젝트");
            }
            if (connection.action == runtime::ObjectAction::Message || connection.action == runtime::ObjectAction::LuaCallback)
                changed |= EditText(connection.action == runtime::ObjectAction::Message ? "내용" : "함수 이름", connection.text);
            if (connection.action == runtime::ObjectAction::MoveTo) {
                changed |= ImGui::DragFloat("X", &connection.x, .05f);
                changed |= ImGui::DragFloat("Y", &connection.y, .05f);
            }
            if (connection.action == runtime::ObjectAction::SetVisible) changed |= ImGui::Checkbox("표시", &connection.visible);
        }
        ImGui::EndGroup(); ImGui::Separator(); ImGui::PopID();
    }
    if (remove >= 0) { edited.connections.erase(edited.connections.begin() + remove); changed = true; }
    ImGui::BeginDisabled(edited.connections.size() >= 64);
    if (ImGui::Button("+ 이벤트 → 동작")) { edited.connections.emplace_back(); changed = true; }
    ImGui::EndDisabled();
    if (changed) {
        auto before = BehaviorBlob(current), after = BehaviorBlob(edited);
        if (!before || !after) { MYE_LOG_ERROR("Editor", "Could not serialize object behavior"); return; }
        if (auto* stack = Stack(ctx)) stack->Push(std::make_unique<PropertyEditCommand>(ObjectRef::Component(entity, *refl::GetType<runtime::ObjectBehavior>()), refl::PropertyPath{}, before.Value(), after.Value(), "Edit Object Behavior"));
    }
    if (ImGui::TreeNode("오브젝트 Lua")) { DrawObjectLua(ctx, 260); ImGui::TreePop(); }
}

class InspectorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kInspectorDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin(PanelWindowTitle("panel.inspector", "mye.inspector").c_str())) {
            ImGui::End();
            return;
        }

        ecs::World* world = ctx.activeWorld();
        SelectionManager* sel = ctx.selection;
        if (!world || !sel || sel->Empty()) {
            ImGui::TextDisabled("%s", mye::i18n::T("inspector.empty"));
            ImGui::End();
            return;
        }

        const SelectableRef primary = sel->Primary();
        if (!primary.IsEntity()) {
            ImGui::TextDisabled("엔티티가 아닌 선택은 표시할 수 없습니다.");
            ImGui::End();
            return;
        }

        const ecs::Entity entity = primary.AsEntity();
        if (!world->Valid(entity)) {
            ImGui::TextDisabled("(무효 엔티티)");
            ImGui::End();
            return;
        }

        // 다중 선택 안내.
        const std::size_t count = sel->Current().size();
        if (count > 1)
            ImGui::TextDisabled("%zu개 선택 — 주 선택 표시", count);

        ImGui::Text("Entity #%u", entity.index);
        ImGui::Separator();

        ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
        if (ImGui::Button("캐릭터 기본 이동 구성")) {
            auto result = SetupCharacterMovement(ctx, entity);
            m_movementStatus = result ? "WASD / 방향키로 이동합니다. 아래에서 속도·충돌 크기와 대기/걷기 모션을 지정하세요." : result.GetError().message;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("필수 컴포넌트를 한 번에 구성합니다. 기존 설정은 보존하고 Ctrl+Z로 되돌립니다.");
        if (!m_movementStatus.empty()) ImGui::TextWrapped("%s", m_movementStatus.c_str());

        // 리플렉션 자동 인스펙터 — 컴포넌트별 헤더·필드 위젯·PropertyEditCommand 발행.
        //   컴포넌트 제거 버튼을 각 헤더 옆에 겹쳐 그리기 위해, DrawEntity 대신 여기서 컴포넌트를
        //   직접 순회한다(DrawReflected 재사용).
        DrawComponents(ctx, *world, entity);

        ImGui::Separator();
        DrawAddComponent(ctx, *world, entity);

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.inspector"));
        out = json::Value(std::move(o));
    }

private:
    std::string m_movementStatus;
    void DrawComponents(EditorContext& ctx, ecs::World& world, ecs::Entity entity) {
        InspectorRenderer* insp = ctx.app ? &ctx.app->Inspector() : nullptr;

        m_pendingRemove = nullptr;
        const auto registered = refl::TypeRegistry::Get().All();
        std::vector<const refl::TypeInfo*> ordered(registered.begin(), registered.end());
        const auto behavior = std::find_if(ordered.begin(), ordered.end(), [](const auto* t) { return t && t->Id() == runtime::ObjectBehavior::kComponentTypeId; });
        if (behavior != ordered.end()) std::rotate(ordered.begin(), behavior, behavior + 1);
        for (const refl::TypeInfo* t : ordered) {
            if (!t || t->GetKind() != refl::Kind::Struct) continue;
            const ecs::ComponentTypeId cid = ComponentIdOf(*t);
            if (!world.IsRegistered(cid)) continue;
            void* comp = world.TryGetDynamic(entity, cid);
            if (!comp) continue;

            const std::string header(t->Name());
            ImGui::PushID(header.c_str());

            const bool basic = header == "LocalTransform" || header == "SpriteRenderer" || header == "CharacterController2D";
            bool open = ImGui::CollapsingHeader(header.c_str(), basic ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s\n우클릭으로 컴포넌트를 제거할 수 있습니다.", ComponentHelp(header));
            if (ImGui::BeginPopupContextItem("##component_actions")) {
                if (ImGui::MenuItem("컴포넌트 제거")) m_pendingRemove = t;
                ImGui::EndPopup();
            }
            if (open) ImGui::TextWrapped("%s", ComponentHelp(header));

            if (open && cid == runtime::ObjectBehavior::kComponentTypeId) {
                DrawObjectBehavior(ctx, entity, *static_cast<runtime::ObjectBehavior*>(comp));
            } else if (open && insp) {
                insp->DrawReflected(ctx, ObjectRef::Component(entity, *t), *t, comp,
                                    refl::PropertyPath{});
            }
            ImGui::Spacing();
            ImGui::PopID();
        }

        // 순회 후 지연 제거(순회 중 구조 변경 금지 규약과 정합).
        if (m_pendingRemove) {
            if (CommandStack* s = Stack(ctx))
                s->Push(std::make_unique<RemoveComponentCommand>(entity, *m_pendingRemove));
            m_pendingRemove = nullptr;
        }
    }

    void DrawAddComponent(EditorContext& ctx, ecs::World& world, ecs::Entity entity) {
        if (ImGui::Button("+ 컴포넌트 추가"))
            ImGui::OpenPopup("add_component_popup");

        if (ImGui::BeginPopup("add_component_popup")) {
            // 검색 필터.
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", m_addSearch.c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputTextWithHint("##add_search", "컴포넌트 검색...", buf, sizeof(buf)))
                m_addSearch = buf;
            ImGui::Separator();

            const refl::TypeInfo* pick = nullptr;
            for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
                if (!t || t->GetKind() != refl::Kind::Struct) continue;
                // 이미 부착된 컴포넌트는 목록에서 제외.
                const ecs::ComponentTypeId cid = ComponentIdOf(*t);
                if (!world.IsRegistered(cid) || world.HasDynamic(entity, cid)) continue;
                const std::string name(t->Name());
                if (!m_addSearch.empty() && !ContainsCI(name, m_addSearch)) continue;
                if (ImGui::Selectable(name.c_str())) pick = t;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", ComponentHelp(name));
            }

            if (pick) {
                if (CommandStack* s = Stack(ctx))
                    s->Push(std::make_unique<AddComponentCommand>(entity, *pick));
                m_addSearch.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    static bool ContainsCI(const std::string& s, const std::string& q) {
        if (q.empty()) return true;
        auto it = std::search(s.begin(), s.end(), q.begin(), q.end(),
                              [](char a, char b) {
                                  return std::tolower((unsigned char)a) ==
                                         std::tolower((unsigned char)b);
                              });
        return it != s.end();
    }

    const refl::TypeInfo* m_pendingRemove = nullptr;
    std::string           m_addSearch;
};

class InspectorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kInspectorDesc; }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<InspectorPanel>();
    }
};

} // namespace

std::unique_ptr<IEditorPanelFactory> MakeInspectorPanelFactory() {
    return std::make_unique<InspectorPanelFactory>();
}

void DrawObjectLua(EditorContext& ctx, float height) {
    auto* world = ctx.activeWorld();
    const auto selected = ctx.selection ? ctx.selection->Primary() : SelectableRef{};
    if (!world || !selected.IsEntity() || !world->Valid(selected.AsEntity())) {
        ImGui::TextWrapped("왼쪽 계층에서 Lua를 작성할 오브젝트를 선택하세요. + 추가에서 Lua 오브젝트를 만들 수도 있습니다.");
        return;
    }
    const auto entity = selected.AsEntity();
    const auto* name = world->TryGet<scene::ObjectName>(entity);
    ImGui::Text("Lua · %s", name ? name->value.c_str() : "선택한 오브젝트");
    ImGui::TextWrapped("return 테이블: on_start(self), on_update(self, dt), on_interact(self), on_trigger_enter(self, other). 씬에 저장되며 실행을 다시 시작하면 반영됩니다.");
    ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
    const auto* behavior = world->TryGet<runtime::ObjectBehavior>(entity);
    if (!behavior) {
        if (ImGui::Button("Lua 추가") && ctx.commands)
            ctx.commands->Push(std::make_unique<AddComponentCommand>(entity, *refl::GetType<runtime::ObjectBehavior>()));
    } else if (behavior->luaSource.size() > 65536) {
        ImGui::TextWrapped("Lua 소스가 64 KiB 제한을 초과했습니다.");
    } else {
        auto text = behavior->luaSource;
        bool changed = false;
        if (text.empty() && ImGui::Button("기본 코드 만들기")) {
            text = "return {\n    on_interact = function(self)\n        print(\"interacted\")\n    end,\n}\n";
            changed = true;
        }
        changed |= EditText("##lua_source", text, true, height);
        if (changed && ctx.commands) {
            refl::PropertyPath path;
            path.segments.push_back(refl::PathSegment{std::string("luaSource")});
            ctx.commands->Push(std::make_unique<PropertyEditCommand>(ObjectRef::Component(entity, *refl::GetType<runtime::ObjectBehavior>()), path,
                ValueBlob{json::Stringify(json::Value(behavior->luaSource))}, ValueBlob{json::Stringify(json::Value(text))}, "Edit Object Lua"));
        }
    }
    ImGui::EndDisabled();
}

} // namespace mye::editor
