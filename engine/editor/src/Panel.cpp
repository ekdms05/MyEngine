// Panel.cpp — 패널 매니저 골격 (M4-A: 등록·오픈 로직, 도킹/드로우는 구현 에이전트가 완성)
//
// 07 §7: buildDockspaceAndDrawAll은 ImGui 도킹 스페이스 + Window 메뉴 자동 생성 + 전체 패널
//   OnGui. 골격은 등록·오픈·인스턴스 목록·이벤트 브로드캐스트를 구현하고, ImGui 호출부는
//   M4-B 구현 에이전트가 채운다(mye_imgui/ImGui:: 의존은 구현 TU에서).
#include "mye/editor/Panel.h"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder* — 기본 도킹 레이아웃 프로그램적 구성

#include "mye/core/I18n.h"

#include <algorithm>
#include <vector>

namespace mye::editor {

struct PanelManager::Impl {
    std::vector<std::unique_ptr<IEditorPanelFactory>> factories;
    std::vector<PanelDesc> descs;   // RegisteredPanels() 반환용 캐시

    struct Instance {
        PanelInstanceId id;
        IEditorPanelFactory* factory = nullptr;
        std::unique_ptr<IEditorPanel> panel;
    };
    std::vector<Instance> instances;
    std::uint64_t nextInstanceId = 1;
    bool builtDefaultLayout = false;   // 첫 프레임 1회 기본 도킹 레이아웃 구성
    std::uint32_t layoutLangVersion = 0xFFFFFFFFu;  // 언어 변경 시 레이아웃 재빌드 트리거

    IEditorPanelFactory* FindFactory(std::string_view panelId) {
        for (auto& f : factories)
            if (f->Desc().id == panelId) return f.get();
        return nullptr;
    }
};

PanelManager::PanelManager() : m_impl(std::make_unique<Impl>()) {}
PanelManager::~PanelManager() = default;

void PanelManager::RegisterFactory(std::unique_ptr<IEditorPanelFactory> factory) {
    if (!factory) return;
    m_impl->descs.push_back(factory->Desc());
    m_impl->factories.push_back(std::move(factory));
}

PanelInstanceId PanelManager::Open(std::string_view panelId) {
    IEditorPanelFactory* factory = m_impl->FindFactory(panelId);
    if (!factory) return {};

    // 다중 불가 패널이 이미 열려 있으면 기존 인스턴스 반환.
    if (!factory->Desc().allowMultiple) {
        for (auto& inst : m_impl->instances)
            if (inst.factory == factory) return inst.id;
    }

    Impl::Instance inst;
    inst.id = PanelInstanceId{m_impl->nextInstanceId++};
    inst.factory = factory;
    inst.panel = factory->Create();
    PanelInstanceId out = inst.id;
    m_impl->instances.push_back(std::move(inst));
    return out;
}

void PanelManager::Close(PanelInstanceId id) {
    auto& v = m_impl->instances;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Impl::Instance& i) { return i.id == id; }),
            v.end());
}

bool PanelManager::IsOpen(std::string_view panelId) const {
    for (auto& inst : m_impl->instances)
        if (inst.panel && inst.panel->Desc().id == panelId) return true;
    return false;
}

void PanelManager::SetupDockspace(EditorContext& /*ctx*/) {
    // 호스트 윈도우(EditorApp) 안에서 호출 — 툴바 아래 남은 영역에 도크스페이스를 만든다.
    //   고정 ID 를 써서 layout.ini 가 이 도크스페이스 레이아웃을 저장/복원할 수 있게 한다.
    const ImGuiID dockId = ImGui::GetID("MyeDockSpace");
    const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;

    // 저장된 레이아웃(layout.ini)이 없을 때만 최초 1회 기본 배치. 이후엔 사용자가 조절/저장한
    //   레이아웃을 ImGui 가 ini 에서 복원하므로 건드리지 않는다. 창 식별자는 "###안정ID" 라
    //   언어가 바뀌어도 유지된다(재빌드 불필요).
    if (!m_impl->builtDefaultLayout && ImGui::DockBuilderGetNode(dockId) == nullptr) {
        m_impl->builtDefaultLayout = true;
        ImGui::DockBuilderAddNode(dockId, dockFlags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetContentRegionAvail());

        ImGuiID center = dockId;
        ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.18f, nullptr, &center);
        ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, nullptr, &center);

        // stableId 로만 매칭(언어 무관). 패널 Begin 은 "라벨###안정ID"를 쓴다(PanelWindowTitle).
        ImGui::DockBuilderDockWindow("###mye.hierarchy", left);
        ImGui::DockBuilderDockWindow("###mye.inspector", right);
        ImGui::DockBuilderDockWindow("###mye.assets",    bottom);
        ImGui::DockBuilderDockWindow("###mye.console",   bottom);
        ImGui::DockBuilderDockWindow("###mye.viewport",  center);
        ImGui::DockBuilderDockWindow("###mye.doteditor", center);   // 뷰포트와 탭 그룹
        ImGui::DockBuilderFinish(dockId);
    } else {
        m_impl->builtDefaultLayout = true;
    }

    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), dockFlags);
}

void PanelManager::DrawPanels(EditorContext& ctx) {
    // 전체 패널 OnGui. 순회 중 Close로 벡터가 흔들리지 않도록 인덱스 스냅샷.
    const std::size_t count = m_impl->instances.size();
    for (std::size_t i = 0; i < count && i < m_impl->instances.size(); ++i) {
        if (m_impl->instances[i].panel) m_impl->instances[i].panel->OnGui(ctx);
    }
}

void PanelManager::SerializeLayout(json::Value& out) const {
    // 열린 패널 id·자체 상태 → session.json(ImGui ini는 별도 layout.ini로 EditorApp이 저장).
    json::Value::Array panels;
    for (auto& inst : m_impl->instances) {
        if (!inst.panel) continue;
        json::Value::Object o;
        o["id"] = json::Value(std::string(inst.panel->Desc().id));
        json::Value state;
        inst.panel->SerializeState(state);
        if (!state.IsNull()) o["state"] = std::move(state);
        panels.push_back(json::Value(std::move(o)));
    }
    json::Value::Object root;
    root["panels"] = json::Value(std::move(panels));
    out = json::Value(std::move(root));
}
void PanelManager::DeserializeLayout(const json::Value& in) {
    const json::Value* panels = in.IsObject() ? in.Find("panels") : nullptr;
    if (!panels || !panels->IsArray()) return;
    for (const json::Value& p : panels->AsArray()) {
        const json::Value* id = p.IsObject() ? p.Find("id") : nullptr;
        if (!id || !id->IsString()) continue;
        PanelInstanceId inst = Open(id->AsString());
        (void)inst;
        // DeserializeState는 새로 만든 인스턴스에 적용(마지막 인스턴스).
        if (!m_impl->instances.empty()) {
            const json::Value* state = p.Find("state");
            if (state) m_impl->instances.back().panel->DeserializeState(*state);
        }
    }
}

std::span<const PanelDesc> PanelManager::RegisteredPanels() const { return m_impl->descs; }

void PanelManager::BroadcastEvent(const EditorEvent& ev) {
    for (auto& inst : m_impl->instances)
        if (inst.panel) inst.panel->OnEvent(ev);
}

} // namespace mye::editor
