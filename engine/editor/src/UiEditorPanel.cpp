#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/Project.h"
#include "mye/editor/Viewport.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Module.h"
#include "mye/asset/AssetDatabase.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

namespace mye::editor {
namespace {
const PanelDesc kDesc{"mye.ui", "게임 UI", false, DockSlot::Floating};
constexpr const char* kTypes[] = {"Panel", "Label", "ProgressBar", "Button", "Image", "Window", "StackLayout", "GridLayout", "TextInput"};

template<class Node> Node* NodeAt(Node& root, const std::vector<size_t>& path) {
    auto* node = &root;
    for (const auto index : path) {
        if (index >= node->children.size()) return nullptr;
        node = &node->children[index];
    }
    return node;
}
bool HasName(const ui::UiNodeDesc& node, std::string_view name) {
    if (node.name == name) return true;
    return std::any_of(node.children.begin(), node.children.end(), [&](const auto& child) { return HasName(child, name); });
}
template<size_t N> bool EditText(const char* label, std::string& value) {
    std::array<char, N> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::InputText("##value", buffer.data(), buffer.size());
    if (changed) value = buffer.data();
    ImGui::PopID();
    return changed;
}

bool EditVector(const char* label, Vec2& value) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::InputFloat2("##value", &value.x);
    ImGui::PopID();
    return changed;
}
void NextButton(const char* label) {
    const auto& style = ImGui::GetStyle();
    const float end = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float width = ImGui::CalcTextSize(label).x + 2 * style.FramePadding.x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= end) ImGui::SameLine();
}

class UiEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    void OnGui(EditorContext& ctx) override {
        const auto* viewport = ImGui::GetMainViewport();
        const Vec2 workSize{viewport->WorkSize.x, viewport->WorkSize.y};
        const ImVec2 size{std::min(1040.0f, workSize.x), std::min(760.0f, workSize.y)};
        if (m_workSize != workSize && !m_docked) {
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
            ImGui::SetNextWindowPos({viewport->WorkPos.x + (workSize.x-size.x)*.5f, viewport->WorkPos.y + (workSize.y-size.y)*.5f}, ImGuiCond_Always);
        } else ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        m_workSize = workSize;
        if (!ImGui::Begin("게임 UI###mye.ui", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }
        m_docked = ImGui::IsWindowDocked();
        if (!ctx.app || !ctx.project || !ctx.project->IsOpen()) {
            ImGui::TextWrapped("프로젝트를 열고 새 UI를 만들거나 에셋의 .ui 파일을 더블 클릭하세요."); ImGui::End(); return;
        }
        const bool playing = ctx.playMode && ctx.playMode->IsPlaying();
        if (m_root != ctx.project->RootDir()) { m_pending = false; m_document = {}; }
        if (playing) ImGui::TextWrapped("Play 중에는 UI 작성·저장을 잠급니다. Stop 후 수정하고 다시 실행하세요.");
        ImGui::BeginDisabled(playing || m_pending);
        if (ImGui::Button("새 UI")) {
            auto created = ctx.project->NewUi();
            if (created) ctx.app->SelectUiDocument(created.Value()->Id()); else m_status = created.GetError().message;
        }
        ImGui::SameLine();
        auto* doc = ctx.app->UiDocument();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##documents", doc ? doc->TabTitle().c_str() : "UI 문서 선택")) {
            for (auto* candidate : ctx.project->Documents()) {
                if (candidate->GetKind() != Document::Kind::Ui) continue;
                ImGui::PushID(static_cast<int>(candidate->Id().value));
                if (ImGui::Selectable(candidate->TabTitle().c_str(), candidate == doc)) { ctx.app->SelectUiDocument(candidate->Id()); doc = candidate; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (!doc) { ImGui::TextWrapped("새 UI는 960×540 화면을 채우는 Panel로 시작합니다. 픽셀 이미지는 외부 도구에서 제작해 임포트하세요."); ImGui::End(); return; }
        if (m_document != doc->Id() || m_root != ctx.project->RootDir()) {
            m_document = doc->Id(); m_root = ctx.project->RootDir(); m_path.clear(); m_pending = false; m_status.clear(); m_previewRequested = true;
            const auto path = doc->Path().empty() ? "assets/ui/hud.ui" : Utf8String(Utf8Path(doc->Path()).lexically_relative(Utf8Path(m_root)));
            std::snprintf(m_savePath.data(), m_savePath.size(), "%s", path.c_str());
            Reload(*doc);
        } else if (m_revision != doc->Commands().Revision()) Reload(*doc);
        doc->Commands().SetContext(&ctx);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ctx.app->SetUiFocused();
        ImGui::BeginDisabled(playing);
        ImGui::SetNextItemWidth(-1);
        ImGui::TextUnformatted("저장 경로 (.ui)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##save_path", "assets/ui/hud.ui", m_savePath.data(), m_savePath.size());
        if (ImGui::Button("저장")) Save(ctx, *doc);
        NextButton("되돌리기");
        ImGui::BeginDisabled(m_pending || !doc->Commands().CanUndo());
        if (ImGui::Button("되돌리기")) { doc->Commands().Undo(); Reload(*doc); }
        ImGui::EndDisabled(); NextButton("다시 실행");
        ImGui::BeginDisabled(m_pending || !doc->Commands().CanRedo());
        if (ImGui::Button("다시 실행")) { doc->Commands().Redo(); Reload(*doc); }
        ImGui::EndDisabled(); NextButton("문서 닫기");
        if (ImGui::Button("문서 닫기")) {
            if (doc->IsDirty() || m_pending) ImGui::OpenPopup("UI 변경 보존###ui_close");
            else m_close = true;
        }
        if (ImGui::BeginPopupModal("UI 변경 보존###ui_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("저장되지 않은 UI 변경이 있습니다. 저장에 실패하면 문서를 유지합니다.");
            if (ImGui::Button("저장 후 닫기") && Save(ctx, *doc)) { m_close = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("변경 버리고 닫기")) { m_close = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (!m_status.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s", m_status.c_str()); }
        const bool content = ImGui::BeginChild("##ui_workspace", ImVec2(0,0));
        if (!m_close && content && ImGui::BeginTabBar("##ui_tasks")) {
            if (ImGui::BeginTabItem("위젯 작성")) {
                ImGui::TextWrapped("트리에서 위젯을 선택합니다. 속성은 적용으로 확정하며 취소하면 저장된 문서 값으로 돌아갑니다.");
                const bool columns = ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 42;
                const bool table = columns && ImGui::BeginTable("##ui_columns", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV);
                if (!columns || table) {
                    if (table) { ImGui::TableSetupColumn("구조", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 15); ImGui::TableSetupColumn("속성"); ImGui::TableNextRow(); ImGui::TableNextColumn(); }
                    std::vector<size_t> path;
                    Tree(*doc, doc->Ui().root, path);
                    if (table) ImGui::TableNextColumn(); else ImGui::Separator();
                    Properties(ctx, *doc);
                    if (table) ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("미리보기", nullptr, m_previewRequested ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
                m_previewRequested = false;
                ImGui::TextWrapped("적용한 문서를 게임과 같은 PNG·글꼴·앵커·클리핑으로 표시합니다. 프로젝트 Lua를 실행하지 않습니다.");
                if (m_pending) ImGui::TextWrapped("적용 전 속성은 미리보기에 포함되지 않습니다.");
                auto* renderer = ctx.app->Viewport();
                auto preview = renderer ? renderer->UiDocumentPreview(doc->Ui(), doc->Id(), doc->Commands().Revision())
                    : Expected<IEditorViewport::TexturePreview, Error>(Error{"렌더 미리보기를 사용할 수 없습니다.", 1});
                if (preview) {
                    const float width = std::max(1.0f, std::min(960.0f, ImGui::GetContentRegionAvail().x));
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview.Value().id)), ImVec2(width, width * 540 / 960));
                } else ImGui::TextWrapped("미리보기 실패: %s", preview.GetError().message.c_str());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::EndDisabled();
        if (m_close) { ctx.project->CloseDocument(doc->Id()); ctx.app->SelectUiDocument({}); m_close = false; m_pending = false; }
        ImGui::End();
    }
private:
    void Reload(Document& doc) {
        auto& data = doc.UiDraft() ? *doc.UiDraft() : doc.Ui();
        auto* node = NodeAt(data.root, m_path);
        if (!node) { m_path.clear(); node = &data.root; }
        m_draft = *node; m_pending = doc.UiDraft().has_value(); m_revision = doc.Commands().Revision();
    }
    bool Apply(EditorContext& ctx, Document& doc) {
        if (!m_pending) return true;
        auto after = doc.Ui();
        auto* node = NodeAt(after.root, m_path);
        if (!node) { m_status = "위젯이 변경되었습니다. 다시 선택하세요."; return false; }
        *node = m_draft;
        auto edited = doc.EditUi(ctx, std::move(after), "UI 속성 변경");
        if (!edited) { m_status = edited.GetError().message; return false; }
        Reload(doc); m_status = "속성을 적용했습니다. 저장하면 다음 Play에 반영됩니다."; return true;
    }
    bool Save(EditorContext& ctx, Document& doc) {
        if (!Apply(ctx, doc)) return false;
        auto saved = ctx.project->SaveUi(doc.Id(), m_savePath.data());
        if (!saved) { m_status = saved.GetError().message; return false; }
        auto* viewport = ctx.app->Viewport();
        auto refreshed = viewport ? viewport->RefreshAssetIndex() : Expected<void, Error>(Error{"에셋 인덱스를 갱신할 수 없습니다.", 1});
        m_status = refreshed ? "UI를 저장했습니다. GameUi.document에 에셋을 드래그하세요." : "UI 저장 완료; 에셋 갱신 실패: " + refreshed.GetError().message;
        return bool(refreshed);
    }
    void Tree(Document& doc, const ui::UiNodeDesc& node, std::vector<size_t>& path) {
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (path == m_path) flags |= ImGuiTreeNodeFlags_Selected;
        if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        const bool open = ImGui::TreeNodeEx("##node", flags, "%s (%s)", node.name.empty() ? "이름 없음" : node.name.c_str(), node.typeName.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && path != m_path) {
            if (m_pending) m_status = "현재 속성을 적용하거나 취소한 뒤 다른 위젯을 선택하세요.";
            else { m_path = path; Reload(doc); }
        }
        if (open) {
            for (size_t i = 0; i < node.children.size(); ++i) {
                ImGui::PushID(static_cast<int>(i)); path.push_back(i); Tree(doc, node.children[i], path); path.pop_back(); ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    void Properties(EditorContext& ctx, Document& doc) {
        ImGui::PushID(static_cast<int>(doc.Id().value));
        const auto depth = m_path.size();
        for (const auto index : m_path) ImGui::PushID(static_cast<int>(index));
        bool changed = false;
        ImGui::TextUnformatted(m_draft.typeName.c_str());
        ImGui::SetNextItemWidth(-1); changed |= EditText<65>("이름", m_draft.name);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lua mye.ui API에서 찾는 고유 이름입니다. 64 bytes 이내로 지정하세요.");
        if (ImGui::BeginCombo("앵커 프리셋", "모서리 / 중앙 / 화면 채우기")) {
            constexpr const char* presets[] = {"왼쪽 위","중앙 위","오른쪽 위","왼쪽 중앙","중앙","오른쪽 중앙","왼쪽 아래","중앙 아래","오른쪽 아래","채우기"};
            for (int i=0; i<10; ++i) if (ImGui::Selectable(presets[i])) {
                if (i == 9) m_draft.anchors = ui::AnchorRect::Fill();
                else { const Vec2 point{float(i%3)*.5f, float(i/3)*.5f}; m_draft.anchors.anchorMin = point; m_draft.anchors.anchorMax = point; m_draft.anchors.pivot = point; m_draft.anchors.offsetMin = {}; m_draft.anchors.offsetMax = {}; }
                changed = true;
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("앵커·피벗은 0~1, 위치·크기는 논리 픽셀입니다. 점 앵커는 크기, 스트레치는 양쪽 여백을 사용합니다.");
        changed |= EditVector("최소 앵커", m_draft.anchors.anchorMin);
        changed |= EditVector("최대 앵커", m_draft.anchors.anchorMax);
        changed |= EditVector("피벗", m_draft.anchors.pivot);
        changed |= EditVector("위치 / 시작 여백", m_draft.anchors.offsetMin);
        changed |= EditVector("끝 여백", m_draft.anchors.offsetMax);
        changed |= EditVector("크기", m_draft.anchors.sizeDelta);
        ImGui::Separator();
        ImGui::TextWrapped("속성: text(한글·TextInput은 한 줄 최대4096 UTF-8 bytes), fontSize(8~96), colour/tint/fill/track(#RRGGBB), value/maximum(게이지), enabled/visible/clip(true/false), texture(PNG GUID), source(x,y,w,h). 위젯별 허용 속성을 검사합니다.");
        size_t remove = m_draft.properties.size();
        for (size_t i=0; i<m_draft.properties.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(-1); changed |= EditText<129>("키", m_draft.properties[i].key);
            ImGui::SetNextItemWidth(-1); changed |= EditText<4097>("값", m_draft.properties[i].value);
            if (m_draft.properties[i].key == "texture" && ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
                    auto* database = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
                    if (database && payload->Data && payload->DataSize > 1) {
                        const auto* bytes = static_cast<const char*>(payload->Data);
                        const std::string_view path(bytes, static_cast<size_t>(payload->DataSize)-1);
                        const auto guid = database->GuidFromPath(path);
                        if (bytes[payload->DataSize-1] == '\0' && path.find('\0') == std::string_view::npos && path.ends_with(".png") && guid.IsValid()) {
                            m_draft.properties[i].value = guid.ToString(); changed = true;
                        } else m_status = "texture에는 임포트한 PNG 에셋을 드래그하세요.";
                    } else m_status = "프로젝트 에셋 인덱스를 갱신한 뒤 PNG를 드래그하세요.";
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::SmallButton("속성 제거")) remove = i;
            ImGui::PopID();
        }
        if (remove < m_draft.properties.size()) { m_draft.properties.erase(m_draft.properties.begin() + static_cast<std::ptrdiff_t>(remove)); changed = true; }
        ImGui::BeginDisabled(m_draft.properties.size() >= 32);
        if (ImGui::Button("속성 추가")) { m_draft.properties.push_back({"", ""}); changed = true; }
        ImGui::EndDisabled();
        if (changed) {
            auto draft = doc.Ui();
            if (auto* node = NodeAt(draft.root, m_path)) { *node = m_draft; doc.StageUi(std::move(draft)); m_pending = true; }
        }
        NextButton("적용");
        if (ImGui::Button("적용")) Apply(ctx, doc);
        NextButton("취소");
        if (ImGui::Button("취소")) { doc.DiscardUiDraft(); Reload(doc); m_status.clear(); }
        ImGui::Separator();
        ImGui::BeginDisabled(m_pending);
        ImGui::TextUnformatted("자식 위젯 종류");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##child_type", kTypes[m_newType])) {
            for (int i=0; i<static_cast<int>(std::size(kTypes)); ++i) if (ImGui::Selectable(kTypes[i], m_newType==i)) m_newType=i;
            ImGui::EndCombo();
        }
        if (ImGui::Button("위젯 추가")) {
            auto after = doc.Ui();
            auto* parent = NodeAt(after.root, m_path);
            if (parent) {
                ui::UiNodeDesc child; child.typeName = kTypes[m_newType];
                for (size_t suffix=1; ;++suffix) { child.name = child.typeName + std::to_string(suffix); if (!HasName(after.root, child.name)) break; }
                child.anchors = ui::AnchorRect::TopLeft({16,16}, {160,32});
                if (child.typeName == "Label") child.properties = {{"text", "새 텍스트"}};
                auto path = m_path; path.push_back(parent->children.size()); parent->children.push_back(std::move(child));
                auto edited = doc.EditUi(ctx, std::move(after), "UI 위젯 추가");
                if (edited) { m_path = std::move(path); Reload(doc); } else m_status = edited.GetError().message;
            }
        }
        NextButton("선택 위젯 제거");
        ImGui::BeginDisabled(m_path.empty());
        if (ImGui::Button("선택 위젯 제거")) {
            auto after = doc.Ui(); auto parentPath=m_path; const auto index=parentPath.back(); parentPath.pop_back();
            if (auto* parent = NodeAt(after.root, parentPath); parent && index < parent->children.size()) {
                parent->children.erase(parent->children.begin()+static_cast<std::ptrdiff_t>(index));
                auto edited = doc.EditUi(ctx, std::move(after), "UI 위젯 제거");
                if (edited) { m_path=std::move(parentPath); Reload(doc); } else m_status=edited.GetError().message;
            }
        }
        ImGui::EndDisabled(); ImGui::EndDisabled();
        for (size_t i=0; i<depth; ++i) ImGui::PopID();
        ImGui::PopID();
    }
    DocumentId m_document{};
    uint64_t m_revision=0;
    std::string m_root, m_status;
    std::array<char,4096> m_savePath{};
    std::vector<size_t> m_path;
    ui::UiNodeDesc m_draft;
    bool m_pending=false, m_close=false, m_previewRequested=true;
    bool m_docked=false;
    Vec2 m_workSize{};
    int m_newType=0;
};
class UiEditorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<UiEditorPanel>(); }
};
}
std::unique_ptr<IEditorPanelFactory> MakeUiEditorPanelFactory() { return std::make_unique<UiEditorPanelFactory>(); }
} // namespace mye::editor
