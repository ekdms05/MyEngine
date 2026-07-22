// AnimationEditorPanel.cpp — 애니메이션 에디터 패널 (docs/07 §애니메이션 에디터)
//
// 07: .anim 클립 편집기. SpriteSheet 프레임 그리드·타임라인(프레임/duration)·프리뷰(재생)·
//   이벤트 마커 편집(footstep 등)·8방향 세트 구성·리스트 기반 상태머신 전이 편집(그래프는 P2).
//   편집은 AnimClipEditCommand 경유(before/after 클립 스냅샷), 값은 AnimationClipData(04 스키마).
//
// 내장=1급 플러그인 — IEditorPanelFactory로 등록. 헤드리스 테스트는 커맨드·헬퍼(anim_edit)를 검증.
#include "mye/editor/Panel.h"
#include "mye/editor/EditorContext.h"
#include "mye/editor/Command.h"
#include "mye/editor/CommandStack.h"
#include "mye/editor/AnimEditing.h"

#include "imgui.h"

#include <memory>
#include <string>

namespace mye::editor {

namespace {

const PanelDesc kAnimDesc{
    /*id*/ "mye.anim",
    /*title*/ "애니메이션 에디터",
    /*allowMultiple*/ false,
    /*defaultDock*/ DockSlot::Center,
};

// 애니 에디터 문서 상태 — 에디터가 보유하는 클립 워킹 카피 + 프리뷰 재생 상태.
//   ConsolePanel 공유 저장소처럼 세션 전역 1개(단일 클립 문서 편집 가정).
struct AnimEditSession {
    asset::AnimationClipData clip;   // 편집 중인 클립(워킹 카피). 로드 시 채워짐.
    bool   loaded = false;
    // 프리뷰 재생.
    bool   playing = false;
    float  cursorTime = 0.0f;        // 초
    int    previewFrame = 0;
    float  speed = 1.0f;

    static AnimEditSession& Get() {
        static AnimEditSession s;
        return s;
    }

    float ClipDuration() const {
        float t = 0.0f;
        for (float d : clip.frameDurations) t += d;
        return t;
    }
    void Advance(float dt) {
        if (!playing || clip.frameDurations.empty()) return;
        const float total = ClipDuration();
        if (total <= 0.0f) return;
        cursorTime += dt * speed;
        if (clip.loop) { while (cursorTime >= total) cursorTime -= total; }
        else if (cursorTime > total) { cursorTime = total; playing = false; }
        float acc = 0.0f;
        previewFrame = 0;
        for (size_t i = 0; i < clip.frameDurations.size(); ++i) {
            acc += clip.frameDurations[i];
            if (cursorTime < acc) { previewFrame = static_cast<int>(i); break; }
            previewFrame = static_cast<int>(i);
        }
    }
};

static CommandStack* Stack(EditorContext& ctx) {
    return ctx.commands;
}

// 클립 전체 변경을 AnimClipEditCommand로 커밋(before=현재 세션 클립, after=변경본).
static void PushClipEdit(EditorContext& ctx, AnimEditSession& s,
                         asset::AnimationClipData after, std::string label) {
    CommandStack* stack = Stack(ctx);
    if (!stack) { s.clip = std::move(after); return; }   // 스택 없으면 직접 적용
    stack->Push(std::make_unique<AnimClipEditCommand>(&s.clip, s.clip, std::move(after),
                                                       std::move(label)));
}

class AnimationEditorPanel final : public IEditorPanel {
public:
    const PanelDesc& Desc() const override { return kAnimDesc; }

    void OnGui(EditorContext& ctx) override {
        if (!ImGui::Begin("애니메이션 에디터")) { ImGui::End(); return; }
        AnimEditSession& s = AnimEditSession::Get();

        if (!s.loaded) {
            if (ImGui::Button("새 클립 만들기")) {
                s.clip = asset::AnimationClipData{};
                s.clip.name = "clip";
                s.loaded = true;
            }
            ImGui::TextDisabled("에셋 브라우저에서 .anim 더블클릭 시 로드(후속 배선)");
            ImGui::End();
            return;
        }

        // 프리뷰 재생 진행.
        s.Advance(ImGui::GetIO().DeltaTime);

        DrawHeader(ctx, s);
        ImGui::Separator();
        DrawTransport(s);
        ImGui::Separator();
        DrawFrames(ctx, s);
        ImGui::Separator();
        DrawEvents(ctx, s);

        ImGui::End();
    }

    void SerializeState(json::Value& out) const override {
        json::Value::Object o;
        o["type"] = json::Value(std::string("mye.anim"));
        out = json::Value(std::move(o));
    }

private:
    static void DrawHeader(EditorContext&, AnimEditSession& s) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", s.clip.name.c_str());
        if (ImGui::InputText("클립 이름", buf, sizeof(buf))) s.clip.name = buf;
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &s.clip.loop);
        ImGui::Text("프레임 %zu개  길이 %.2fs", s.clip.frameIndices.size(), s.ClipDuration());
    }

    static void DrawTransport(AnimEditSession& s) {
        if (ImGui::Button(s.playing ? "❚❚ 정지" : "▶ 재생")) s.playing = !s.playing;
        ImGui::SameLine();
        if (ImGui::Button("⏮")) { s.cursorTime = 0; s.previewFrame = 0; }
        ImGui::SameLine();
        ImGui::SliderFloat("speed", &s.speed, 0.1f, 4.0f, "x%.2f");
        ImGui::Text("프리뷰 프레임: %d", s.previewFrame);
    }

    static void DrawFrames(EditorContext& ctx, AnimEditSession& s) {
        ImGui::TextUnformatted("프레임:");
        static int newFrameIdx = 0;
        static float newDur = 0.1f;
        ImGui::InputInt("추가 프레임 idx", &newFrameIdx);
        ImGui::InputFloat("duration", &newDur);
        if (ImGui::Button("+ 프레임 추가")) {
            PushClipEdit(ctx, s,
                         anim_edit::WithFrameAppended(s.clip,
                             static_cast<uint32_t>(std::max(0, newFrameIdx)), newDur),
                         "프레임 추가");
        }
        for (size_t i = 0; i < s.clip.frameIndices.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("f%zu: sheet[%u]", i, s.clip.frameIndices[i]);
            ImGui::SameLine();
            float d = (i < s.clip.frameDurations.size()) ? s.clip.frameDurations[i] : 0.0f;
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputFloat("dur", &d))
                PushClipEdit(ctx, s, anim_edit::WithFrameDuration(s.clip, i, d), "duration 변경");
            ImGui::SameLine();
            if (ImGui::SmallButton("삭제"))
                PushClipEdit(ctx, s, anim_edit::WithFrameRemoved(s.clip, i), "프레임 삭제");
            ImGui::PopID();
        }
    }

    static void DrawEvents(EditorContext& ctx, AnimEditSession& s) {
        ImGui::TextUnformatted("이벤트 마커:");
        static int evFrame = 0;
        static char evName[64] = "footstep";
        ImGui::InputInt("프레임", &evFrame);
        ImGui::InputText("이름", evName, sizeof(evName));
        if (ImGui::Button("+ 이벤트 추가")) {
            asset::AnimEventMarker ev;
            ev.frameIndex = static_cast<uint32_t>(std::max(0, evFrame));
            ev.name = evName;
            PushClipEdit(ctx, s, anim_edit::WithEventAdded(s.clip, ev), "이벤트 추가");
        }
        for (size_t i = 0; i < s.clip.events.size(); ++i) {
            ImGui::PushID(static_cast<int>(1000 + i));
            ImGui::Text("◆ f%u: %s", s.clip.events[i].frameIndex, s.clip.events[i].name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("삭제"))
                PushClipEdit(ctx, s, anim_edit::WithEventRemoved(s.clip, i), "이벤트 삭제");
            ImGui::PopID();
        }
    }
};

template <class P>
class SimpleFactory final : public IEditorPanelFactory {
public:
    explicit SimpleFactory(const PanelDesc& d) : m_desc(d) {}
    const PanelDesc& Desc() const override { return m_desc; }
    std::unique_ptr<IEditorPanel> Create() override { return std::make_unique<P>(); }
private:
    const PanelDesc& m_desc;
};

} // namespace

std::unique_ptr<IEditorPanelFactory> MakeAnimationEditorPanelFactory() {
    return std::make_unique<SimpleFactory<AnimationEditorPanel>>(kAnimDesc);
}

} // namespace mye::editor
