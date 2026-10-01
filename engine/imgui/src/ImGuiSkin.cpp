// Neutral editor surfaces; blue denotes focus/selection (docs/18-editor-authoring.md).
#include "mye/imgui/ImGuiSkin.h"

#include "imgui.h"

namespace mye::imgui {

namespace {
constexpr ImVec4 kAccent   {0.34f, 0.62f, 1.00f, 1.00f};
constexpr ImVec4 kAccentHi {0.48f, 0.72f, 1.00f, 1.00f};
constexpr ImVec4 kAccentLo {0.20f, 0.43f, 0.72f, 1.00f};

inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
} // namespace

void ApplySkin() {
    ImGui::StyleColorsDark();

    ImGuiStyle& st = ImGui::GetStyle();

    st.WindowRounding    = 3.0f;
    st.ChildRounding     = 3.0f;
    st.FrameRounding     = 3.0f;
    st.PopupRounding     = 3.0f;
    st.GrabRounding      = 2.0f;
    st.WindowPadding    = ImVec2(10, 10);
    st.FramePadding     = ImVec2(7, 4);
    st.ItemSpacing      = ImVec2(8, 6);
    st.ScrollbarSize     = 16.0f;
    st.ScrollbarRounding = 8.0f;
    st.TabRounding       = 3.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.64f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);

    c[ImGuiCol_FrameBgHovered]      = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_FrameBgActive]       = WithAlpha(kAccent, 0.55f);

    c[ImGuiCol_CheckMark]           = kAccent;
    c[ImGuiCol_SliderGrab]          = kAccent;
    c[ImGuiCol_SliderGrabActive]    = kAccentHi;

    c[ImGuiCol_Button]              = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = WithAlpha(kAccent, 0.70f);
    c[ImGuiCol_ButtonActive]        = kAccentLo;

    c[ImGuiCol_Header]              = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_HeaderHovered]       = WithAlpha(kAccent, 0.60f);
    c[ImGuiCol_HeaderActive]        = WithAlpha(kAccent, 0.80f);

    c[ImGuiCol_Tab]                 = ImVec4(0.18f, 0.18f, 0.18f, 1);
    c[ImGuiCol_TabHovered]          = WithAlpha(kAccent, 0.70f);
    c[ImGuiCol_TabActive]           = WithAlpha(kAccentLo, 0.90f);
    c[ImGuiCol_TabUnfocused]        = ImVec4(0.15f, 0.15f, 0.15f, 1);
    c[ImGuiCol_TabUnfocusedActive]  = WithAlpha(kAccentLo, 0.55f);

    c[ImGuiCol_TitleBgActive]       = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

    c[ImGuiCol_ResizeGrip]          = WithAlpha(kAccent, 0.30f);
    c[ImGuiCol_ResizeGripHovered]   = WithAlpha(kAccent, 0.65f);
    c[ImGuiCol_ResizeGripActive]    = WithAlpha(kAccentHi, 0.90f);
    c[ImGuiCol_SeparatorHovered]    = WithAlpha(kAccent, 0.65f);
    c[ImGuiCol_SeparatorActive]     = kAccentHi;
    c[ImGuiCol_PlotHistogramHovered]= kAccentHi;
    c[ImGuiCol_TextSelectedBg]      = WithAlpha(kAccent, 0.35f);
    c[ImGuiCol_NavHighlight]        = kAccent;
    c[ImGuiCol_DragDropTarget]      = kAccentHi;

    c[ImGuiCol_ScrollbarGrabHovered]= WithAlpha(kAccent, 0.55f);
    c[ImGuiCol_ScrollbarGrabActive] = WithAlpha(kAccent, 0.80f);

    c[ImGuiCol_DockingPreview]      = WithAlpha(kAccent, 0.55f);
}

} // namespace mye::imgui
