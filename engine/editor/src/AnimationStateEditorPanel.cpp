#include "imgui.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/core/Module.h"
#include "mye/core/JsonFile.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/Viewport.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

namespace mye::editor {
namespace {
const PanelDesc kDesc{"mye.animstate", "행동 모션", false, DockSlot::Floating};
constexpr const char* kTypes[]{"bool", "float", "trigger"};
constexpr const char* kOperations[]{">", "<", ">=", "<=", "==", "!=", "참", "거짓"};

bool NameField(const char* label, std::string& value) {
    std::array<char, 65> bytes{};
    std::snprintf(bytes.data(), bytes.size(), "%s", value.c_str());
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    const auto id = std::string("##") + label;
    if (!ImGui::InputText(id.c_str(), bytes.data(), bytes.size()))
        return false;
    value = bytes.data();
    return true;
}
void NextButton(const char* label) {
    const auto& style = ImGui::GetStyle();
    const auto end = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x <=
        end)
        ImGui::SameLine();
}
bool StateChoice(const char* label, const asset::AnimationStateAsset& data, int& index, bool any = false) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    const char* caption =
        index >= 0 && index < static_cast<int>(data.states.size()) ? data.states[index].name.c_str() : "모든 상태";
    bool changed = false;
    if (ImGui::BeginCombo((std::string("##") + label).c_str(), caption)) {
        if (any && ImGui::Selectable("모든 상태", index == -1)) {
            index = -1;
            changed = true;
        }
        for (size_t i = 0; i < data.states.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(data.states[i].name.c_str(), index == static_cast<int>(i))) {
                index = static_cast<int>(i);
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
template <class Values> std::string UnusedName(const Values& values, const char* prefix) {
    for (size_t i = 1;; ++i) {
        auto name = std::string(prefix) + std::to_string(i);
        if (std::none_of(values.begin(), values.end(), [&](const auto& value) { return value.name == name; }))
            return name;
    }
}

class AnimationStateEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override {
        return kDesc;
    }
    void OnGui(EditorContext& ctx) override {
        const auto* viewport = ImGui::GetMainViewport();
        const Vec2 workSize{viewport->WorkSize.x, viewport->WorkSize.y};
        const ImVec2 size{std::min(880.0f, workSize.x), std::min(760.0f, workSize.y)};
        if (m_workSize != workSize && !m_docked) {
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            ImGui::SetNextWindowPos(
                {viewport->WorkPos.x + (workSize.x - size.x) * .5f, viewport->WorkPos.y + (workSize.y - size.y) * .5f},
                ImGuiCond_Always);
        } else
            ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        m_workSize = workSize;
        if (!ImGui::Begin("행동 모션###mye.animstate", nullptr, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::End();
            return;
        }
        m_docked = ImGui::IsWindowDocked();
        if (!ctx.app || !ctx.project || !ctx.project->IsOpen()) {
            ImGui::TextWrapped("프로젝트를 열고 새 행동 모션을 만들거나 .animstate 에셋을 여세요.");
            ImGui::End();
            return;
        }
        auto* doc = ctx.app->AnimationStateDocument();
        const bool playing = ctx.playMode && ctx.playMode->IsPlaying();
        if (playing)
            ImGui::TextWrapped("Play 중에는 작성·저장을 잠급니다. Stop 후 편집하세요.");
        ImGui::BeginDisabled(playing || (doc && doc->AnimationStateDraft()));
        if (ImGui::Button("새 행동 모션")) {
            auto created = ctx.project->NewAnimationState();
            if (created) {
                doc = created.Value();
                ctx.app->SelectAnimationStateDocument(doc->Id());
            } else
                m_status = created.GetError().message;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##documents", doc ? doc->TabTitle().c_str() : "문서 선택")) {
            for (auto* candidate : ctx.project->Documents()) {
                if (candidate->GetKind() != Document::Kind::AnimationState)
                    continue;
                ImGui::PushID(static_cast<int>(candidate->Id().value));
                if (ImGui::Selectable(candidate->TabTitle().c_str(), candidate == doc)) {
                    doc = candidate;
                    ctx.app->SelectAnimationStateDocument(doc->Id());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (!doc) {
            ImGui::TextWrapped(
                "기존 .anim을 상태에 연결하고 조건·전이를 작성합니다. 그림과 리깅은 외부 도구에서 제작합니다.");
            ImGui::End();
            return;
        }
        if (m_document != doc->Id()) {
            m_document = doc->Id();
            m_status.clear();
            const auto path =
                doc->Path().empty()
                    ? "assets/animations/character.animstate"
                    : Utf8String(Utf8Path(doc->Path()).lexically_relative(Utf8Path(ctx.project->RootDir())));
            std::snprintf(m_savePath.data(), m_savePath.size(), "%s", path.c_str());
            Reload(*doc);
        } else if (m_revision != doc->Commands().Revision())
            Reload(*doc);
        doc->Commands().SetContext(&ctx);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ctx.app->SetAnimationStateFocused();
        ImGui::BeginDisabled(playing);
        ImGui::TextUnformatted("저장 경로 (.animstate)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##save_path", m_savePath.data(), m_savePath.size());
        if (ImGui::Button("저장"))
            Save(ctx, *doc);
        NextButton("되돌리기");
        ImGui::BeginDisabled(m_pending || !doc->Commands().CanUndo());
        if (ImGui::Button("되돌리기")) {
            doc->Commands().Undo();
            Reload(*doc);
        }
        ImGui::EndDisabled();
        NextButton("다시 실행");
        ImGui::BeginDisabled(m_pending || !doc->Commands().CanRedo());
        if (ImGui::Button("다시 실행")) {
            doc->Commands().Redo();
            Reload(*doc);
        }
        ImGui::EndDisabled();
        NextButton("문서 닫기");
        bool close = false;
        if (ImGui::Button("문서 닫기")) {
            if (doc->IsDirty())
                ImGui::OpenPopup("행동 변경 보존###state_close");
            else
                close = true;
        }
        if (ImGui::BeginPopupModal("행동 변경 보존###state_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("저장되지 않은 변경이 있습니다. 저장에 실패하면 문서를 유지합니다.");
            if (ImGui::Button("저장 후 닫기") && Save(ctx, *doc)) {
                close = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("변경 버리고 닫기")) {
                close = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("취소"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        NextButton("선택한 스프라이트에 지정");
        ImGui::BeginDisabled(m_pending || doc->Path().empty() || doc->IsDirty());
        if (ImGui::Button("선택한 스프라이트에 지정")) {
            auto* db = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
            const auto relative = Utf8Path(doc->Path()).lexically_relative(Utf8Path(ctx.project->RootDir()) / "assets");
            const auto guid = db ? db->GuidFromPath("assets://" + Utf8String(relative)) : asset::AssetGuid{};
            auto assigned = AssignAnimationStateToEntity(
                ctx, ctx.selection ? ctx.selection->Primary().AsEntity() : ecs::Entity{}, asset::AssetRef{guid});
            m_status = assigned ? "씬에 지정했습니다. 씬을 저장하고 Play하세요." : assigned.GetError().message;
        }
        ImGui::EndDisabled();
        if (!m_status.empty())
            ImGui::TextWrapped("%s", m_status.c_str());
        ImGui::BeginDisabled(!m_pending);
        if (ImGui::Button("적용"))
            Apply(ctx, *doc);
        NextButton("편집 취소");
        if (ImGui::Button("편집 취소") || (m_pending && ImGui::Shortcut(ImGuiKey_Escape))) {
            doc->DiscardAnimationStateDraft();
            Reload(*doc);
            m_status.clear();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("적용은 Undo 한 단계, 저장은 파일 반영입니다. 미적용 값은 실행에 사용하지 않습니다.");
        bool changed = false;
        const bool visible = ImGui::BeginChild("##state_workspace", ImVec2(0, 0));
        if (!close && visible) {
            changed |= NameField("행동 이름", m_draft.name);
            if (ImGui::BeginTabBar("##state_tasks")) {
                if (ImGui::BeginTabItem("상태")) {
                    changed |= States(ctx);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("매개변수")) {
                    changed |= Parameters();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("전이")) {
                    changed |= Transitions();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        if (changed) {
            m_pending = true;
            doc->StageAnimationState(m_draft);
        }
        ImGui::EndDisabled();
        if (close) {
            ctx.project->CloseDocument(doc->Id());
            ctx.app->SelectAnimationStateDocument({});
            m_document = {};
            m_pending = false;
        }
        ImGui::End();
    }

private:
    void Reload(Document& doc) {
        m_draft = doc.AnimationStateDraft() ? *doc.AnimationStateDraft() : doc.AnimationState();
        m_pending = doc.AnimationStateDraft().has_value();
        m_revision = doc.Commands().Revision();
    }
    bool Apply(EditorContext& ctx, Document& doc) {
        if (!m_pending)
            return true;
        auto edited = doc.EditAnimationState(ctx, m_draft, "행동 모션 변경");
        if (!edited) {
            m_status = edited.GetError().message;
            return false;
        }
        Reload(doc);
        m_status = "변경을 적용했습니다. 저장하면 다음 실행에 반영됩니다.";
        return true;
    }
    bool Save(EditorContext& ctx, Document& doc) {
        if (!Apply(ctx, doc))
            return false;
        auto saved = ctx.project->SaveAnimationState(doc.Id(), m_savePath.data());
        if (!saved) {
            m_status = saved.GetError().message;
            return false;
        }
        auto* viewport = ctx.app->Viewport();
        auto refreshed = viewport ? viewport->RefreshAssetIndex()
                                  : Expected<void, Error>(Error{"에셋 인덱스를 갱신할 수 없습니다.", 1});
        m_status = refreshed ? "행동 모션을 저장했습니다. 씬에 지정하거나 stateMachine에 에셋을 드래그하세요."
                             : "파일 저장 완료; 에셋 갱신 실패: " + refreshed.GetError().message;
        return bool(refreshed);
    }
    bool States(EditorContext& ctx) {
        bool changed = StateChoice("시작 상태", m_draft, m_draft.initialState);
        ImGui::TextWrapped("상태마다 기존 .anim을 지정합니다. 상태를 제거하면 연결된 전이도 제거되며 적용 후 Undo로 "
                           "복구할 수 있습니다.");
        size_t remove = m_draft.states.size();
        for (size_t i = 0; i < m_draft.states.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& state = m_draft.states[i];
            if (ImGui::CollapsingHeader((state.name + "###state").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= NameField("상태 이름", state.name);
                auto* db = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
                const auto path = db ? db->PathFromGuid(state.animation.guid) : std::string{};
                const auto caption = path.empty() ? state.animation.guid.ToString() : path;
                ImGui::TextUnformatted("애니메이션 (.anim) · 에셋에서 드래그");
                ImGui::Button((caption + "###animation").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
                        if (db && payload->Data && payload->DataSize > 1) {
                            const auto* bytes = static_cast<const char*>(payload->Data);
                            const std::string_view assetPath(bytes, static_cast<size_t>(payload->DataSize) - 1);
                            const auto guid = db->GuidFromPath(assetPath);
                            if (bytes[payload->DataSize - 1] == '\0' &&
                                assetPath.find('\0') == std::string_view::npos && assetPath.ends_with(".anim") &&
                                guid.IsValid()) {
                                state.animation = {guid};
                                changed = true;
                            } else
                                m_status = "프로젝트에 임포트한 .anim 클립을 드래그하세요.";
                        } else
                            m_status = "에셋 인덱스를 갱신한 뒤 .anim 클립을 드래그하세요.";
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::BeginDisabled(m_draft.states.size() <= 1);
                if (ImGui::Button("상태 제거"))
                    remove = i;
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        if (remove < m_draft.states.size()) {
            const int index = static_cast<int>(remove);
            std::erase_if(m_draft.transitions, [&](const auto& t) { return t.from == index || t.to == index; });
            for (auto& t : m_draft.transitions) {
                if (t.from > index)
                    --t.from;
                if (t.to > index)
                    --t.to;
            }
            m_draft.states.erase(m_draft.states.begin() + static_cast<std::ptrdiff_t>(remove));
            if (m_draft.initialState == index)
                m_draft.initialState = 0;
            else if (m_draft.initialState > index)
                --m_draft.initialState;
            changed = true;
        }
        ImGui::BeginDisabled(m_draft.states.size() >= 64);
        if (ImGui::Button("상태 추가")) {
            m_draft.states.push_back({UnusedName(m_draft.states, "state_"), {}});
            changed = true;
        }
        ImGui::EndDisabled();
        return changed;
    }
    bool Parameters() {
        bool changed = false;
        size_t remove = m_draft.parameters.size();
        ImGui::TextWrapped("moving(bool)은 실제 2D 이동량입니다. 나머지 값은 게임 Lua가 설정합니다. trigger는 false로 "
                           "시작하며 선택된 전이에서 소모합니다. 타입 변경 뒤 조건 연산도 확인하세요.");
        for (size_t i = 0; i < m_draft.parameters.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& p = m_draft.parameters[i];
            if (ImGui::CollapsingHeader((p.name + "###parameter").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto previous = p.name;
                if (NameField("매개변수 이름", p.name)) {
                    const bool duplicate = std::any_of(
                        m_draft.parameters.begin(), m_draft.parameters.end(),
                        [&](const auto& other) { return &other != &p && other.name == p.name; });
                    if (duplicate) {
                        p.name = previous; // Keep named references distinct while the field is being typed.
                        m_status = "다른 매개변수와 구분되는 이름을 입력하세요.";
                    } else {
                        for (auto& t : m_draft.transitions) {
                            for (auto& c : t.conditions)
                                if (c.param == previous)
                                    c.param = p.name;
                            for (auto& name : t.consumeTriggers)
                                if (name == previous)
                                    name = p.name;
                        }
                        changed = true;
                    }
                }
                int type = static_cast<int>(p.type);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##type", &type, kTypes, 3)) {
                    p.type = static_cast<asset::ParamType>(type);
                    p.value = 0;
                    changed = true;
                }
                if (p.type == asset::ParamType::Float) {
                    ImGui::TextUnformatted("기본값");
                    ImGui::SetNextItemWidth(-1);
                    changed |= ImGui::InputFloat("##default", &p.value);
                } else {
                    bool value = p.value != 0;
                    ImGui::BeginDisabled(p.type == asset::ParamType::Trigger);
                    if (ImGui::Checkbox("기본값", &value)) {
                        p.value = value ? 1.0f : 0.0f;
                        changed = true;
                    }
                    ImGui::EndDisabled();
                }
                if (ImGui::Button("매개변수 제거"))
                    remove = i;
            }
            ImGui::PopID();
        }
        if (remove < m_draft.parameters.size()) {
            const auto& name = m_draft.parameters[remove].name;
            const bool used = std::any_of(m_draft.transitions.begin(), m_draft.transitions.end(), [&](const auto& t) {
                return std::find(t.consumeTriggers.begin(), t.consumeTriggers.end(), name) != t.consumeTriggers.end() ||
                       std::any_of(t.conditions.begin(), t.conditions.end(),
                                   [&](const auto& c) { return c.param == name; });
            });
            if (used)
                m_status = "이 매개변수의 조건·소모 참조를 먼저 제거하세요.";
            else {
                m_draft.parameters.erase(m_draft.parameters.begin() + static_cast<std::ptrdiff_t>(remove));
                changed = true;
            }
        }
        ImGui::BeginDisabled(m_draft.parameters.size() >= 64);
        if (ImGui::Button("매개변수 추가")) {
            m_draft.parameters.push_back({UnusedName(m_draft.parameters, "param_"), asset::ParamType::Bool, 0});
            changed = true;
        }
        ImGui::EndDisabled();
        return changed;
    }
    bool Transitions() {
        bool changed = false;
        size_t remove = m_draft.transitions.size();
        int move = -1, offset = 0;
        ImGui::TextWrapped("위에서부터 평가하고 틱마다 하나만 전환합니다. 모든 조건이 참이어야 하며 조건이 없으면 항상 "
                           "준비됩니다. 완료 대기는 비반복 클립에 사용합니다.");
        for (size_t i = 0; i < m_draft.transitions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& t = m_draft.transitions[i];
            if (ImGui::CollapsingHeader(("전이 " + std::to_string(i + 1) + "###transition").c_str())) {
                changed |= StateChoice("출발", m_draft, t.from, true);
                changed |= StateChoice("도착", m_draft, t.to);
                changed |= ImGui::Checkbox("클립 완료를 기다림", &t.onClipFinished);
                changed |= ImGui::Checkbox("진행률 유지", &t.keepPhase);
                size_t removeCondition = t.conditions.size();
                for (size_t j = 0; j < t.conditions.size(); ++j) {
                    ImGui::PushID(static_cast<int>(j));
                    auto& c = t.conditions[j];
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##parameter", c.param.empty() ? "매개변수 선택" : c.param.c_str())) {
                        for (size_t parameter = 0; parameter < m_draft.parameters.size(); ++parameter) {
                            const auto& p = m_draft.parameters[parameter];
                            ImGui::PushID(static_cast<int>(parameter));
                            if (ImGui::Selectable(p.name.c_str(), p.name == c.param)) {
                                c.param = p.name;
                                c.op = p.type == asset::ParamType::Float ? asset::CmpOp::Greater : asset::CmpOp::IsTrue;
                                c.threshold = 0;
                                changed = true;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    int op = static_cast<int>(c.op);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##operation", &op, kOperations, 8)) {
                        c.op = static_cast<asset::CmpOp>(op);
                        if (op >= 6)
                            c.threshold = 0;
                        changed = true;
                    }
                    if (op < 6) {
                        ImGui::TextUnformatted("비교값");
                        ImGui::SetNextItemWidth(-1);
                        changed |= ImGui::InputFloat("##threshold", &c.threshold);
                    }
                    if (ImGui::Button("조건 제거"))
                        removeCondition = j;
                    ImGui::PopID();
                }
                if (removeCondition < t.conditions.size()) {
                    t.conditions.erase(t.conditions.begin() + static_cast<std::ptrdiff_t>(removeCondition));
                    changed = true;
                }
                ImGui::BeginDisabled(t.conditions.size() >= 16 || m_draft.parameters.empty());
                if (ImGui::Button("조건 추가")) {
                    t.conditions.push_back({});
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("추가로 소모할 trigger (조건의 trigger는 자동 소모)");
                ImGui::PushID("consume");
                for (size_t parameter = 0; parameter < m_draft.parameters.size(); ++parameter) {
                    const auto& p = m_draft.parameters[parameter];
                    if (p.type == asset::ParamType::Trigger) {
                        ImGui::PushID(static_cast<int>(parameter));
                        bool consume = std::find(t.consumeTriggers.begin(), t.consumeTriggers.end(), p.name) !=
                                       t.consumeTriggers.end();
                        ImGui::BeginDisabled(!consume && t.consumeTriggers.size() >= 16);
                        if (ImGui::Checkbox(p.name.c_str(), &consume)) {
                            if (consume)
                                t.consumeTriggers.push_back(p.name);
                            else
                                std::erase(t.consumeTriggers, p.name);
                            changed = true;
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::Button("우선순위 올리기")) {
                    move = static_cast<int>(i);
                    offset = -1;
                }
                ImGui::EndDisabled();
                NextButton("우선순위 내리기");
                ImGui::BeginDisabled(i + 1 == m_draft.transitions.size());
                if (ImGui::Button("우선순위 내리기")) {
                    move = static_cast<int>(i);
                    offset = 1;
                }
                ImGui::EndDisabled();
                NextButton("전이 제거");
                if (ImGui::Button("전이 제거"))
                    remove = i;
            }
            ImGui::PopID();
        }
        if (remove < m_draft.transitions.size()) {
            m_draft.transitions.erase(m_draft.transitions.begin() + static_cast<std::ptrdiff_t>(remove));
            changed = true;
        } else if (move >= 0) {
            std::swap(m_draft.transitions[move], m_draft.transitions[move + offset]);
            changed = true;
        }
        ImGui::BeginDisabled(m_draft.transitions.size() >= 256);
        if (ImGui::Button("전이 추가")) {
            asset::AnimTransition transition;
            transition.keepPhase = false;
            m_draft.transitions.push_back(std::move(transition));
            changed = true;
        }
        ImGui::EndDisabled();
        return changed;
    }
    DocumentId m_document{};
    uint64_t m_revision = 0;
    asset::AnimationStateAsset m_draft;
    std::array<char, 4096> m_savePath{};
    std::string m_status;
    Vec2 m_workSize{};
    bool m_pending = false, m_docked = false;
};
class AnimationStateEditorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override {
        return kDesc;
    }
    std::unique_ptr<IEditorPanel> Create() override {
        return std::make_unique<AnimationStateEditorPanel>();
    }
};
} // namespace
std::unique_ptr<IEditorPanelFactory> MakeAnimationStateEditorPanelFactory() {
    return std::make_unique<AnimationStateEditorPanelFactory>();
}
} // namespace mye::editor
