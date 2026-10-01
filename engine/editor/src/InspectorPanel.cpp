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
#include "mye/core/I18n.h"
#include "mye/runtime/ObjectComponents.h"
#include "mye/ser/JsonArchive.h"
#include "mye/core/Log.h"

#include "mye/ecs/World.h"
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

bool EditText(const char* label, std::string& text, bool multiline = false) {
    std::vector<char> buffer(multiline ? 65537 : std::max<std::size_t>(1024, text.size() + 256), 0);
    std::copy_n(text.data(), std::min(text.size(), buffer.size() - 1), buffer.data());
    const bool changed = multiline
        ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(-1, 260), ImGuiInputTextFlags_AllowTabInput)
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
    if (ImGui::TreeNode("오브젝트 Lua")) {
        ImGui::TextWrapped("return 테이블 규약. on_start(self), on_update(self, dt), on_interact(self), on_trigger_enter(self, other)를 사용합니다. 변경 내용은 씬 저장에 포함되며 Play를 다시 시작하면 반영됩니다.");
        if (edited.luaSource.empty() && ImGui::Button("기본 코드 만들기")) {
            edited.luaSource = "return {\n    on_interact = function(self)\n        print(\"interacted\")\n    end,\n}\n"; changed = true;
        }
        changed |= EditText("##lua_source", edited.luaSource, true);
        ImGui::TreePop();
    }
    if (changed) {
        auto before = BehaviorBlob(current), after = BehaviorBlob(edited);
        if (!before || !after) { MYE_LOG_ERROR("Editor", "Could not serialize object behavior"); return; }
        if (auto* stack = Stack(ctx)) stack->Push(std::make_unique<PropertyEditCommand>(ObjectRef::Component(entity, *refl::GetType<runtime::ObjectBehavior>()), refl::PropertyPath{}, before.Value(), after.Value(), "Edit Object Behavior"));
    }
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

            ImGui::SetNextItemAllowOverlap();
            bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                                                ImGuiTreeNodeFlags_AllowOverlap);
            // 헤더 우측에 제거 버튼(트랜스폼 등 필수 컴포넌트도 규약상 허용 — Undo로 복원).
            ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
            if (ImGui::SmallButton("제거"))
                m_pendingRemove = t;

            if (open && cid == runtime::ObjectBehavior::kComponentTypeId) {
                DrawObjectBehavior(ctx, entity, *static_cast<runtime::ObjectBehavior*>(comp));
            } else if (open && insp) {
                insp->DrawReflected(ctx, ObjectRef::Component(entity, *t), *t, comp,
                                    refl::PropertyPath{});
            }
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

} // namespace mye::editor
