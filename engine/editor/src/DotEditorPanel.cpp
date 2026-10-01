#include "mye/editor/Panel.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/DotEditing.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/Selection.h"
#include "mye/editor/Viewport.h"
#include "mye/anim/ClipPlayback.h"
#include "mye/core/JsonFile.h"
#include "mye/core/Log.h"
#include "mye/imgui/EditorWidgets.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace mye::editor {
namespace {
const PanelDesc kDesc{"mye.doteditor", "도트 에디터", false, DockSlot::Center};
enum class Tool { Brush, Eraser, Eyedropper, Fill, Select };
ImU32 DisplayColor(uint32_t c, float opacity = 1) {
    return IM_COL32(c & 255, (c >> 8) & 255, (c >> 16) & 255, int((c >> 24) * opacity));
}
Vec2 RotatePoint(Vec2 p, float degrees) {
    const float a = degrees * 3.14159265358979323846f / 180;
    return {p.x * std::cos(a) - p.y * std::sin(a), p.x * std::sin(a) + p.y * std::cos(a)};
}
void PutKey(DotDocument& data, int motion, int bone, int frame, DotPose pose) {
    auto& keys = data.motions[motion].keys;
    const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto& k) { return k.frame == frame && k.bone == bone; });
    if (key == keys.end()) keys.push_back({frame, bone, pose}); else key->pose = pose;
}

class DotEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin(PanelWindowTitle("panel.doteditor", "mye.doteditor").c_str())) {
            FinishStroke(ctx.app ? ctx.app->DotDocumentForEditing() : nullptr);
            ImGui::End(); return;
        }
        if (!ctx.app || !ctx.project || !ctx.project->IsOpen()) {
            ImGui::TextUnformatted("프로젝트를 먼저 열어주세요."); ImGui::End(); return;
        }
        ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
        DrawDocumentBar(ctx);
        ImGui::EndDisabled();
        auto* doc = ctx.app->DotDocumentForEditing();
        if (!doc) {
            ImGui::TextWrapped("새 도트 문서를 만들거나 .dot 파일을 여세요. 참조 위에 그리기 → 파츠 선택 → 리깅과 포즈 → 모션 내보내기 순서로 제작합니다.");
            ImGui::End(); return;
        }
        doc->Commands().SetContext(&ctx);
        if (m_document != doc->Id() || m_root != ctx.project->RootDir()) {
            m_document = doc->Id(); m_root = ctx.project->RootDir(); m_frame = m_motion = 0; m_bone = -1;
            m_stroke.reset(); m_playing = false; m_fit = true; m_export.reset(); m_status.clear(); m_position = UINT64_MAX;
            m_referenceSource = nullptr;
            const auto path = doc->Path().empty() ? "assets/sprites/sprite-" + std::to_string(doc->Id().value) + ".dot" : std::string(doc->Path());
            std::snprintf(m_savePath.data(), m_savePath.size(), "%s", path.c_str());
            ImGui::SetWindowFocus();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ctx.app->SetDotFocused();
        if (m_position != doc->Commands().Position()) {
            m_position = doc->Commands().Position(); m_export.reset(); m_stroke.reset(); m_previewDirty = true; m_cursor = {}; m_playing = false;
        }
        ClampSelection(doc->Dot());
        ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
        DrawSaveBar(ctx, *doc);
        ImGui::EndDisabled();
        if (m_position != doc->Commands().Position()) {
            m_position = doc->Commands().Position(); m_export.reset(); m_stroke.reset(); m_previewDirty = true; m_cursor = {}; m_playing = false;
        }
        ClampSelection(doc->Dot());
        ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
        if (ImGui::BeginTable("##dot_workspace", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("도구", ImGuiTableColumnFlags_WidthFixed, 225);
            ImGui::TableSetupColumn("캔버스", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::BeginChild("##dot_tools", ImVec2(0, 0));
            DrawTools(ctx, *doc); DrawRig(*doc); DrawMotions(ctx, *doc);
            ImGui::EndChild(); ImGui::TableNextColumn();
            DrawCanvas(*doc); DrawTimeline(*doc);
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        FinishStroke(doc);
        ImGui::End();
    }
    void SerializeState(json::Value& out) const override {
        out = json::Value(json::Value::Object{{"type", json::Value(std::string(kDesc.id))}});
    }
private:
    DocumentId m_document{};
    std::string m_root, m_status;
    std::array<char, 4096> m_savePath{}, m_imagePath{};
    int m_frame = 0, m_motion = 0, m_bone = -1, m_mode = 0;
    Tool m_tool = Tool::Brush;
    uint32_t m_color = 0xff586ee6;
    bool m_grid = true, m_referenceVisible = true, m_onion = false, m_fit = true, m_playing = false, m_previewDirty = true;
    float m_opacity = .45f, m_scale = 8;
    Vec2 m_pan{};
    RectInt m_region{};
    Vec2i m_selectStart{}, m_lastPixel{};
    std::optional<DotDocument> m_stroke;
    int m_strokeFrame = 0;
    bool m_draggingBone = false, m_painted = false;
    Vec2 m_dragStart{};
    DotPose m_dragPose;
    std::optional<DotExport> m_export;
    uint64_t m_position = UINT64_MAX;
    DotImage m_preview, m_referencePreview;
    const DotImage* m_referenceSource = nullptr;
    asset::AnimationClipData m_playClip;
    anim::ClipCursor m_cursor;

    void ClampSelection(const DotDocument& data) {
        m_motion = std::clamp(m_motion, 0, int(data.motions.size()) - 1);
        m_frame = std::clamp(m_frame, 0, int(data.frames.size()) - 1);
        m_bone = std::clamp(m_bone, -1, int(data.bones.size()) - 1);
    }
    void Commit(Document& doc, DotDocument after, const char* label) {
        auto valid = after.Validate();
        if (!valid) { m_status = valid.GetError().message; return; }
        doc.Commands().Push(std::make_unique<DotEditCommand>(doc.Dot(), doc.Dot(), std::move(after), label));
        m_position = doc.Commands().Position();
        m_export.reset(); m_previewDirty = true; m_cursor = {}; m_playing = false;
    }
    void DrawDocumentBar(EditorContext& ctx) {
        if (imgui::EditorButton(imgui::EditorIcon::NewFile,"새 도트")) ctx.app->SelectDotDocument(ctx.project->NewDot()->Id());
        ImGui::SameLine();
        if (imgui::EditorButton(imgui::EditorIcon::Open,"불러오기")) {
            auto path = ctx.app->BrowseDotFile(false);
            if (!path) m_status = path.GetError().message;
            else if (!path.Value().empty()) { auto result = ctx.app->OpenDot(path.Value()); if (!result) m_status = result.GetError().message; }
        }
        ImGui::SameLine(); ImGui::SetNextItemWidth(200);
        auto* doc = ctx.app->DotDocumentForEditing();
        if (ImGui::BeginCombo("문서", doc ? doc->TabTitle().c_str() : ".dot")) {
            for (auto* candidate : ctx.project->Documents()) if (candidate->GetKind() == Document::Kind::Dot) {
                ImGui::PushID(int(candidate->Id().value));
                if (ImGui::Selectable(candidate->TabTitle().c_str(), candidate == doc)) ctx.app->SelectDotDocument(candidate->Id());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    void SaveSource(EditorContext& ctx, Document& doc) {
        auto result = ctx.project->SaveDot(doc.Id(), m_savePath.data());
        m_status = result ? "원본 저장 완료 — 참조·프레임·뼈대·포즈·모션 포함" : result.GetError().message;
    }
    void DrawSaveBar(EditorContext& ctx, Document& doc) {
        ImGui::SetNextItemWidth(std::max(100.f, ImGui::GetContentRegionAvail().x - 310));
        ImGui::InputText("##dot_save_path", m_savePath.data(), m_savePath.size());
        ImGui::SameLine();
        if (imgui::EditorButton(imgui::EditorIcon::Save,"원본 저장")) {
            auto target = Utf8Path(m_savePath.data()); if (!target.is_absolute()) target = Utf8Path(m_root) / target;
            std::error_code ec; const bool exists = std::filesystem::exists(target, ec);
            const bool same = !doc.Path().empty() && std::filesystem::equivalent(Utf8Path(doc.Path()), target, ec);
            if (exists && !same) ImGui::OpenPopup("도트 원본 덮어쓰기"); else SaveSource(ctx, doc);
        }
        if (ImGui::BeginPopup("도트 원본 덮어쓰기")) {
            ImGui::TextWrapped("기존 파일을 덮어씁니다: %s", m_savePath.data());
            if (ImGui::Button("덮어쓰기")) { SaveSource(ctx, doc); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine(); ImGui::BeginDisabled(!doc.Commands().CanUndo());
        if (ImGui::Button("Undo")) doc.Commands().Undo();
        ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!doc.Commands().CanRedo());
        if (ImGui::Button("Redo")) doc.Commands().Redo();
        ImGui::EndDisabled();
        if (!m_status.empty()) {
            ImGui::TextUnformatted(m_status.c_str()); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_status.c_str());
        }
        ImGui::Separator();
    }
    void ImportReference(Document& doc, const std::filesystem::path& path) {
        auto image = LoadDotImage(path);
        if (!image) { m_status = image.GetError().message; return; }
        auto after = doc.Dot(); after.reference = std::make_shared<const DotImage>(std::move(image).Value());
        Commit(doc, std::move(after), "참조 이미지 가져오기"); m_referenceVisible = true;
    }
    void DrawTools(EditorContext& ctx, Document& doc);
    void DrawRig(Document& doc);
    void DrawMotions(EditorContext& ctx, Document& doc);
    void DrawCanvas(Document& doc);
    void DrawTimeline(Document& doc);
    void AdvancePreview(const DotDocument& data);
    void FinishStroke(Document* doc) {
        if (!m_stroke || ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) return;
        if (doc && m_painted) Commit(*doc, std::move(*m_stroke), m_draggingBone ? "뼈 이동" : "픽셀 스트로크");
        m_stroke.reset(); m_draggingBone = false;
    }
    static void DrawPixels(ImDrawList& draw, const DotImage& image, const DotCanvasView& view, ImVec2 low, ImVec2 high, float opacity = 1);
};

void DotEditorPanel::DrawTools(EditorContext& ctx, Document& doc) {
    ImGui::SeparatorText("그리기");
    if (ImGui::Combo("편집", &m_mode, "그리기\0리깅 / 정지\0포즈 / 키프레임\0")) m_previewDirty = true;
    static const char* names[] = {"연필", "지우개", "색 추출", "채우기", "파츠 선택"};
    for (int i = 0; i < 5; ++i) {
        if (i % 2) ImGui::SameLine();
        if (ImGui::RadioButton(names[i], int(m_tool) == i)) { m_tool = Tool(i); m_mode = 0; }
    }
    float rgba[]{float(m_color & 255)/255, float((m_color>>8)&255)/255, float((m_color>>16)&255)/255, float(m_color>>24)/255};
    if (ImGui::ColorEdit4("색", rgba)) {
        m_color = uint32_t(std::lround(rgba[0]*255)) | uint32_t(std::lround(rgba[1]*255))<<8 |
            uint32_t(std::lround(rgba[2]*255))<<16 | uint32_t(std::lround(rgba[3]*255))<<24;
    }
    constexpr uint32_t colors[] = {0xff242126,0xfff8eee2,0xff476be3,0xff60a4ed,0xff62b0f0,0xff6ab858,0xffaa9b43,0xffbe865b};
    for (int i = 0; i < 8; ++i) {
        ImGui::PushID(i); if (i % 4) ImGui::SameLine();
        const auto c = colors[i];
        if (ImGui::ColorButton("##palette", ImVec4(float(c&255)/255,float((c>>8)&255)/255,float((c>>16)&255)/255,1), 0, ImVec2(28,24))) m_color = c;
        ImGui::PopID();
    }
    ImGui::Checkbox("그리드", &m_grid); ImGui::SameLine(); ImGui::Checkbox("이전 프레임", &m_onion);
    if (ImGui::CollapsingHeader("캔버스 크기 / 지우기")) {
        int size[]{doc.Dot().frames[0].image.width, doc.Dot().frames[0].image.height};
        if (ImGui::InputInt2("px (Enter)", size, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (size[0] >= 1 && size[1] >= 1 && size[0] <= 512 && size[1] <= 512 && doc.Dot().bones.empty() && size_t(size[0])*size[1]*doc.Dot().frames.size() <= 4*1024*1024) {
                auto after = doc.Dot();
                for (auto& frame : after.frames) {
                    DotImage image; image.width = size[0]; image.height = size[1]; image.pixels.assign(size_t(size[0])*size[1], 0);
                    for (int y = 0; y < std::min(size[1], frame.image.height); ++y)
                        std::copy_n(frame.image.pixels.begin()+size_t(y)*frame.image.width, std::min(size[0],frame.image.width), image.pixels.begin()+size_t(y)*size[0]);
                    frame.image = std::move(image);
                }
                Commit(doc, std::move(after), "캔버스 크기"); m_fit = true;
            } else m_status = "크기는 1–512 px, 전체 프레임은 4백만 픽셀 이내입니다. 리깅 전에 크기를 확정하세요.";
        }
        if (ImGui::Button("프레임 지우기")) ImGui::OpenPopup("프레임 지우기 확인");
        if (ImGui::BeginPopup("프레임 지우기 확인")) {
            if (ImGui::Button("모든 픽셀 지우기")) {
                auto after = doc.Dot(); std::fill(after.frames[m_frame].image.pixels.begin(), after.frames[m_frame].image.pixels.end(), 0u);
                Commit(doc,std::move(after),"프레임 지우기"); ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::SeparatorText("참조 이미지");
    if (ImGui::Button("PNG 가져오기")) {
        auto path = ctx.app->BrowseImageFile();
        if (!path) m_status = path.GetError().message;
        else if (!path.Value().empty()) ImportReference(doc,Utf8Path(path.Value()));
    }
    ImGui::Button("에셋 PNG 놓기", ImVec2(-1,28));
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
            const auto* text = static_cast<const char*>(payload->Data);
            if (payload->DataSize > 0 && text[payload->DataSize-1] == '\0') {
                const std::string_view vpath(text,size_t(payload->DataSize-1));
                if (vpath.starts_with("assets://") && vpath.ends_with(".png")) {
                    std::error_code ec;
                    const auto root = std::filesystem::weakly_canonical(Utf8Path(m_root)/"assets",ec);
                    const auto path = std::filesystem::weakly_canonical(root/Utf8Path(vpath.substr(9)),ec);
                    const auto relative = path.lexically_relative(root);
                    if (!ec && !relative.empty() && !relative.is_absolute() && *relative.begin() != "..") ImportReference(doc,path);
                    else m_status = "프로젝트 에셋 안의 PNG를 사용하세요.";
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::CollapsingHeader("PNG 경로로 가져오기")) {
        ImGui::InputText("경로",m_imagePath.data(),m_imagePath.size());
        if (ImGui::Button("경로에서 가져오기")) {
            auto path = Utf8Path(m_imagePath.data()); if (!path.is_absolute()) path = Utf8Path(m_root)/path;
            ImportReference(doc,path);
        }
    }
    if (doc.Dot().reference) {
        ImGui::Checkbox("참조 표시",&m_referenceVisible); ImGui::SliderFloat("불투명도",&m_opacity,.05f,1);
        if (ImGui::Button("참조 → 현재 프레임")) {
            auto after = doc.Dot(); const auto& current = after.frames[m_frame].image;
            after.frames[m_frame].image = ResampleDotImage(*after.reference,current.width,current.height);
            Commit(doc,std::move(after),"참조 픽셀 복사");
        }
        if (ImGui::Button("참조 제거")) { auto after = doc.Dot(); after.reference.reset(); Commit(doc,std::move(after),"참조 제거"); }
    }
}

void DotEditorPanel::DrawRig(Document& doc) {
    if (!ImGui::CollapsingHeader("파츠 / 뼈대", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("파츠 선택으로 영역을 드래그한 뒤 뼈에 연결합니다. 부모를 먼저 만들고 가려진 영역은 직접 채우세요.");
    int region[]{m_region.x,m_region.y,m_region.w,m_region.h};
    if (ImGui::InputInt4("영역 x/y/w/h",region)) m_region = {region[0],region[1],region[2],region[3]};
    if (ImGui::Button("영역 → 뼈 / 파츠 추가")) {
        auto after = doc.Dot(); auto result = AddDotBone(after,m_region,m_bone);
        if (result) {
            const int bone = int(after.bones.size())-1; Commit(doc,std::move(after),"파츠 리깅"); m_bone = bone; m_mode = 1;
        } else m_status = result.GetError().message;
    }
    if (ImGui::Selectable("루트에 연결 / 선택 해제",m_bone == -1)) m_bone = -1;
    for (size_t i = 0; i < doc.Dot().bones.size(); ++i) {
        ImGui::PushID(int(i)); const auto& b = doc.Dot().bones[i];
        int depth = 0, parent = b.parent; while (parent >= 0) { ++depth; parent = doc.Dot().bones[parent].parent; }
        const float indent = float(depth)*10;
        if (indent > 0) ImGui::Indent(indent);
        if (ImGui::Selectable(b.name.c_str(),m_bone == int(i))) m_bone = int(i);
        if (indent > 0) ImGui::Unindent(indent);
        ImGui::PopID();
    }
    if (m_bone < 0 || m_bone >= int(doc.Dot().bones.size())) return;
    const auto bone = doc.Dot().bones[m_bone];
    char name[129]; std::snprintf(name,sizeof(name),"%s",bone.name.c_str());
    if (ImGui::InputText("뼈 이름 (Enter)",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); after.bones[m_bone].name = name; Commit(doc,std::move(after),"뼈 이름");
    }
    if (ImGui::BeginCombo("부모",bone.parent < 0 ? "루트" : doc.Dot().bones[bone.parent].name.c_str())) {
        for (int parent = -1; parent < m_bone; ++parent) {
            if (!ImGui::Selectable(parent < 0 ? "루트" : doc.Dot().bones[parent].name.c_str(),parent == bone.parent)) continue;
            auto after = doc.Dot(); auto& changed = after.bones[m_bone];
            auto rebase = [&](DotPose world,const std::vector<DotPose>& poses) {
                if (parent < 0) return world;
                return DotPose{RotatePoint(world.position-poses[parent].position,-poses[parent].degrees),world.degrees-poses[parent].degrees};
            };
            const auto rest = DotWorldPoses(doc.Dot(),m_motion,0,true); changed.rest = rebase(rest[m_bone],rest);
            for (size_t m = 0; m < after.motions.size(); ++m) for (auto& k : after.motions[m].keys) if (k.bone == m_bone) {
                const auto poses = DotWorldPoses(doc.Dot(),int(m),float(k.frame),false); k.pose = rebase(poses[m_bone],poses);
            }
            changed.parent = parent; Commit(doc,std::move(after),"뼈 부모 변경"); break;
        }
        ImGui::EndCombo();
    }
    float pivot[]{bone.pivot.x,bone.pivot.y};
    if (ImGui::InputFloat2("피벗 px (Enter)",pivot,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); after.bones[m_bone].pivot = {pivot[0],pivot[1]}; Commit(doc,std::move(after),"파츠 피벗");
    }
    const bool poseMode = m_mode == 2;
    const auto pose = poseMode ? EvaluateDotPose(doc.Dot(),m_motion,m_bone,float(m_frame)) : bone.rest;
    float values[]{pose.position.x,pose.position.y,pose.degrees};
    if (ImGui::InputFloat3(poseMode ? "포즈 x/y/각도 (Enter)" : "정지 x/y/각도 (Enter)",values,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); const DotPose changed{{values[0],values[1]},values[2]};
        if (poseMode) PutKey(after,m_motion,m_bone,m_frame,changed); else after.bones[m_bone].rest = changed;
        Commit(doc,std::move(after),poseMode ? "포즈 키" : "정지 포즈");
    }
    const auto motion = doc.Dot().motions[m_motion];
    ImGui::BeginDisabled(m_frame < motion.first || m_frame > motion.last);
    if (ImGui::Button("현재 포즈 키")) {
        auto after = doc.Dot(); PutKey(after,m_motion,m_bone,m_frame,EvaluateDotPose(after,m_motion,m_bone,float(m_frame)));
        Commit(doc,std::move(after),"포즈 키 추가"); m_mode = 2;
    }
    if (ImGui::Button("정지 포즈 키")) {
        auto after = doc.Dot(); PutKey(after,m_motion,m_bone,m_frame,bone.rest); Commit(doc,std::move(after),"정지 포즈 키");
    }
    if (ImGui::Button("현재 키 삭제")) {
        auto after = doc.Dot(); std::erase_if(after.motions[m_motion].keys,[&](const auto& k) { return k.bone == m_bone && k.frame == m_frame; });
        Commit(doc,std::move(after),"포즈 키 삭제");
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(m_bone+1 != int(doc.Dot().bones.size()));
    if (ImGui::Button("마지막 뼈 제거")) {
        auto after = doc.Dot();
        for (auto& m : after.motions) std::erase_if(m.keys,[&](const auto& k) { return k.bone == m_bone; });
        after.bones.pop_back(); Commit(doc,std::move(after),"뼈 제거"); --m_bone;
    }
    ImGui::EndDisabled();
}

void DotEditorPanel::DrawMotions(EditorContext& ctx, Document& doc) {
    if (!ImGui::CollapsingHeader("모션 / 게임 연결",ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto motion = doc.Dot().motions[m_motion];
    if (ImGui::BeginCombo("모션",motion.name.c_str())) {
        for (size_t i = 0; i < doc.Dot().motions.size(); ++i) if (ImGui::Selectable(doc.Dot().motions[i].name.c_str(),m_motion == int(i))) {
            m_motion = int(i); m_frame = doc.Dot().motions[i].first; m_cursor = {}; m_playing = false; m_previewDirty = true;
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(doc.Dot().motions.size() >= 32);
    if (ImGui::Button("+ 모션")) {
        auto after = doc.Dot(); DotMotion added; added.name = "motion_" + std::to_string(after.motions.size());
        while (std::any_of(after.motions.begin(),after.motions.end(),[&](const auto& m) { return m.name == added.name; })) added.name += "_";
        added.first = added.last = m_frame; after.motions.push_back(std::move(added));
        const int selected = int(after.motions.size())-1; Commit(doc,std::move(after),"모션 추가");
        m_motion = std::min(selected,int(doc.Dot().motions.size())-1);
    }
    ImGui::EndDisabled();
    motion = doc.Dot().motions[m_motion];
    char name[129]; std::snprintf(name,sizeof(name),"%s",motion.name.c_str());
    if (ImGui::InputText("모션 이름 (Enter)",name,sizeof(name),ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); for (auto& m : after.motions) if (m.next == motion.name) m.next = name;
        after.motions[m_motion].name = name; Commit(doc,std::move(after),"모션 이름");
    }
    int range[]{motion.first,motion.last};
    if (ImGui::InputInt2("시작 / 끝 (Enter)",range,ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); after.motions[m_motion].first = range[0]; after.motions[m_motion].last = range[1];
        Commit(doc,std::move(after),"모션 구간");
    }
    bool loop = motion.loop;
    if (ImGui::Checkbox("반복",&loop)) { auto after = doc.Dot(); after.motions[m_motion].loop = loop; Commit(doc,std::move(after),"모션 반복"); }
    int direction = int(motion.direction);
    if (ImGui::Combo("재생 방향",&direction,"정방향\0역방향\0왕복\0")) {
        auto after = doc.Dot(); after.motions[m_motion].direction = asset::AnimationClipData::Direction(direction); Commit(doc,std::move(after),"모션 방향");
    }
    if (ImGui::BeginCombo("끝난 뒤 연결",motion.next.empty() ? "없음" : motion.next.c_str())) {
        if (ImGui::Selectable("없음",motion.next.empty())) { auto after = doc.Dot(); after.motions[m_motion].next.clear(); Commit(doc,std::move(after),"모션 연결"); }
        for (const auto& m : doc.Dot().motions) if (ImGui::Selectable(m.name.c_str(),motion.next == m.name)) {
            auto after = doc.Dot(); after.motions[m_motion].next = m.name; Commit(doc,std::move(after),"모션 연결"); break;
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(doc.Dot().motions.size() == 1);
    if (ImGui::Button("모션 삭제")) {
        auto after = doc.Dot(); const auto removed = after.motions[m_motion].name;
        after.motions.erase(after.motions.begin()+m_motion); for (auto& m : after.motions) if (m.next == removed) m.next.clear();
        Commit(doc,std::move(after),"모션 삭제"); m_motion = 0;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("PNG 시트 + 모션 내보내기")) {
        auto result = ExportDotMotions(doc.Dot(),Utf8Path(m_root));
        if (!result) { m_status = result.GetError().message; MYE_LOG_ERROR("Editor","{}",m_status); }
        else {
            m_export = std::move(result).Value(); m_status = "내보내기 완료: " + Utf8String(m_export->directory);
            if (ctx.app->Viewport()) {
                auto refreshed = ctx.app->Viewport()->RefreshAssetIndex();
                if (!refreshed) { m_status += " / " + refreshed.GetError().message; m_export.reset(); }
            }
        }
    }
    ImGui::BeginDisabled(!m_export || m_motion >= int(m_export->animations.size()));
    static const char* labels[] = {"선택 캐릭터 → 대기", "선택 캐릭터 → 걷기", "선택 스프라이트 → 재생"};
    for (int action = 0; action < 3; ++action) if (ImGui::Button(labels[action]) && m_export) {
        const auto entity = ctx.selection ? ctx.selection->Primary().AsEntity() : ecs::Entity{};
        auto result = action == 2 ? AssignAnimationToEntity(ctx,entity,m_export->animations[m_motion]) : AssignCharacterMotion(ctx,entity,m_export->animations[m_motion],action == 1);
        m_status = result ? "모션 연결 완료. 씬 저장 후 Play에서 확인하세요." : result.GetError().message;
    }
    ImGui::EndDisabled();
}

void DotEditorPanel::DrawPixels(ImDrawList& draw, const DotImage& image, const DotCanvasView& view, ImVec2 low, ImVec2 high, float opacity) {
    const auto start = view.PixelAt({low.x,low.y}), end = view.PixelAt({high.x,high.y});
    const int x0 = std::clamp(int(std::floor(start.x)),0,image.width), y0 = std::clamp(int(std::floor(start.y)),0,image.height);
    const int x1 = std::clamp(int(std::ceil(end.x)),0,image.width), y1 = std::clamp(int(std::ceil(end.y)),0,image.height);
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1;) {
        const auto color = image.pixels[size_t(y)*image.width+x]; int last = x+1;
        while (last < x1 && image.pixels[size_t(y)*image.width+last] == color) ++last;
        if (color>>24) draw.AddRectFilled(ImVec2(view.origin.x+x*view.scale,view.origin.y+y*view.scale),
            ImVec2(view.origin.x+last*view.scale,view.origin.y+(y+1)*view.scale),DisplayColor(color,opacity));
        x = last;
    }
}

void DotEditorPanel::DrawCanvas(Document& doc) {
    if (ImGui::Button("맞춤")) m_fit = true;
    ImGui::SameLine(); if (ImGui::Button("1:1")) { m_scale = 1; m_pan = {}; m_fit = false; }
    ImGui::SameLine(); ImGui::SetNextItemWidth(110);
    if (ImGui::SliderFloat("배율",&m_scale,.25f,64.f,"%.2fx",ImGuiSliderFlags_Logarithmic)) m_fit = false;
    ImGui::TextDisabled("휠: 확대/축소 · 가운데 버튼: 이동 · 우클릭: 지우기");
    const float height = std::max(100.f,ImGui::GetContentRegionAvail().y-155);
    ImGui::BeginChild("##dot_canvas",ImVec2(0,height),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const auto low = ImGui::GetCursorScreenPos(); auto size = ImGui::GetContentRegionAvail(); size.x = std::max(size.x,1.f); size.y = std::max(size.y,1.f);
    const ImVec2 high(low.x+size.x,low.y+size.y);
    const int width = doc.Dot().frames[0].image.width, imageHeight = doc.Dot().frames[0].image.height;
    if (m_fit) {
        DotCanvasView fitted; fitted.Fit({size.x,size.y},{width,imageHeight});
        m_scale = fitted.scale; m_pan = fitted.origin;
    }
    DotCanvasView view{{low.x+m_pan.x,low.y+m_pan.y},m_scale};
    ImGui::InvisibleButton("##pixel_canvas",size,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(); auto& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0) {
        m_fit = false;
        view.ZoomAt({io.MousePos.x,io.MousePos.y},view.scale*std::pow(1.25f,io.MouseWheel));
        m_scale = view.scale; m_pan = {view.origin.x-low.x,view.origin.y-low.y};
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_fit = false;
        m_pan = m_pan+Vec2{io.MouseDelta.x,io.MouseDelta.y}; view.origin = {low.x+m_pan.x,low.y+m_pan.y};
    }
    const auto mouse = view.PixelAt({io.MousePos.x,io.MousePos.y});
    const Vec2i pixel{int(std::floor(mouse.x)),int(std::floor(mouse.y))};
    const bool inside = pixel.x >= 0 && pixel.y >= 0 && pixel.x < width && pixel.y < imageHeight;
    if (m_playing) AdvancePreview(doc.Dot());
    if (m_mode == 0 && !m_playing && (active || hovered)) {
        if (inside && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            if (m_tool == Tool::Select) m_selectStart = pixel;
            else if (m_tool == Tool::Eyedropper) m_color = doc.Dot().frames[m_frame].image.pixels[size_t(pixel.y)*width+pixel.x];
            else {
                m_stroke = doc.Dot(); m_strokeFrame = m_frame; m_lastPixel = pixel; m_draggingBone = false; m_painted = false;
            }
        }
        if (m_tool == Tool::Select && active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && inside)
            m_region = {std::min(pixel.x,m_selectStart.x),std::min(pixel.y,m_selectStart.y),std::abs(pixel.x-m_selectStart.x)+1,std::abs(pixel.y-m_selectStart.y)+1};
        if (m_stroke && !m_draggingBone && inside && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))) {
            const uint32_t color = (m_tool == Tool::Eraser || ImGui::IsMouseDown(ImGuiMouseButton_Right)) ? 0 : m_color;
            if (m_tool == Tool::Fill) { if (!m_painted) FillDotRegion(m_stroke->frames[m_strokeFrame].image,pixel,color); }
            else PaintDotLine(m_stroke->frames[m_strokeFrame].image,m_lastPixel,pixel,color);
            m_lastPixel = pixel; m_painted = true;
        }
    } else if (!m_playing && m_mode > 0 && active && !doc.Dot().bones.empty()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto poses = DotWorldPoses(doc.Dot(),m_motion,float(m_frame),m_mode == 1);
            float nearest = 12/view.scale; int selected = -1;
            for (size_t i = 0; i < poses.size(); ++i) {
                const auto delta = mouse-poses[i].position; const float distance = std::sqrt(delta.x*delta.x+delta.y*delta.y);
                if (distance < nearest) { nearest = distance; selected = int(i); }
            }
            if (selected >= 0) {
                m_bone = selected; m_stroke = doc.Dot(); m_draggingBone = true; m_painted = false; m_dragStart = mouse;
                m_dragPose = m_mode == 1 ? doc.Dot().bones[m_bone].rest : EvaluateDotPose(doc.Dot(),m_motion,m_bone,float(m_frame));
            }
        }
        if (m_stroke && m_draggingBone && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto pose = m_dragPose; auto delta = mouse-m_dragStart; const int parent = doc.Dot().bones[m_bone].parent;
            if (parent >= 0) {
                const auto poses = DotWorldPoses(doc.Dot(),m_motion,float(m_frame),m_mode == 1); delta = RotatePoint(delta,-poses[parent].degrees);
            }
            pose.position = pose.position+delta;
            if (m_mode == 1) m_stroke->bones[m_bone].rest = pose; else PutKey(*m_stroke,m_motion,m_bone,m_frame,pose);
            m_painted = true;
        }
    }
    const auto& display = m_stroke ? *m_stroke : doc.Dot();
    auto& draw = *ImGui::GetWindowDrawList(); draw.PushClipRect(low,high,true);
    draw.AddRectFilled(low,high,IM_COL32(25,25,25,255));
    const ImVec2 canvasHigh(view.origin.x+width*view.scale,view.origin.y+imageHeight*view.scale);
    draw.AddRectFilled(ImVec2(view.origin.x,view.origin.y),canvasHigh,IM_COL32(55,55,55,255));
    if (display.reference && m_referenceVisible) {
        if (m_referenceSource != display.reference.get() || m_referencePreview.width != width || m_referencePreview.height != imageHeight) {
            m_referencePreview = ResampleDotImage(*display.reference,width,imageHeight); m_referenceSource = display.reference.get();
        }
        DrawPixels(draw,m_referencePreview,view,low,high,m_opacity);
    }
    if (m_onion && m_frame > 0) DrawPixels(draw,display.frames[m_frame-1].image,view,low,high,.2f);
    if (m_mode == 0 && !m_playing) DrawPixels(draw,display.frames[m_frame].image,view,low,high);
    else {
        if (m_previewDirty || m_stroke) { m_preview = RenderDotFrame(display,m_motion,m_frame,m_mode == 1 && !m_playing); m_previewDirty = false; }
        DrawPixels(draw,m_preview,view,low,high);
        const auto poses = DotWorldPoses(display,m_motion,float(m_frame),m_mode == 1 && !m_playing);
        for (size_t i = 0; i < poses.size(); ++i) {
            const auto point = poses[i].position*view.scale+view.origin;
            const auto color = int(i) == m_bone ? IM_COL32(255,190,65,255) : IM_COL32(85,205,220,255);
            if (display.bones[i].parent >= 0) {
                const auto parent = poses[display.bones[i].parent].position*view.scale+view.origin;
                draw.AddLine(ImVec2(parent.x,parent.y),ImVec2(point.x,point.y),color,2);
            }
            draw.AddCircleFilled(ImVec2(point.x,point.y),5,color);
        }
    }
    if (m_grid && view.scale >= 5) {
        const auto first = view.PixelAt({low.x,low.y}), last = view.PixelAt({high.x,high.y});
        for (int x = std::clamp(int(std::ceil(first.x)),0,width); x <= std::clamp(int(std::floor(last.x)),0,width); ++x)
            draw.AddLine(ImVec2(view.origin.x+x*view.scale,view.origin.y),ImVec2(view.origin.x+x*view.scale,canvasHigh.y),IM_COL32(140,140,140,60));
        for (int y = std::clamp(int(std::ceil(first.y)),0,imageHeight); y <= std::clamp(int(std::floor(last.y)),0,imageHeight); ++y)
            draw.AddLine(ImVec2(view.origin.x,view.origin.y+y*view.scale),ImVec2(canvasHigh.x,view.origin.y+y*view.scale),IM_COL32(140,140,140,60));
    }
    draw.AddRect(ImVec2(view.origin.x,view.origin.y),canvasHigh,IM_COL32(165,165,165,255));
    if (m_mode == 0 && m_region.w > 0 && m_region.h > 0)
        draw.AddRect(ImVec2(view.origin.x+m_region.x*view.scale,view.origin.y+m_region.y*view.scale),
            ImVec2(view.origin.x+(m_region.x+m_region.w)*view.scale,view.origin.y+(m_region.y+m_region.h)*view.scale),IM_COL32(255,190,65,255),0.f,2.f);
    draw.PopClipRect(); ImGui::EndChild();
}

void DotEditorPanel::AdvancePreview(const DotDocument& data) {
    if (m_playClip.name != data.motions[m_motion].name) m_playClip = DotPlaybackClip(data,m_motion);
    anim::AdvanceClip(m_playClip,m_cursor,std::min(ImGui::GetIO().DeltaTime,.1f),[](const asset::AnimEventMarker&) {});
    if (m_cursor.finished) {
        const auto& next = data.motions[m_motion].next;
        const auto found = std::find_if(data.motions.begin(),data.motions.end(),[&](const auto& m) { return m.name == next; });
        if (found != data.motions.end()) { m_motion = int(found-data.motions.begin()); m_cursor = {}; m_playClip = DotPlaybackClip(data,m_motion); m_previewDirty = true; }
        else m_playing = false;
    }
    const int frame = int(anim::CurrentFrameIndex(m_playClip,m_cursor));
    if (frame != m_frame) { m_frame = frame; m_previewDirty = true; }
}

void DotEditorPanel::DrawTimeline(Document& doc) {
    ImGui::BeginChild("##dot_timeline",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
    if (imgui::EditorButton(m_playing ? imgui::EditorIcon::Pause : imgui::EditorIcon::Play,m_playing ? "일시 정지" : "재생")) {
        m_playing = !m_playing; m_previewDirty = true;
        if (m_playing) { m_cursor = {}; m_playClip = DotPlaybackClip(doc.Dot(),m_motion); }
    }
    ImGui::SameLine(); ImGui::BeginDisabled(doc.Dot().frames.size() >= 64);
    if (ImGui::Button("프레임 복제")) {
        auto after = doc.Dot(); after.frames.push_back(after.frames[m_frame]); Commit(doc,std::move(after),"프레임 복제");
        m_frame = int(doc.Dot().frames.size())-1;
    }
    ImGui::SameLine();
    if (ImGui::Button("빈 프레임")) {
        auto after = doc.Dot(); auto frame = after.frames[m_frame]; std::fill(frame.image.pixels.begin(),frame.image.pixels.end(),0u);
        after.frames.push_back(std::move(frame)); Commit(doc,std::move(after),"빈 프레임"); m_frame = int(doc.Dot().frames.size())-1;
    }
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(doc.Dot().frames.size() == 1);
    if (ImGui::Button("프레임 삭제")) {
        auto after = doc.Dot(); auto result = RemoveDotFrame(after,m_frame);
        if (result) { Commit(doc,std::move(after),"프레임 삭제"); m_frame = std::min(m_frame,int(doc.Dot().frames.size())-1); }
    }
    ImGui::EndDisabled();
    float seconds = doc.Dot().frames[m_frame].seconds; ImGui::SetNextItemWidth(90);
    if (ImGui::InputFloat("프레임 초 (Enter)",&seconds,0,0,"%.3f",ImGuiInputTextFlags_EnterReturnsTrue)) {
        auto after = doc.Dot(); after.frames[m_frame].seconds = seconds; Commit(doc,std::move(after),"프레임 시간");
    }
    ImGui::SameLine(); ImGui::Text("%d / %zu · %zu 포즈 키",m_frame,doc.Dot().frames.size()-1,doc.Dot().motions[m_motion].keys.size());
    for (size_t i = 0; i < doc.Dot().frames.size(); ++i) {
        ImGui::PushID(int(i)); if (i) ImGui::SameLine();
        const auto keys = std::count_if(doc.Dot().motions[m_motion].keys.begin(),doc.Dot().motions[m_motion].keys.end(),[&](const auto& k) { return k.frame == int(i); });
        const auto label = std::to_string(i)+(keys ? " *" : "");
        if (ImGui::Selectable(label.c_str(),m_frame == int(i),0,ImVec2(46,32))) { m_frame = int(i); m_playing = false; m_previewDirty = true; }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

class DotEditorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<DotEditorPanel>(); }
};
}
std::unique_ptr<IEditorPanelFactory> MakeDotEditorPanelFactory() { return std::make_unique<DotEditorPanelFactory>(); }
} // namespace mye::editor
