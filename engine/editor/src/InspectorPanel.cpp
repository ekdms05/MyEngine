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

class InspectorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kInspectorDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin("인스펙터")) {
            ImGui::End();
            return;
        }

        ecs::World* world = ctx.activeWorld();
        SelectionManager* sel = ctx.selection;
        if (!world || !sel || sel->Empty()) {
            ImGui::TextDisabled("선택된 엔티티가 없습니다.");
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
        for (const refl::TypeInfo* t : refl::TypeRegistry::Get().All()) {
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

            if (open && insp) {
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
                if (world.HasDynamic(entity, cid)) continue;
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
