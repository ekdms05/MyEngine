// HierarchyPanel.cpp — 하이어라키 패널(World 트리·선택·드래그드롭 재부모화) (docs/07 하이어라키)
//
// 07: 씬 World의 엔티티 계층을 트리로 표시한다. 루트(부모 없음)부터 Children 캐시를 따라 재귀
//   드로우. 클릭=선택(Ctrl 토글/Shift 추가), 드래그드롭=재부모화(ReparentCommand). 컨텍스트
//   메뉴=Create/Delete(구조 커맨드). 모든 편집은 IEditorCommand 경유(패널이 World 직접 변경 금지).
//
// 소유: 내장 패널도 1급 플러그인 — 다른 패널과 동일하게 IEditorPanelFactory로 등록된다.
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/PlayMode.h"

#include "mye/ecs/World.h"
#include "mye/scene/Transform.h"

#include "mye/core/I18n.h"

#include "imgui.h"

#include <cstring>
#include <string>
#include <vector>

namespace mye::editor {

namespace {

const PanelDesc kHierarchyDesc{
    /*id*/ "mye.hierarchy",
    /*title*/ "하이어라키",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Left,
};

// 엔티티 표시 이름(리플렉션 Name 컴포넌트가 없는 baseline에서는 "Entity #idx").
std::string EntityLabel(ecs::Entity e) {
    return "Entity #" + std::to_string(e.index);
}

class HierarchyPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kHierarchyDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin(PanelWindowTitle("panel.hierarchy", "mye.hierarchy").c_str())) {
            ImGui::End();
            return;
        }
        ecs::World* world = ctx.activeWorld();
        if (!world) {
            ImGui::TextDisabled("활성 씬 없음");
            ImGui::End();
            return;
        }

        // 빈 영역 우클릭 → 루트에 엔티티 생성.
        if (ImGui::BeginPopupContextWindow("hierarchy_ctx",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("엔티티 생성")) {
                PushCreate(ctx, ecs::Entity::Null());
            }
            ImGui::EndPopup();
        }

        // 루트 엔티티 수집(부모 없음)·중복 방지. 트리는 Children 캐시로 재귀.
        m_pendingReparent = {};
        m_pendingDelete = ecs::Entity::Null();
        m_pendingCreateChildOf = ecs::Entity::Null();
        m_hasPendingReparent = false;

        std::vector<ecs::Entity> roots = CollectRoots(*world);
        for (ecs::Entity r : roots) DrawNode(ctx, *world, r);

        // 지연 커맨드 발행(순회 중 구조 변경 금지 규약과 정합).
        if (m_hasPendingReparent)
            PushReparent(ctx, m_pendingReparent.child, m_pendingReparent.parent);
        if (!m_pendingDelete.IsNull())
            PushDelete(ctx, m_pendingDelete);
        if (!m_pendingCreateChildOf.IsNull())
            PushCreate(ctx, m_pendingCreateChildOf);

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.hierarchy"));
        out = json::Value(std::move(o));
    }

private:
    struct PendingReparent { ecs::Entity child; ecs::Entity parent; };

    static std::vector<ecs::Entity> CollectRoots(ecs::World& world) {
        // Parent 풀을 훑어 부모가 있는 엔티티를 집합화하는 대신, LocalTransform 보유 엔티티를
        //   후보로 열거하고 부모 없는 것만 루트로 삼는다(baseline: 편집 엔티티는 Transform 보유).
        std::vector<ecs::Entity> roots;
        world.Query<scene::LocalTransform>().Each([&](ecs::Entity e, scene::LocalTransform&) {
            scene::Parent* p = world.TryGet<scene::Parent>(e);
            if (!p || p->parent.IsNull() || !world.Valid(p->parent))
                roots.push_back(e);
        });
        return roots;
    }

    void DrawNode(EditorContext& ctx, ecs::World& world, ecs::Entity e) {
        if (!world.Valid(e)) return;
        scene::Children* ch = world.TryGet<scene::Children>(e);
        const bool leaf = !ch || ch->list.empty();

        const bool selected =
            ctx.selection && ctx.selection->IsSelected(SelectableRef::OfEntity(e));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(static_cast<int>(e.index));
        const bool open = ImGui::TreeNodeEx(EntityLabel(e).c_str(), flags);

        // 선택 처리(Ctrl 토글 / Shift 추가 / 단일 교체).
        if (ImGui::IsItemClicked() && ctx.selection) {
            SelectMode mode = SelectMode::Replace;
            if (ImGui::GetIO().KeyCtrl) mode = SelectMode::Toggle;
            else if (ImGui::GetIO().KeyShift) mode = SelectMode::Add;
            ctx.selection->Select(SelectableRef::OfEntity(e), mode);
        }

        // 드래그 소스 = 이 엔티티.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            const std::uint64_t packed = e.Packed();
            ImGui::SetDragDropPayload("MYE_ENTITY", &packed, sizeof(packed));
            ImGui::TextUnformatted(EntityLabel(e).c_str());
            ImGui::EndDragDropSource();
        }
        // 드롭 타겟 = 이 엔티티를 새 부모로 재부모화.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("MYE_ENTITY")) {
                std::uint64_t packed = 0;
                std::memcpy(&packed, pl->Data, sizeof(packed));
                ecs::Entity dragged = ecs::Entity::FromPacked(packed);
                if (dragged != e) {
                    m_pendingReparent = {dragged, e};
                    m_hasPendingReparent = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 항목 컨텍스트 메뉴.
        if (ImGui::BeginPopupContextItem("entity_ctx")) {
            if (ImGui::MenuItem("자식 엔티티 생성"))
                m_pendingCreateChildOf = e;
            if (ImGui::MenuItem("삭제"))
                m_pendingDelete = e;
            if (ImGui::MenuItem("루트로 이동")) {
                m_pendingReparent = {e, ecs::Entity::Null()};
                m_hasPendingReparent = true;
            }
            ImGui::EndPopup();
        }

        if (open && !leaf) {
            // 자식 목록 복사 후 순회(재부모화가 캐시를 변경할 수 있음).
            std::vector<ecs::Entity> kids = ch->list;
            for (ecs::Entity k : kids) DrawNode(ctx, world, k);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    static CommandStack* Stack(EditorContext& ctx) {
        // 07 §3·§4: 플레이 중이면 플레이 스택, 아니면 포커스 문서 스택.
        if (ctx.playMode && ctx.playMode->IsPlaying())
            return ctx.playMode->PlayCommandStack();
        return ctx.commands;
    }

    void PushCreate(EditorContext& ctx, ecs::Entity parent) {
        if (CommandStack* s = Stack(ctx))
            s->Push(std::make_unique<CreateEntityCommand>(parent));
    }
    void PushDelete(EditorContext& ctx, ecs::Entity e) {
        if (CommandStack* s = Stack(ctx))
            s->Push(std::make_unique<DestroyEntityCommand>(e));
    }
    void PushReparent(EditorContext& ctx, ecs::Entity child, ecs::Entity parent) {
        if (CommandStack* s = Stack(ctx))
            s->Push(std::make_unique<ReparentCommand>(child, parent, /*keepWorld*/ true));
    }

    PendingReparent m_pendingReparent{};
    bool            m_hasPendingReparent = false;
    ecs::Entity     m_pendingDelete = ecs::Entity::Null();
    ecs::Entity     m_pendingCreateChildOf = ecs::Entity::Null();
};

class HierarchyPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kHierarchyDesc; }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<HierarchyPanel>();
    }
};

} // namespace

std::unique_ptr<IEditorPanelFactory> MakeHierarchyPanelFactory() {
    return std::make_unique<HierarchyPanelFactory>();
}

} // namespace mye::editor
