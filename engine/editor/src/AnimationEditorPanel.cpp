#include "mye/editor/BuiltinPanels.h"
#include "mye/editor/EditorApp.h"
#include "mye/editor/AnimEditing.h"
#include "mye/editor/Project.h"
#include "mye/editor/Viewport.h"
#include "mye/editor/Selection.h"
#include "mye/asset/AssetDatabase.h"
#include "mye/asset/SpriteImporter.h"
#include "mye/core/Module.h"
#include "mye/core/JsonFile.h"
#include "mye/ecs/World.h"
#include "mye/scene/Renderable.h"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

namespace mye::editor {
namespace {
const PanelDesc kDesc{"mye.anim", "애니메이션", false, DockSlot::RightBottom};

class AnimationEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin(PanelWindowTitle("panel.anim", "mye.anim").c_str())) { ImGui::End(); return; }
        if (!ctx.app || !ctx.project || !ctx.project->IsOpen()) {
            ImGui::TextUnformatted("프로젝트를 먼저 열어주세요."); ImGui::End(); return;
        }
        if (ImGui::Button("새 클립")) {
            auto* doc = ctx.project->NewAnimation();
            ctx.app->SelectAnimationDocument(doc->Id());
        }
        ImGui::SameLine();
        auto* doc = ctx.app->AnimationDocument();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##문서", doc ? doc->TabTitle().c_str() : "클립 선택")) {
            for (auto* candidate : ctx.project->Documents()) {
                if (candidate->GetKind() != Document::Kind::Asset) continue;
                ImGui::PushID(static_cast<int>(candidate->Id().value));
                if (ImGui::Selectable(candidate->TabTitle().c_str(), candidate == doc)) {
                    ctx.app->SelectAnimationDocument(candidate->Id()); doc = candidate;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("열린 .anim 문서를 선택합니다. 에셋 브라우저에서 파일을 더블 클릭해도 됩니다.");
        if (!doc) { ImGui::TextWrapped("새 클립을 만들거나 에셋 브라우저에서 .anim 파일을 열어주세요."); ImGui::End(); return; }
        if (m_document != doc->Id() || m_root != ctx.project->RootDir()) {
            m_document = doc->Id(); m_root = ctx.project->RootDir(); m_cursor = {}; m_playing = false;
            m_status.clear();
            const auto relative = doc->Path().empty() ? std::filesystem::path{}
                : Utf8Path(doc->Path()).lexically_relative(Utf8Path(ctx.project->RootDir()));
            const auto savePath = relative.empty() ? std::string("assets/animations/new_animation.anim") : Utf8String(relative);
            std::snprintf(m_savePath.data(), m_savePath.size(), "%s", savePath.c_str());
        }
        doc->Commands().SetContext(&ctx);
        auto& data = doc->Animation();
        if (ImGui::Button("저장")) {
            auto target = Utf8Path(m_savePath.data());
            if (!target.is_absolute()) target = Utf8Path(ctx.project->RootDir()) / target;
            std::error_code ec;
            const bool exists = std::filesystem::exists(target, ec);
            const bool same = !doc->Path().empty() && std::filesystem::equivalent(Utf8Path(doc->Path()), target, ec);
            if (exists && !same) ImGui::OpenPopup("기존 애니메이션 덮어쓰기");
            else SaveDocument(ctx, *doc);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("클립 탭의 경로에 저장합니다. 다른 이름으로 저장하려면 경로를 바꾸세요.");
        if (ImGui::BeginPopupModal("기존 애니메이션 덮어쓰기", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("기존 파일을 덮어씁니다: %s", m_savePath.data());
            if (ImGui::Button("덮어쓰기")) { SaveDocument(ctx, *doc); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("취소")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!doc->Commands().CanUndo());
        if (ImGui::Button("되돌리기")) { doc->Commands().Undo(); m_cursor = {}; }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!doc->Commands().CanRedo());
        if (ImGui::Button("다시 실행")) { doc->Commands().Redo(); m_cursor = {}; }
        ImGui::EndDisabled();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ctx.app->SetAnimationFocused();
        if (m_commandPosition != doc->Commands().Position()) { m_cursor = {}; m_commandPosition = doc->Commands().Position(); }
        if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());
        auto valid = data.Validate();
        if (!valid) ImGui::TextWrapped("%s", valid.GetError().message.c_str());
        if (valid && m_playing) {
            anim::AdvanceClip(data.clip, m_cursor, std::min(ImGui::GetIO().DeltaTime, 0.1f) * m_speed,
                              [](const asset::AnimEventMarker&) {});
            if (m_cursor.finished) m_playing = false;
        }
        auto* viewport = ctx.app->Viewport();
        auto preview = viewport ? viewport->AssetTexture(data.sheet.texture.guid)
                                : Expected<IEditorViewport::TexturePreview, Error>(Error{"Preview unavailable", 1});
        if (ImGui::BeginTabBar("애니메이션 작업")) {
            if (ImGui::BeginTabItem("미리보기")) {
                if (ImGui::Button(m_playing ? "일시 정지" : "재생")) {
                    if (m_cursor.finished) m_cursor = {};
                    m_playing = !m_playing;
                }
                ImGui::SameLine();
                if (ImGui::Button("처음으로")) { m_cursor = {}; m_playing = false; }
                ImGui::TextUnformatted("미리보기 속도"); ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##미리보기 속도", &m_speed, 0.1f, 3.0f, "%.1fx");
                if (preview && data.imageSize.x == static_cast<int32_t>(preview.Value().width) && data.imageSize.y == static_cast<int32_t>(preview.Value().height)) {
                    const auto index = anim::CurrentFrameIndex(data.clip, m_cursor);
                    if (index < data.sheet.frames.size()) {
                        ImGui::Text("현재 시트 프레임: %u", index);
                        DrawImage(preview.Value(), data.sheet.frames[index], ImGui::GetFontSize() * 5.0f);
                    }
                } else if (preview) ImGui::TextWrapped("시트 크기가 PNG와 다릅니다. 시트를 다시 설정하세요.");
                else ImGui::TextWrapped("%s", preview.GetError().message.c_str());
                DrawTimeline(*doc);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("클립")) {
                ImGui::TextUnformatted("저장 경로 (프로젝트 기준)"); ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##저장 경로", m_savePath.data(), m_savePath.size());
                ImGui::BeginDisabled(ctx.playMode && ctx.playMode->IsPlaying());
                if (ImGui::Button("선택한 스프라이트에 지정", ImVec2(-1, 0))) {
                    auto* db = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
                    const auto relative = Utf8Path(doc->Path()).lexically_relative(Utf8Path(ctx.project->RootDir()) / "assets");
                    asset::AssetRef ref;
                    if (db) ref.guid = db->GuidFromPath("assets://" + Utf8String(relative));
                    auto assigned = AssignAnimationToEntity(ctx, ctx.selection ? ctx.selection->Primary().AsEntity() : ecs::Entity{}, ref);
                    m_status = assigned ? "씬에 지정했습니다. 씬 저장 후 Play에서 재생됩니다." : assigned.GetError().message;
                }
                ImGui::EndDisabled();
                DrawSettings(ctx, *doc);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("시트")) {
                DrawSheetSettings(ctx, *doc);
                if (preview) DrawSheetFrames(*doc, preview.Value());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("이벤트")) {
                DrawEvents(*doc);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
private:
    void SaveDocument(EditorContext& ctx, Document& doc) {
        const auto saved = ctx.project->SaveAnimation(doc.Id(), m_savePath.data());
        m_status = saved ? "애니메이션 저장 완료" : saved.GetError().message;
        if (saved && ctx.app->Viewport()) {
            auto refreshed = ctx.app->Viewport()->RefreshAssetIndex();
            if (!refreshed) m_status += " / " + refreshed.GetError().message;
        }
    }
    void Commit(Document& doc, asset::AnimationAsset after, const char* label) {
        doc.Commands().Push(std::make_unique<AnimAssetEditCommand>(doc.Animation(), std::move(after), label));
        m_cursor = {};
    }
    static void DrawImage(const IEditorViewport::TexturePreview& texture, const asset::SpriteFrame& frame, float height) {
        const auto& r = frame.rect;
        if (r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0 ||
            int64_t{r.x} + r.w > texture.width || int64_t{r.y} + r.h > texture.height) return;
        const float width = std::min(height * r.w / r.h, ImGui::GetContentRegionAvail().x);
        ImGui::Image(reinterpret_cast<ImTextureID>(texture.id), ImVec2(width, width * r.h / r.w),
            ImVec2(float(r.x) / texture.width, float(r.y) / texture.height),
            ImVec2(float(r.x + r.w) / texture.width, float(r.y + r.h) / texture.height));
    }
    void DrawSettings(EditorContext& ctx, Document& doc) {
        auto& data = doc.Animation();
        char name[256]; std::snprintf(name, sizeof(name), "%s", data.clip.name.c_str());
        ImGui::TextUnformatted("클립 이름 (Enter로 적용)"); ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##클립 이름", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
            auto after = data; after.clip.name = name; Commit(doc, std::move(after), "클립 이름");
        }
        bool loop = data.clip.loop;
        if (ImGui::Checkbox("반복", &loop)) { auto after = data; after.clip.loop = loop; Commit(doc, std::move(after), "반복"); }
        int direction = static_cast<int>(data.clip.direction);
        ImGui::TextUnformatted("재생 방향"); ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##방향", &direction, "정방향\0역방향\0왕복\0")) {
            auto after = data; after.clip.direction = static_cast<asset::AnimationClipData::Direction>(direction);
            Commit(doc, std::move(after), "재생 방향");
        }
        ImGui::TextWrapped("프레임마다 표시 시간을 정하고 반복 또는 왕복 재생을 선택합니다. 그림과 리깅은 외부 도구에서 제작하세요.");
        ImGui::SeparatorText("후속 모션");
        ImGui::TextWrapped("반복을 끈 클립이 끝나면 재생할 .anim 파일을 놓습니다. 캐릭터의 대기·걷기 전환은 조작 컴포넌트에서 설정합니다.");
        auto* db = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
        const auto path = db ? db->PathFromGuid(data.nextAnimation.guid) : std::string{};
        ImGui::Button((path.empty() ? "후속 .anim 놓기" : path.c_str()), ImVec2(-1, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
                const auto* text = static_cast<const char*>(payload->Data);
                if (db && text && payload->DataSize > 1 && text[payload->DataSize - 1] == '\0') {
                    const std::string_view assetPath(text, payload->DataSize - 1);
                    const auto guid = db->GuidFromPath(assetPath);
                    if (assetPath.ends_with(".anim") && guid.IsValid()) {
                        auto after = data; after.nextAnimation = {guid, 0};
                        Commit(doc, std::move(after), "후속 모션");
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::BeginDisabled(!data.nextAnimation.guid.IsValid());
        if (ImGui::Button("후속 모션 비우기")) {
            auto after = data; after.nextAnimation = {}; Commit(doc, std::move(after), "후속 모션 비우기");
        }
        ImGui::EndDisabled();
    }
    void DrawSheetSettings(EditorContext& ctx, Document& doc) {
        auto& data = doc.Animation();
        ImGui::TextWrapped("PNG 시트를 에셋 브라우저에서 아래로 드래그하세요. 기존 시트 설정은 Undo로 복원할 수 있습니다.");
        ImGui::Button("PNG 시트 놓기", ImVec2(-1, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("MYE_ASSET")) {
                const auto* text = static_cast<const char*>(payload->Data);
                auto* db = ctx.engine ? ctx.engine->GetService<asset::AssetDatabase>() : nullptr;
                if (db && text && payload->DataSize > 0 && text[payload->DataSize - 1] == '\0' &&
                    std::string_view(text).ends_with(".png") && ctx.app->Viewport()) {
                    auto texture = ctx.app->Viewport()->AssetTexture(db->GuidFromPath(text));
                    if (texture) {
                        auto after = data;
                        after.sheet.texture.guid = db->GuidFromPath(text);
                        after.imageSize = {static_cast<int32_t>(texture.Value().width), static_cast<int32_t>(texture.Value().height)};
                        after.sheet.frames.clear(); after.clip.frameIndices.clear(); after.clip.frameDurations.clear(); after.clip.events.clear();
                        Commit(doc, std::move(after), "시트 선택");
                    } else m_status = texture.GetError().message;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextUnformatted("그리드 셀 크기 (px)"); ImGui::SetNextItemWidth(-1);
        ImGui::InputInt2("##그리드 셀 크기", m_cell.data());
        ImGui::BeginDisabled(m_cell[0] <= 0 || m_cell[1] <= 0 || data.imageSize.x <= 0 || data.imageSize.y <= 0);
        if (ImGui::Button("그리드에서 프레임 만들기", ImVec2(-1, 0))) {
            const int64_t count = int64_t{data.imageSize.x / m_cell[0]} * (data.imageSize.y / m_cell[1]);
            if (count > 0 && count <= 4096) {
                auto after = data;
                asset::SpriteSheetImportSettings settings; settings.frameSize = {m_cell[0], m_cell[1]};
                after.sheet.frames = asset::SpriteSheetImporter::SliceGrid(data.imageSize, settings);
                after.clip.frameIndices.clear(); after.clip.frameDurations.clear(); after.clip.events.clear();
                for (uint32_t i = 0; i < after.sheet.frames.size(); ++i) {
                    after.sheet.frames[i].pivot = {0.5f, 1.0f};
                    after.clip.frameIndices.push_back(i); after.clip.frameDurations.push_back(0.15f);
                }
                Commit(doc, std::move(after), "그리드 프레임");
            } else m_status = "시트 프레임 수는 1~4096이어야 합니다.";
        }
        ImGui::EndDisabled();
    }
    void DrawSheetFrames(Document& doc, const IEditorViewport::TexturePreview& texture) {
        if (!ImGui::CollapsingHeader("시트 프레임 / 클릭하여 타임라인에 추가")) return;
        auto& data = doc.Animation();
        for (size_t i = 0; i < data.sheet.frames.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            DrawImage(texture, data.sheet.frames[i], 64);
            if (ImGui::IsItemClicked()) {
                auto after = data;
                after.clip = anim_edit::WithFrameAppended(data.clip, static_cast<uint32_t>(i), 0.15f);
                Commit(doc, std::move(after), "프레임 추가");
            }
            int rect[4]{data.sheet.frames[i].rect.x, data.sheet.frames[i].rect.y,
                        data.sheet.frames[i].rect.w, data.sheet.frames[i].rect.h};
            ImGui::TextUnformatted("영역 x, y, 너비, 높이 (Enter)"); ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt4("##영역", rect, ImGuiInputTextFlags_EnterReturnsTrue)) {
                auto after = data;
                after.sheet.frames[i].rect = {rect[0], rect[1], rect[2], rect[3]};
                after.sheet.frames[i].uv = {float(rect[0]) / data.imageSize.x, float(rect[1]) / data.imageSize.y,
                                           float(rect[2]) / data.imageSize.x, float(rect[3]) / data.imageSize.y};
                if (after.Validate()) Commit(doc, std::move(after), "프레임 영역"); else m_status = "프레임 영역이 시트 밖입니다.";
            }
            const auto& f = data.sheet.frames[i];
            float pivot[2]{f.pivotInPixels ? f.pivot.x : f.pivot.x * f.rect.w,
                           f.pivotInPixels ? f.pivot.y : f.pivot.y * f.rect.h};
            ImGui::TextUnformatted("피벗 px (Enter)"); ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat2("##피벗", pivot, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                auto after = data; after.sheet.frames[i].pivot = {pivot[0], pivot[1]}; after.sheet.frames[i].pivotInPixels = true;
                if (after.Validate()) Commit(doc, std::move(after), "프레임 피벗");
            }
            ImGui::PopID();
        }
    }
    void DrawTimeline(Document& doc) {
        auto& data = doc.Animation();
        if (!ImGui::CollapsingHeader("타임라인")) return;
        ImGui::TextWrapped("프레임의 표시 시간을 초 단위로 입력하고 Enter로 적용합니다.");
        for (size_t i = 0; i < data.clip.frameIndices.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("%zu: 시트 %u / 표시 시간 (초)", i, data.clip.frameIndices[i]);
            float duration = data.clip.frameDurations[i]; ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##초", &duration, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                auto after = data; after.clip = anim_edit::WithFrameDuration(data.clip, i, duration);
                if (after.Validate()) Commit(doc, std::move(after), "프레임 시간");
            }
            if (ImGui::SmallButton("보기")) { m_cursor = {}; m_cursor.step = data.clip.direction == asset::AnimationClipData::Direction::Reverse
                    ? static_cast<uint32_t>(data.clip.frameIndices.size() - 1 - i) : static_cast<uint32_t>(i); m_playing = false; }
            ImGui::SameLine(); ImGui::BeginDisabled(i == 0);
            if (ImGui::SmallButton("<")) { auto after = data; after.clip = anim_edit::WithFrameMoved(data.clip, i, i - 1); Commit(doc, std::move(after), "순서 이동"); }
            ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(i + 1 == data.clip.frameIndices.size());
            if (ImGui::SmallButton(">")) { auto after = data; after.clip = anim_edit::WithFrameMoved(data.clip, i, i + 1); Commit(doc, std::move(after), "순서 이동"); }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::SmallButton("삭제")) { auto after = data; after.clip = anim_edit::WithFrameRemoved(data.clip, i); Commit(doc, std::move(after), "프레임 삭제"); ImGui::PopID(); break; }
            ImGui::PopID();
        }
    }
    void DrawEvents(Document& doc) {
        auto& data = doc.Animation();
        ImGui::TextWrapped("프레임이 재생될 때 전달할 이벤트입니다. Lua on_event의 animation 이벤트에서 이름과 인자를 처리합니다.");
        for (size_t i = 0; i < data.clip.events.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto& event = data.clip.events[i];
            ImGui::TextWrapped("%u / %s / %s / %.2f", event.frameIndex, event.name.c_str(), event.stringArg.c_str(), event.floatArg);
            if (ImGui::SmallButton("삭제")) { auto after = data; after.clip = anim_edit::WithEventRemoved(data.clip, i); Commit(doc, std::move(after), "이벤트 삭제"); ImGui::PopID(); break; }
            ImGui::PopID();
        }
        ImGui::TextUnformatted("프레임 위치 (0부터)"); ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##프레임 위치", &m_eventFrame);
        ImGui::TextUnformatted("이벤트 이름"); ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##이름", m_eventName.data(), m_eventName.size());
        ImGui::TextUnformatted("문자열 인자"); ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##문자열 인자", m_eventText.data(), m_eventText.size());
        ImGui::TextUnformatted("수치 인자"); ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##수치 인자", &m_eventValue);
        if (ImGui::Button("이벤트 추가") && m_eventFrame >= 0) {
            auto after = data;
            after.clip = anim_edit::WithEventAdded(data.clip, {static_cast<uint32_t>(m_eventFrame), m_eventName.data(), m_eventText.data(), m_eventValue});
            if (after.Validate()) Commit(doc, std::move(after), "이벤트 추가"); else m_status = "이벤트 프레임·이름·인자를 확인하세요.";
        }
    }
    DocumentId m_document{};
    uint64_t m_commandPosition = 0;
    std::string m_root, m_status;
    std::array<char, 4096> m_savePath{};
    std::array<int, 2> m_cell{64, 64};
    anim::ClipCursor m_cursor{};
    bool m_playing = false;
    float m_speed = 1.0f, m_eventValue = 0;
    int m_eventFrame = 0;
    std::array<char, 128> m_eventName{}, m_eventText{};
};
class AnimationEditorPanelFactory final : public IEditorPanelFactory {
public:
    const PanelDesc& Desc() const override { return kDesc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<AnimationEditorPanel>(); }
};
}
std::unique_ptr<IEditorPanelFactory> MakeAnimationEditorPanelFactory() { return std::make_unique<AnimationEditorPanelFactory>(); }
} // namespace mye::editor
